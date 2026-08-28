/*
 * stb_pic2.h - PIC2 image decoder (stb_image style, single-file library)
 *
 * Loads PIC2 (.p2) format image files and returns 24bpp RGB pixel data.
 * Supports all four block formats: P2SS, P2SF, P2BM, P2BI.
 *
 * Usage:
 *   #define STB_PIC2_IMPLEMENTATION
 *   #include "stb_pic2.h"
 *
 *   int w, h;
 *   unsigned char *pixels = LoadPIC2("image.p2", &w, &h);
 *   if (pixels) {
 *       // use pixels (w * h * 3 bytes of RGB data)
 *       free(pixels);
 *   }
 *
 * Based on the PIC2 format specification and xvpic2.c reference implementation.
 */

#ifndef STB_PIC2_H
#define STB_PIC2_H

unsigned char *LoadPIC2(const char *filename, int *out_w, int *out_h);

#endif /* STB_PIC2_H */

#ifdef STB_PIC2_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================ */
/*  Internal types                                               */
/* ============================================================ */

typedef unsigned long  pic2_pixel;
typedef unsigned char  pic2_byte;

#define PIC2_HEADER_SIZE       124
#define PIC2_BLOCK_HEADER_SIZE  26
#define PIC2_ARITH_CACHE       32
#define PIC2_ARITH_CONTEXT    128
#define PIC2_FAST_CACHE        64
#define PIC2_SHIFT_BITS(b, n)  (((n) > 0) ? ((b) << (n)) : ((b) >> -(n)))

/* ============================================================ */
/*  Big-endian readers                                           */
/* ============================================================ */

static int pic2_read_be16(FILE *fp)
{
    unsigned char buf[2];
    if (fread(buf, 1, 2, fp) < 2) return -1;
    return ((int)buf[0] << 8) | buf[1];
}

static long pic2_read_be32(FILE *fp)
{
    unsigned char buf[4];
    if (fread(buf, 1, 4, fp) < 4) return -1;
    return ((long)buf[0] << 24) | ((long)buf[1] << 16) |
           ((long)buf[2] << 8)  |  (long)buf[3];
}

/* ============================================================ */
/*  Bitstream reader (MSB-first)                                 */
/* ============================================================ */

typedef struct {
    FILE           *fp;
    int             rest;       /* bits remaining in cur */
    unsigned char   cur;        /* current byte being consumed */
} pic2_bs;

static unsigned long pic2_read_bits(pic2_bs *bs, int n)
{
    unsigned long r = 0;
    while (n > 0) {
        while (bs->rest > 0 && n > 0) {
            r = (r << 1) | ((bs->cur >> 7) & 1);
            bs->cur <<= 1;
            bs->rest--;
            n--;
        }
        if (n > 0) {
            int c = fgetc(bs->fp);
            if (c == EOF) return r;
            bs->cur  = (unsigned char)c;
            bs->rest = 8;
        }
    }
    return r;
}

/* ============================================================ */
/*  Color utilities                                              */
/* ============================================================ */

static pic2_byte pic2_convert_color_bits(int c, int from, int to)
{
    if (from == to) return (pic2_byte)c;
    if (from < to) {
        /* Pad: replicate MSBs into LSBs */
        pic2_byte p = 0;
        int remaining = to;
        while (remaining > 0) {
            remaining -= from;
            if (remaining >= 0)
                p |= (pic2_byte)(c << remaining);
            else
                p |= (pic2_byte)(c >> (-remaining));
        }
        return p;
    }
    return (pic2_byte)(c >> (from - to));
}

static pic2_pixel pic2_exchange_rg(pic2_pixel p, int colbits)
{
    pic2_pixel rmask = ((pic2_pixel)0xff >> (8 - colbits)) << (colbits * 2);
    pic2_pixel gmask = ((pic2_pixel)0xff >> (8 - colbits)) << colbits;
    pic2_pixel bmask = ((pic2_pixel)0xff >> (8 - colbits));
    return ((p << colbits) & rmask) | ((p >> colbits) & gmask) | (p & bmask);
}

/* ============================================================ */
/*  Decoder state                                                */
/* ============================================================ */

typedef struct {
    FILE            *fp;
    pic2_bs          bs;

    long             fsize;

    /* Header fields */
    short            hdr_flag;
    long             hdr_size;       /* offset to first block */
    short            hdr_depth;      /* bits per pixel (3,6,9,12,15,18,21,24) */
    short            hdr_x_max, hdr_y_max;

    /* Palette (only used if hdr_flag & 1) */
    int              pal_bits;
    int              n_pal;
    pic2_byte        pal[256][3];

    /* Current block */
    char             block_id[4];
    long             block_size;
    short            block_flag;
    short            x_wid, y_wid;
    short            x_offset, y_offset;
    pic2_pixel       block_opaque;

    /* Full image dimensions (may grow from multi-block) */
    short            x_max, y_max;

    /* Line buffers (allocated with +8 padding for negative indexing) */
    pic2_pixel      *vram_prev;
    pic2_pixel      *vram_now;
    pic2_pixel      *vram_next;
    short           *flag_now;
    short           *flag_next;
    short           *flag2_now;
    short           *flag2_next;
    short           *flag2_next2;

    /* Cache (shared between arithmetic and fast modes) */
    pic2_pixel     (*cache)[PIC2_ARITH_CACHE];
    unsigned short  *cache_pos;
    unsigned short  *mulu_tab;

    /* Arithmetic decoder state */
    long             aa;
    long             dd;
    int              cache_hit_c;

    /* Fast decoder state */
    int              fast_aa;
    int              fast_dd;

    /* Line tracking */
    int              ynow;
    long             block_pos;
    long             data_pos;       /* where to read next block header */

    /* Work buffer */
    pic2_byte       *buf;
} pic2_state;

/* ============================================================ */
/*  Line buffer management (handle_para)                         */
/*  Saves current pointers, advances by 4 for negative indexing, */
/*  then rotates buffers on mode 1.                              */
/* ============================================================ */

static void pic2_handle_para(pic2_state *s, int mode)
{
    static pic2_pixel *sv_prev, *sv_now, *sv_next;
    static short      *sv_fnow, *sv_fnext;
    static short      *sv_f2now, *sv_f2next, *sv_f2next2;

    switch (mode) {
    case 0:
        sv_prev    = s->vram_prev;
        sv_now     = s->vram_now;
        sv_next    = s->vram_next;
        sv_fnow    = s->flag_now;
        sv_fnext   = s->flag_next;
        sv_f2now   = s->flag2_now;
        sv_f2next  = s->flag2_next;
        sv_f2next2 = s->flag2_next2;
        s->vram_prev    += 4;
        s->vram_now     += 4;
        s->vram_next    += 4;
        s->flag_now     += 4;
        s->flag_next    += 4;
        s->flag2_now    += 4;
        s->flag2_next   += 4;
        s->flag2_next2  += 4;
        break;
    case 1:
        s->vram_prev    = sv_now;
        s->vram_now     = sv_next;
        s->vram_next    = sv_prev;
        s->flag_now     = sv_fnext;
        s->flag_next    = sv_fnow;
        s->flag2_now    = sv_f2next;
        s->flag2_next   = sv_f2next2;
        s->flag2_next2  = sv_f2now;
        break;
    }
}

/* ============================================================ */
/*  Buffer allocation / free                                     */
/* ============================================================ */

static void pic2_alloc_buffer(pic2_state *s)
{
    int wid = s->x_wid;
    pic2_byte *p;
    size_t total;

    if (s->buf) return;

    total = (size_t)(wid + 8) * sizeof(pic2_pixel) * 3          /* vram_prev/now/next */
          + sizeof(pic2_pixel) * PIC2_ARITH_CACHE * 8 * 8 * 8   /* cache */
          + sizeof(unsigned short) * 8 * 8 * 8                   /* cache_pos */
          + sizeof(unsigned short) * 16384                        /* mulu_tab */
          + sizeof(short) * (wid + 8) * 5;                       /* flag arrays */

    s->buf = (pic2_byte *)calloc(total, 1);
    if (!s->buf) return;

    p = s->buf;
    s->vram_prev    = (pic2_pixel *)p;  p += (wid + 8) * sizeof(pic2_pixel);
    s->vram_now     = (pic2_pixel *)p;  p += (wid + 8) * sizeof(pic2_pixel);
    s->vram_next    = (pic2_pixel *)p;  p += (wid + 8) * sizeof(pic2_pixel);
    s->cache        = (pic2_pixel (*)[PIC2_ARITH_CACHE])p;
                                          p += sizeof(pic2_pixel) * PIC2_ARITH_CACHE * 8 * 8 * 8;
    s->cache_pos    = (unsigned short *)p; p += sizeof(unsigned short) * 8 * 8 * 8;
    s->mulu_tab     = (unsigned short *)p; p += sizeof(unsigned short) * 16384;
    s->flag_now     = (short *)p;        p += sizeof(short) * (wid + 8);
    s->flag_next    = (short *)p;        p += sizeof(short) * (wid + 8);
    s->flag2_now    = (short *)p;        p += sizeof(short) * (wid + 8);
    s->flag2_next   = (short *)p;        p += sizeof(short) * (wid + 8);
    s->flag2_next2  = (short *)p;        p += sizeof(short) * (wid + 8);
}

static void pic2_free_buffer(pic2_state *s)
{
    if (s->buf) { free(s->buf); s->buf = NULL; }
}

/* ============================================================ */
/*  Block finding                                                */
/* ============================================================ */

static int pic2_read_block_header1(pic2_state *s)
{
    if (fread(s->block_id, 1, 4, s->fp) < 4) return -1;
    s->block_size = pic2_read_be32(s->fp);
    return 0;
}

static int pic2_read_block_header2(pic2_state *s)
{
    s->block_flag   = (short)pic2_read_be16(s->fp);
    s->x_wid        = (short)pic2_read_be16(s->fp);
    s->y_wid        = (short)pic2_read_be16(s->fp);
    s->x_offset     = (short)pic2_read_be16(s->fp);
    s->y_offset     = (short)pic2_read_be16(s->fp);
    s->block_opaque = (pic2_pixel)pic2_read_be32(s->fp);
    pic2_read_be32(s->fp); /* reserve */
    return 0;
}

static int pic2_next_block(pic2_state *s)
{
    int i;
    static const char *form_ids[] = { "P2SS", "P2SF", "P2BM", "P2BI" };

    fseek(s->fp, s->data_pos, SEEK_SET);

    if (pic2_read_block_header1(s) != 0)
        return -1;

    /* End block (id[0] == 0) */
    if (s->block_id[0] == 0)
        return 0;

    s->block_pos = s->data_pos;
    s->data_pos += s->block_size;

    /* Check if known block type */
    for (i = 0; i < 4; i++) {
        if (memcmp(s->block_id, form_ids[i], 4) == 0)
            break;
    }
    if (i == 4)
        return 2; /* unknown block, skip */

    /* Read rest of block header */
    pic2_read_block_header2(s);

    if (s->x_offset + s->x_wid > s->x_max)
        s->x_max = s->x_offset + s->x_wid;
    if (s->y_offset + s->y_wid > s->y_max)
        s->y_max = s->y_offset + s->y_wid;

    return 1;
}

static int pic2_find_block(pic2_state *s)
{
    s->data_pos = s->hdr_size;
    return pic2_next_block(s);
}

/* ============================================================ */
/*  P2SS: Arithmetic decoder                                     */
/* ============================================================ */

static int pic2_arith_decode_bit(pic2_state *s, int c)
{
    unsigned short pp;

    pp = s->mulu_tab[(s->aa & 0x7f00) / 2 + c];
    if (s->dd >= (int)pp) {
        s->dd -= pp;
        s->aa -= pp;
        while ((short)s->aa >= 0) {
            s->dd *= 2;
            if (pic2_read_bits(&s->bs, 1))
                s->dd++;
            s->aa *= 2;
        }
        return 1;
    } else {
        s->aa = pp;
        while ((short)s->aa >= 0) {
            s->dd *= 2;
            if (pic2_read_bits(&s->bs, 1))
                s->dd++;
            s->aa *= 2;
        }
        return 0;
    }
}

static int pic2_arith_decode_nn(pic2_state *s, int c)
{
    int n;

    if (pic2_arith_decode_bit(s, c)) {
        n = 0;
    } else if (pic2_arith_decode_bit(s, c + 1)) {
        n = 1;
        if (pic2_arith_decode_bit(s, c + 8))
            n += 1;
    } else if (pic2_arith_decode_bit(s, c + 2)) {
        n = 1 + 2;
        if (pic2_arith_decode_bit(s, c + 8))
            n += 1;
        if (pic2_arith_decode_bit(s, c + 9))
            n += 2;
    } else if (pic2_arith_decode_bit(s, c + 3)) {
        n = 1 + 2 + 4;
        if (pic2_arith_decode_bit(s, c + 8))
            n += 1;
        if (pic2_arith_decode_bit(s, c + 9))
            n += 2;
        if (pic2_arith_decode_bit(s, c + 10))
            n += 4;
    } else if (pic2_arith_decode_bit(s, c + 4)) {
        n = 1 + 2 + 4 + 8;
        if (pic2_arith_decode_bit(s, c + 8))
            n += 1;
        if (pic2_arith_decode_bit(s, c + 9))
            n += 2;
        if (pic2_arith_decode_bit(s, c + 10))
            n += 4;
        if (pic2_arith_decode_bit(s, c + 11))
            n += 8;
    } else if (pic2_arith_decode_bit(s, c + 5)) {
        n = 1 + 2 + 4 + 8 + 16;
        if (pic2_arith_decode_bit(s, c + 8))
            n += 1;
        if (pic2_arith_decode_bit(s, c + 9))
            n += 2;
        if (pic2_arith_decode_bit(s, c + 10))
            n += 4;
        if (pic2_arith_decode_bit(s, c + 11))
            n += 8;
        if (pic2_arith_decode_bit(s, c + 12))
            n += 16;
    } else if (pic2_arith_decode_bit(s, c + 6)) {
        n = 1 + 2 + 4 + 8 + 16 + 32;
        if (pic2_arith_decode_bit(s, c + 8))
            n += 1;
        if (pic2_arith_decode_bit(s, c + 9))
            n += 2;
        if (pic2_arith_decode_bit(s, c + 10))
            n += 4;
        if (pic2_arith_decode_bit(s, c + 11))
            n += 8;
        if (pic2_arith_decode_bit(s, c + 12))
            n += 16;
        if (pic2_arith_decode_bit(s, c + 13))
            n += 32;
    } else if (pic2_arith_decode_bit(s, c + 7)) {
        n = 1 + 2 + 4 + 8 + 16 + 32 + 64;
        if (pic2_arith_decode_bit(s, c + 8))
            n += 1;
        if (pic2_arith_decode_bit(s, c + 9))
            n += 2;
        if (pic2_arith_decode_bit(s, c + 10))
            n += 4;
        if (pic2_arith_decode_bit(s, c + 11))
            n += 8;
        if (pic2_arith_decode_bit(s, c + 12))
            n += 16;
        if (pic2_arith_decode_bit(s, c + 13))
            n += 32;
        if (pic2_arith_decode_bit(s, c + 14))
            n += 64;
    } else {
        n = 1 + 2 + 4 + 8 + 16 + 32 + 64 + 128;
    }
    return n;
}

static void pic2_arith_expand_chain(pic2_state *s, int x, pic2_pixel cc)
{
    static const unsigned short c_tab[] = {
        80 + 6 * 5, 80 + 6 * 4, 80 + 6 * 3,
        80 + 6 * 2, 80 + 6 * 1, 80 + 6 * 0, 80 + 6 * 0
    };
    unsigned short b;

    b = c_tab[s->flag_now[x] + 5];
    if (!pic2_arith_decode_bit(s, b++)) {
        if (pic2_arith_decode_bit(s, b++)) {
            s->vram_next[x]     = cc; s->flag_next[x]     = -1;
        } else if (pic2_arith_decode_bit(s, b++)) {
            s->vram_next[x - 1] = cc; s->flag_next[x - 1] = -2;
        } else if (pic2_arith_decode_bit(s, b++)) {
            s->vram_next[x + 1] = cc; s->flag_next[x + 1] = -3;
        } else if (pic2_arith_decode_bit(s, b++)) {
            s->vram_next[x - 2] = cc; s->flag_next[x - 2] = -4;
        } else {
            s->vram_next[x + 2] = cc; s->flag_next[x + 2] = -5;
        }
    }
}

static int pic2_arith_get_number(pic2_state *s, int c, int bef)
{
    unsigned short n;
    pic2_byte maxcol;

    maxcol = (pic2_byte)(0xff >> (8 - s->hdr_depth / 3));

    n = (unsigned short)pic2_arith_decode_nn(s, c);
    if (bef > ((int)maxcol >> 1)) {
        if (n > ((int)maxcol - bef) * 2)
            n = maxcol - n;
        else if (n & 1)
            n = n / 2 + bef + 1;
        else
            n = bef - n / 2;
    } else {
        if ((int)n > (bef * 2))
            n = n;
        else if (n & 1)
            n = n / 2 + bef + 1;
        else
            n = bef - n / 2;
    }
    return (int)n;
}

static pic2_pixel pic2_arith_read_color(pic2_state *s, int x)
{
    pic2_pixel c1, c2, cc;
    unsigned short i, j, k, m;
    int r, g, b, r0, g0, b0;
    int colbits;
    pic2_pixel rmask, gmask, bmask;
    pic2_byte maxcol;

    colbits = s->hdr_depth / 3;
    rmask = ((pic2_pixel)0xff >> (8 - colbits)) << (colbits * 2);
    gmask = ((pic2_pixel)0xff >> (8 - colbits)) << colbits;
    bmask = ((pic2_pixel)0xff >> (8 - colbits));
    maxcol = (pic2_byte)bmask;

    c1 = s->vram_prev[x];
    k = (unsigned short)(
        ((c1 >> ((colbits - 3) * 3)) & 0x1c0) |
        ((c1 >> ((colbits - 3) * 2)) & 0x038) |
        ((c1 >>  (colbits - 3)     ) & 0x007));
    if (colbits == 5)
        k = (unsigned short)pic2_exchange_rg(k, 3);

    if (pic2_arith_decode_bit(s, s->cache_hit_c)) {
        /* Cache miss: predict and read delta */
        s->cache_hit_c = 16;

        c2 = s->vram_now[x - 1];
        r = (int)(((c1 & rmask) + (c2 & rmask)) >> (colbits * 2 + 1));
        g = (int)(((c1 & gmask) + (c2 & gmask)) >> (colbits + 1));
        b = (int)(((c1 & bmask) + (c2 & bmask)) >> 1);

        g0 = pic2_arith_get_number(s, 32, g);
        r = r + g0 - g;
        if (r > (int)maxcol) r = maxcol; else if (r < 0) r = 0;

        b = b + g0 - g;
        if (b > (int)maxcol) b = maxcol; else if (b < 0) b = 0;

        r0 = pic2_arith_get_number(s, 48, r);
        b0 = pic2_arith_get_number(s, 64, b);

        s->cache_pos[k] = j = (s->cache_pos[k] - 1) & (PIC2_ARITH_CACHE - 1);
        s->cache[k][j] = cc = ((pic2_pixel)r0 << (colbits * 2))
                             | ((pic2_pixel)g0 << colbits)
                             | (pic2_pixel)b0;
    } else {
        /* Cache hit: read from cache */
        s->cache_hit_c = 15;

        j = (unsigned short)pic2_arith_decode_nn(s, 17);
        m = s->cache_pos[k];
        i = (m + j / 2) & (PIC2_ARITH_CACHE - 1);
        j = (m + j)     & (PIC2_ARITH_CACHE - 1);

        cc = s->cache[k][j];
        s->cache[k][j] = s->cache[k][i];
        s->cache[k][i] = s->cache[k][m];
        s->cache[k][m] = cc;
    }
    return cc;
}

static int pic2_arith_expand_line(pic2_state *s, pic2_pixel **line)
{
    int x, xw, ymax;
    pic2_pixel cc;

    pic2_handle_para(s, 0);

    xw   = s->x_wid;
    ymax = s->y_wid - 1;

    if (s->ynow > ymax)
        return -2;

    if (s->ynow == 0)
        cc = 0;
    else
        cc = s->vram_prev[xw - 1];
    s->vram_now[-1] = cc;

    memset(s->flag_next, 0, xw * sizeof(short));
    memset(s->flag2_next2, 0, xw * sizeof(short));

    for (x = 0; x < xw; x++) {
        if (s->flag_now[x] < 0) {
            cc = s->vram_now[x];
            if (s->ynow < ymax)
                pic2_arith_expand_chain(s, x, cc);
        } else if (pic2_arith_decode_bit(s, s->flag2_now[x])) {
            /* Change point */
            s->flag2_now  [x + 1]++;
            s->flag2_now  [x + 2]++;
            s->flag2_next [x - 1]++;
            s->flag2_next [x    ]++;
            s->flag2_next [x + 1]++;
            s->flag2_next2[x - 1]++;
            s->flag2_next2[x    ]++;
            s->flag2_next2[x + 1]++;

            s->vram_now[x] = cc = pic2_arith_read_color(s, x);
            if (s->ynow < ymax)
                pic2_arith_expand_chain(s, x, cc);
        } else {
            s->vram_now[x] = cc;
        }
    }

    if (line) *line = s->vram_now;
    s->ynow++;

    pic2_handle_para(s, 1);
    return s->ynow - 1;
}

static int pic2_arith_loader_init(pic2_state *s)
{
    unsigned short p2b[PIC2_ARITH_CONTEXT];
    int i, xw;

    s->ynow = 0;

    if (s->hdr_depth % 3)
        return -1;

    xw = s->x_wid;
    memset(s->cache, 0, sizeof(pic2_pixel) * 8 * 8 * 8);
    memset(s->cache_pos, 0, sizeof(unsigned short) * 8 * 8 * 8);
    memset(s->flag_now, 0, xw * sizeof(short));
    memset(s->flag2_now, 0, (8 + xw) * sizeof(short));
    memset(s->flag2_next, 0, (8 + xw) * sizeof(short));

    fseek(s->fp, s->block_pos + PIC2_BLOCK_HEADER_SIZE, SEEK_SET);

    s->bs.rest = 0;
    s->bs.cur  = 0;

    for (i = 0; i < PIC2_ARITH_CONTEXT; i++)
        p2b[i] = (unsigned short)pic2_read_be16(s->fp);

    for (i = 0; i < 16384; i++) {
        s->mulu_tab[i] = (unsigned short)((long)(i / 128 + 128) * (int)p2b[i & 127] / 256);
        if (s->mulu_tab[i] == 0) s->mulu_tab[i] = 1;
    }

    s->aa = 0xffff;
    s->dd = 0;
    for (i = 0; i < 16; i++) {
        s->dd *= 2;
        if (pic2_read_bits(&s->bs, 1))
            s->dd |= 1;
    }
    s->cache_hit_c = 16;

    return 0;
}

/* ============================================================ */
/*  P2SF: Fast decoder                                           */
/* ============================================================ */

static int pic2_fast_read_length(pic2_state *s)
{
    int a = 0;
    while (pic2_read_bits(&s->bs, 1))
        a++;
    if (a == 0) return 0;
    return (int)(pic2_read_bits(&s->bs, a) + (1u << a) - 1);
}

static void pic2_fast_expand_chain(pic2_state *s, int x, pic2_pixel cc)
{
    if (pic2_read_bits(&s->bs, 1)) {
        if (pic2_read_bits(&s->bs, 1)) {
            s->vram_next[x]     = cc; s->flag_next[x]     = -1;
        } else if (pic2_read_bits(&s->bs, 1)) {
            if (pic2_read_bits(&s->bs, 1) == 0) {
                s->vram_next[x - 2] = cc; s->flag_next[x - 2] = -1;
            } else {
                s->vram_next[x - 1] = cc; s->flag_next[x - 1] = -1;
            }
        } else {
            if (pic2_read_bits(&s->bs, 1) == 0) {
                s->vram_next[x + 2] = cc; s->flag_next[x + 2] = -1;
            } else {
                s->vram_next[x + 1] = cc; s->flag_next[x + 1] = -1;
            }
        }
    }
}

static pic2_pixel pic2_fast_read_color(pic2_state *s, pic2_pixel bc)
{
    pic2_pixel cc;
    unsigned short j, k, m;
    int colbits;
    pic2_pixel (*cache)[PIC2_FAST_CACHE];

    colbits = s->hdr_depth / 3;
    cache = (pic2_pixel (*)[PIC2_FAST_CACHE])s->cache;

    k = (unsigned short)PIC2_SHIFT_BITS(bc, 8 - s->hdr_depth);
    if (pic2_read_bits(&s->bs, 1) == 0) {
        /* New color: read from bitstream */
        s->cache_pos[k] = m = (s->cache_pos[k] - 1) & (PIC2_FAST_CACHE - 1);
        cc = pic2_read_bits(&s->bs, s->hdr_depth);
        cache[k][m] = cc;
    } else {
        /* Cache hit: read index */
        j = (unsigned short)pic2_read_bits(&s->bs, 6);
        m = s->cache_pos[k];
        cc = cache[k][(m + j) & (PIC2_FAST_CACHE - 1)];
    }
    return cc;
}

static int pic2_fast_expand_line(pic2_state *s, pic2_pixel **line)
{
    int x, xw, ymax;
    pic2_pixel cc;

    pic2_handle_para(s, 0);

    xw   = s->x_wid;
    ymax = s->y_wid - 1;

    if (s->ynow > ymax)
        return -2;

    if (s->ynow == 0) {
        s->fast_dd = 0;
        s->fast_aa = pic2_fast_read_length(s);
        if (s->fast_aa == 1023)
            s->fast_dd = 1023;
        else if (s->fast_aa > 1023)
            s->fast_aa--;
        cc = 0;
    } else {
        cc = s->vram_prev[xw - 1];
    }

    memset(s->flag_next, 0, xw * sizeof(short));

    for (x = 0; x < xw; x++) {
        if (s->fast_dd > 0) {
            if (s->flag_now[x] < 0) {
                cc = s->vram_now[x];
                pic2_fast_expand_chain(s, x, cc);
                if (--s->fast_dd == 0) {
                    s->fast_aa = pic2_fast_read_length(s);
                    if (s->fast_aa == 1023)
                        s->fast_dd = 1023;
                    else if (s->fast_aa > 1023)
                        s->fast_aa--;
                }
            } else {
                s->vram_now[x] = cc;
            }
        } else {
            if (s->flag_now[x] < 0) {
                cc = s->vram_now[x];
                pic2_fast_expand_chain(s, x, cc);
            } else if (--s->fast_aa < 0) {
                cc = s->vram_now[x] = pic2_fast_read_color(s, cc);
                pic2_fast_expand_chain(s, x, cc);
                s->fast_aa = pic2_fast_read_length(s);
                if (s->fast_aa == 1023)
                    s->fast_dd = 1023;
                else if (s->fast_aa > 1023)
                    s->fast_aa--;
            } else {
                s->vram_now[x] = cc;
            }
        }
    }

    if (line) *line = s->vram_now;
    s->ynow++;

    pic2_handle_para(s, 1);
    return s->ynow - 1;
}

static int pic2_fast_loader_init(pic2_state *s)
{
    int xw;

    s->ynow = 0;

    if (s->hdr_depth % 3)
        return -1;

    xw = s->x_wid;
    memset(s->cache, 0, sizeof(pic2_pixel) * 256);
    memset(s->cache_pos, 0, sizeof(unsigned short) * 8 * 8 * 8);
    memset(s->flag_now, 0, (xw + 8) * sizeof(short));
    memset(s->flag_next, 0, (xw + 8) * sizeof(short));

    fseek(s->fp, s->block_pos + PIC2_BLOCK_HEADER_SIZE, SEEK_SET);

    s->bs.rest = 0;
    s->bs.cur  = 0;

    return 0;
}

/* ============================================================ */
/*  P2BM / P2BI: Raw bitmap decoder                              */
/* ============================================================ */

static int pic2_beta_expand_line(pic2_state *s, pic2_pixel **line)
{
    int i, xw, ymax;
    pic2_byte a, b, c, *p;
    pic2_pixel *pc;
    int depth, pixbyte, colbits;

    depth   = s->hdr_depth;
    pixbyte = depth / 8 + ((depth % 8) > 0);
    colbits = depth / 3;
    xw      = s->x_wid;
    ymax    = s->y_wid - 1;

    if (s->ynow > ymax)
        return -2;

    pc = s->vram_now;
    p  = (pic2_byte *)s->vram_prev;

    if (pixbyte == 3) {
        if (fread(p, 1, (size_t)(xw * 3), s->fp) < (size_t)(xw * 3))
            return -1;
        for (i = 0; i < xw; i++, pc++) {
            a = *p++; b = *p++; c = *p++;
            *pc = ((pic2_pixel)a << 16) | ((pic2_pixel)b << 8) | c;
        }
    } else if (pixbyte == 2) {
        if (fread(p, 1, (size_t)(xw * 2), s->fp) < (size_t)(xw * 2))
            return -1;
        if (memcmp(s->block_id, "P2BM", 4) == 0) {
            for (i = 0; i < xw; i++, pc++) {
                a = *p++; b = *p++;
                *pc = ((pic2_pixel)a << 8) | b;
                if (colbits == 5) {
                    *pc >>= 1;
                    *pc = pic2_exchange_rg(*pc, colbits);
                }
            }
        } else {
            for (i = 0; i < xw; i++, pc++) {
                a = *p++; b = *p++;
                *pc = ((pic2_pixel)b << 8) | a;
                if (colbits == 5) {
                    *pc >>= 1;
                    *pc = pic2_exchange_rg(*pc, colbits);
                }
            }
        }
    } else {
        if (fread(p, 1, (size_t)xw, s->fp) < (size_t)xw)
            return -1;
        for (i = 0; i < xw; i++)
            *pc++ = *p++;
    }

    if (line) *line = s->vram_now;

    {
        pic2_pixel *tmp = s->vram_prev;
        s->vram_prev = s->vram_now;
        s->vram_now  = s->vram_next;
        s->vram_next = tmp;
    }

    s->ynow++;
    return s->ynow - 1;
}

static int pic2_beta_loader_init(pic2_state *s)
{
    s->ynow = 0;
    fseek(s->fp, s->block_pos + PIC2_BLOCK_HEADER_SIZE, SEEK_SET);
    return 0;
}

/* ============================================================ */
/*  LoadPIC2: Main entry point                                   */
/* ============================================================ */

unsigned char *LoadPIC2(const char *filename, int *out_w, int *out_h)
{
    pic2_state s;
    FILE *fp;
    pic2_pixel *linep = NULL;
    unsigned char *rgb_buffer = NULL;
    int total_pixels, i, line;
    int colbits, colmask;
    int loader_type;
    int pw, ph;
    static const char *form_ids[] = { "P2SS", "P2SF", "P2BM", "P2BI" };

    if (!filename || !out_w || !out_h) return NULL;
    *out_w = 0; *out_h = 0;

    memset(&s, 0, sizeof(s));

    fp = fopen(filename, "rb");
    if (!fp) return NULL;
    s.fp = fp;
    s.bs.fp = fp;

    fseek(fp, 0, SEEK_END);
    s.fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* ---- Validate magic ---- */
    {
        char magic[4];
        if (fread(magic, 1, 4, fp) < 4 || memcmp(magic, "P2DT", 4) != 0) {
            fclose(fp);
            return NULL;
        }
    }

    /* ---- Read 124-byte header (field-by-field, big-endian) ----
     * Layout:
     *   magic[4]  name[18]  subtitle[8]  crlf0[2]  title[30]
     *   crlf1[2]  saver[30]  crlf2[2]  eof[1]  reserve0[1]
     *   flag(2)   no(2)     time(4)     size(4)
     *   depth(2)  x_aspect(2)  y_aspect(2)  x_max(2)  y_max(2)
     *   reserve1(4)
     * Total = 124 bytes
     */

    /* Skip string fields: 4+18+8+2+30+2+30+2+1+1 = 98 bytes */
    fseek(fp, 98, SEEK_SET);

    s.hdr_flag  = (short)pic2_read_be16(fp);   /* flag at 98 */
    /* no */            pic2_read_be16(fp);      /* no at 100 */
    /* time */          pic2_read_be32(fp);      /* time at 102 */
    s.hdr_size  = pic2_read_be32(fp);           /* size at 106 */
    s.hdr_depth = (short)pic2_read_be16(fp);    /* depth at 110 */
    /* x_aspect */      pic2_read_be16(fp);      /* x_aspect at 112 */
    /* y_aspect */      pic2_read_be16(fp);      /* y_aspect at 114 */
    s.hdr_x_max = (short)pic2_read_be16(fp);    /* x_max at 116 */
    s.hdr_y_max = (short)pic2_read_be16(fp);    /* y_max at 118 */
    /* reserve1 */      pic2_read_be32(fp);      /* reserve1 at 120 */

    /* ---- Read palette if present (flag bit 0) ---- */
    if (s.hdr_flag & 1) {
        s.pal_bits = fgetc(fp);
        s.n_pal    = pic2_read_be16(fp);
        if (s.n_pal > 256) s.n_pal = 256;
        for (i = 0; i < s.n_pal; i++) {
            s.pal[i][0] = (pic2_byte)fgetc(fp);
            s.pal[i][1] = (pic2_byte)fgetc(fp);
            s.pal[i][2] = (pic2_byte)fgetc(fp);
        }
    }

    /* Block data starts at hdr_size */
    s.data_pos = s.hdr_size;
    s.x_max = s.hdr_x_max;
    s.y_max = s.hdr_y_max;

    /* ---- Find first valid image block ---- */
    loader_type = -1;
    {
        int r = pic2_find_block(&s);
        if (r == 2) {
            /* Skip unknown blocks, advance with pic2_next_block */
            while (r == 2)
                r = pic2_next_block(&s);
        }
        if (r == 1) {
            for (i = 0; i < 4; i++) {
                if (memcmp(s.block_id, form_ids[i], 4) == 0) {
                    loader_type = i;
                    break;
                }
            }
        }
    }

    if (loader_type < 0) {
        fclose(fp);
        return NULL;
    }

    pw = s.x_max;
    ph = s.y_max;
    if (pw <= 0 || ph <= 0) {
        fclose(fp);
        return NULL;
    }

    total_pixels = pw * ph;
    rgb_buffer = (unsigned char *)calloc((size_t)total_pixels * 3, 1);
    if (!rgb_buffer) {
        fclose(fp);
        return NULL;
    }

    /* ---- Decode all blocks ---- */
    do {
        pic2_alloc_buffer(&s);

        switch (loader_type) {
        case 0: pic2_arith_loader_init(&s); break;
        case 1: pic2_fast_loader_init(&s); break;
        case 2: /* fall through */
        case 3: pic2_beta_loader_init(&s); break;
        }

        s.ynow = 0;

        for (;;) {
            if (loader_type == 0)
                line = pic2_arith_expand_line(&s, &linep);
            else if (loader_type == 1)
                line = pic2_fast_expand_line(&s, &linep);
            else
                line = pic2_beta_expand_line(&s, &linep);

            if (line < 0) break;

            /* Convert packed pixel to 24bpp RGB */
            {
                int y = line + s.y_offset;
                int base = y * pw + s.x_offset;

                colbits = s.hdr_depth / 3;
                colmask = 0xff >> (8 - colbits);

                for (i = 0; i < s.x_wid; i++) {
                    pic2_pixel px = linep[i];
                    int dst = base + i;
                    if (dst < 0 || dst >= total_pixels) continue;
                    if ((s.block_flag & 1) && px == s.block_opaque) continue;
                    {
                        unsigned char r, g, b;
                        r = (unsigned char)((px >> (colbits * 2)) & colmask);
                        g = (unsigned char)((px >> colbits) & colmask);
                        b = (unsigned char)(px & colmask);
                        r = pic2_convert_color_bits(r, colbits, 8);
                        g = pic2_convert_color_bits(g, colbits, 8);
                        b = pic2_convert_color_bits(b, colbits, 8);
                        rgb_buffer[dst * 3 + 0] = r;
                        rgb_buffer[dst * 3 + 1] = g;
                        rgb_buffer[dst * 3 + 2] = b;
                    }
                }
            }
        }

        pic2_free_buffer(&s);

        /* Find next block */
        for (;;) {
            int r = pic2_next_block(&s);
            if (r <= 0) goto done_blocks;
            if (r == 2) continue;
            loader_type = -1;
            for (i = 0; i < 4; i++) {
                if (memcmp(s.block_id, form_ids[i], 4) == 0) {
                    loader_type = i;
                    break;
                }
            }
            if (loader_type >= 0) break;
        }
    } while (1);

done_blocks:
    fclose(fp);
    *out_w = pw;
    *out_h = ph;
    return rgb_buffer;
}

#endif /* STB_PIC2_IMPLEMENTATION */
