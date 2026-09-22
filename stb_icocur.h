/* stb_icocur.h - v1.0 - Microsoft Windows ICO/CUR decoder in stb_image style.
 *
 * Written in C89. No dependencies. Single header file.
 *
 * For documentation see the block comment below headed "DOCUMENTATION".
 * For license see the block comment below headed "LICENSE".
 *
 * USAGE:
 *
 *   In ONE C file do:
 *
 *       #define STB_ICOCUR_IMPLEMENTATION
 *       #include "stb_icocur.h"
 *
 *   In all other files just:
 *
 *       #include "stb_icocur.h"
 *
 *   Optional configuration macros (define before including):
 *
 *       STB_ICOCUR_STATIC        - make all public functions static
 *       STB_ICOCUR_NO_STDIO      - remove all stdio/file functions
 *       STB_ICOCUR_NO_PNG        - remove PNG-compressed entry support hooks
 *       STB_ICOCUR_USE_STB_IMAGE - fall back to stbi_load_from_memory for PNG
 *                                  entries (you must link stb_image as well;
 *                                  see DOCUMENTATION below for details).
 *       STB_ICOCUR_MALLOC(sz)    - custom allocator (default: malloc)
 *       STB_ICOCUR_REALLOC(p,sz) - custom reallocator (default: realloc)
 *       STB_ICOCUR_FREE(p)       - custom deallocator (default: free)
 *
 * DOCUMENTATION:
 *
 *   stb_icocur decodes Windows .ICO (icon) and .CUR (cursor) files.
 *
 *   An ICO/CUR file is a container holding 1 or more images (directory
 *   entries). Each entry is either:
 *
 *     a) a BMP image without a BITMAPFILEHEADER (BITMAPINFOHEADER or
 *        BITMAPCOREHEADER plus palette plus XOR mask plus AND mask), or
 *     b) a complete PNG image (Vista and later, typically for 256x256).
 *
 *   This header decodes case (a) natively, including 1/4/8-bit paletted,
 *   16/24/32-bit, BI_RGB, BI_RLE8, BI_RLE4, BI_BITFIELDS, top-down
 *   (negative height) BMPs, and the 1-bit AND transparency mask. 32-bit
 *   images with meaningful alpha ignore the AND mask; all other images
 *   apply the AND mask to produce alpha.
 *
 *   Case (b) (PNG) cannot be decoded without a PNG decoder. By default
 *   PNG entries are recognized for counting/query but loading them fails
 *   with a clear error message. To enable PNG decoding either:
 *
 *     1) #define STB_ICOCUR_USE_STB_IMAGE and link stb_image (stb_image.h
 *        must be available; its stbi_load_from_memory is used), or
 *     2) install your own decoder with stb_icocur_set_png_decoder(),
 *        e.g.:
 *
 *          stb_icocur_set_png_decoder(
 *            (stb_icocur_png_decoder_func) stbi_load_from_memory);
 *
 *        after including stb_image.h, or
 *     3) use stb_icocur_query() / stb_icocur_is_png() to detect PNG
 *        entries and decode them externally.
 *
 *   QUICK START (single best image, like stbi_load):
 *
 *       int x, y, comp;
 *       unsigned char *pixels = stb_icocur_load("icon.ico", &x, &y, &comp, 4);
 *       if (!pixels) { printf("%s\n", stb_icocur_failure_reason()); }
 *       else { ... use x*y*4 bytes ...; stb_icocur_free(pixels); }
 *
 *   The "best" image is the largest (width*height, breaking ties with
 *   higher bit depth). stb_icocur_load() tries entries from best to
 *   worst until one decodes, so a PNG largest entry without a PNG
 *   decoder falls back to the best BMP entry.
 *
 *   ENUMERATING ENTRIES:
 *
 *       int n = stb_icocur_count("icon.ico");
 *       int i;
 *       for (i = 0; i < n; ++i) {
 *           int x, y, comp, bpp, is_png;
 *           stb_icocur_query("icon.ico", i, &x, &y, &comp, &bpp, &is_png);
 *           ... stb_icocur_load_index("icon.ico", i, ...) ...
 *       }
 *
 *   CUR HOTSPOTS:
 *
 *       int xhot, yhot;
 *       if (stb_icocur_hotspot("cursor.cur", 0, &xhot, &yhot)) { ... }
 *       (returns 0 for .ICO files, which have no hotspot).
 *
 *   OUTPUT FORMAT:
 *
 *     Decoded pixels are 8-bit per channel, top-down (first row is the
 *     top row), non-premultiplied. By default 4 channels (RGBA) are
 *     produced; pass req_comp 1..4 to convert (1=Y, 2=YA, 3=RGB, 4=RGBA),
 *     using the same conversion rules as stb_image. channels-in-file
 *     (*comp) is 4 for BMP entries and the native PNG channel count
 *     (1..4) for PNG entries.
 *
 *     Use stb_icocur_set_flip_vertically_on_load(1) to flip output
 *     vertically (applies to BMP and PNG paths).
 *
 *   FUNCTION LIST (memory / file / callback variants where sensible):
 *
 *     const char *stb_icocur_failure_reason(void);
 *     void stb_icocur_set_flip_vertically_on_load(int flag);
 *     void stb_icocur_free(void *ptr);
 *     void stb_icocur_set_png_decoder(stb_icocur_png_decoder_func f);
 *
 *     int stb_icocur_is_ico_from_memory(...);   int stb_icocur_is_ico(...);
 *     int stb_icocur_is_cur_from_memory(...);   int stb_icocur_is_cur(...);
 *     int stb_icocur_type_from_memory(...);     int stb_icocur_type(...);
 *         (type returns 1=ICO, 2=CUR, 0=not an ICO/CUR file)
 *     int stb_icocur_count_from_memory(...);    int stb_icocur_count(...);
 *     int stb_icocur_info_from_memory(...);     int stb_icocur_info(...);
 *         (info describes the image stb_icocur_load would return)
 *     int stb_icocur_query_from_memory(...);    int stb_icocur_query(...);
 *         (query of entry index; any out pointer may be NULL)
 *     int stb_icocur_hotspot_from_memory(...);  int stb_icocur_hotspot(...);
 *     int stb_icocur_is_png_from_memory(...);   int stb_icocur_is_png(...);
 *         (1=PNG entry, 0=BMP entry, -1=error)
 *
 *     unsigned char *stb_icocur_load_from_memory(...);
 *     unsigned char *stb_icocur_load(...);
 *     unsigned char *stb_icocur_load_index_from_memory(...);
 *     unsigned char *stb_icocur_load_index(...);
 *
 *     ..._from_callbacks() variants exist for every call above that
 *     takes a filename or memory buffer (unless STB_ICOCUR_NO_STDIO
 *     handling differs; callbacks never need stdio).
 *
 * LICENSE:
 *
 *   This software is available under the MIT license (same choice as
 *   stb_image). See end of file for full license text.
 */

#ifndef STB_ICOCUR_H_INCLUDED
#define STB_ICOCUR_H_INCLUDED

/* -------------------------------------------------------------------- */
/* configuration                                                         */
/* -------------------------------------------------------------------- */

#ifndef STBICOCURDEF
#ifdef STB_ICOCUR_STATIC
#define STBICOCURDEF static
#else
#define STBICOCURDEF extern
#endif
#endif

#ifndef STB_ICOCUR_NO_STDIO
#include <stdio.h>
#endif

typedef unsigned char stb_icocur_uc;

/* PNG decoder hook. Must follow stb_image convention:
 *   - *x, *y, *comp receive width, height, channels-in-file,
 *   - return value is malloced pixels with req_comp channels if
 *     req_comp != 0, else *comp channels,
 *   - return NULL on failure.
 */
typedef stb_icocur_uc *(*stb_icocur_png_decoder_func)(
    stb_icocur_uc const *buffer, int len,
    int *x, int *y, int *comp, int req_comp);

/* io callbacks, identical in spirit to stbi_io_callbacks */
typedef struct stb_icocur_callbacks stb_icocur_callbacks;
struct stb_icocur_callbacks
{
    int  (*read)(void *user, char *data, int size);
    void (*skip)(void *user, int n);
    int  (*eof)(void *user);
};

/* -------------------------------------------------------------------- */
/* public API                                                            */
/* -------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

STBICOCURDEF char const *stb_icocur_failure_reason(void);
STBICOCURDEF void stb_icocur_set_flip_vertically_on_load(int flag);
STBICOCURDEF void stb_icocur_free(void *ptr);
#ifndef STB_ICOCUR_NO_PNG
STBICOCURDEF void stb_icocur_set_png_decoder(stb_icocur_png_decoder_func f);
#endif

STBICOCURDEF int stb_icocur_is_ico_from_memory(stb_icocur_uc const *buffer, int len);
STBICOCURDEF int stb_icocur_is_cur_from_memory(stb_icocur_uc const *buffer, int len);
STBICOCURDEF int stb_icocur_type_from_memory(stb_icocur_uc const *buffer, int len);
STBICOCURDEF int stb_icocur_count_from_memory(stb_icocur_uc const *buffer, int len);
STBICOCURDEF int stb_icocur_info_from_memory(stb_icocur_uc const *buffer, int len, int *x, int *y, int *comp);
STBICOCURDEF int stb_icocur_query_from_memory(stb_icocur_uc const *buffer, int len, int index,
    int *x, int *y, int *comp, int *bpp, int *is_png);
STBICOCURDEF int stb_icocur_hotspot_from_memory(stb_icocur_uc const *buffer, int len, int index,
    int *xhot, int *yhot);
STBICOCURDEF int stb_icocur_is_png_from_memory(stb_icocur_uc const *buffer, int len, int index);
STBICOCURDEF stb_icocur_uc *stb_icocur_load_from_memory(stb_icocur_uc const *buffer, int len,
    int *x, int *y, int *comp, int req_comp);
STBICOCURDEF stb_icocur_uc *stb_icocur_load_index_from_memory(stb_icocur_uc const *buffer, int len,
    int index, int *x, int *y, int *comp, int req_comp);

STBICOCURDEF int stb_icocur_is_ico_from_callbacks(stb_icocur_callbacks const *c, void *user);
STBICOCURDEF int stb_icocur_is_cur_from_callbacks(stb_icocur_callbacks const *c, void *user);
STBICOCURDEF int stb_icocur_type_from_callbacks(stb_icocur_callbacks const *c, void *user);
STBICOCURDEF int stb_icocur_count_from_callbacks(stb_icocur_callbacks const *c, void *user);
STBICOCURDEF int stb_icocur_info_from_callbacks(stb_icocur_callbacks const *c, void *user,
    int *x, int *y, int *comp);
STBICOCURDEF int stb_icocur_query_from_callbacks(stb_icocur_callbacks const *c, void *user,
    int index, int *x, int *y, int *comp, int *bpp, int *is_png);
STBICOCURDEF int stb_icocur_hotspot_from_callbacks(stb_icocur_callbacks const *c, void *user,
    int index, int *xhot, int *yhot);
STBICOCURDEF int stb_icocur_is_png_from_callbacks(stb_icocur_callbacks const *c, void *user, int index);
STBICOCURDEF stb_icocur_uc *stb_icocur_load_from_callbacks(stb_icocur_callbacks const *c, void *user,
    int *x, int *y, int *comp, int req_comp);
STBICOCURDEF stb_icocur_uc *stb_icocur_load_index_from_callbacks(stb_icocur_callbacks const *c, void *user,
    int index, int *x, int *y, int *comp, int req_comp);

#ifndef STB_ICOCUR_NO_STDIO
STBICOCURDEF int stb_icocur_is_ico(char const *filename);
STBICOCURDEF int stb_icocur_is_cur(char const *filename);
STBICOCURDEF int stb_icocur_type(char const *filename);
STBICOCURDEF int stb_icocur_count(char const *filename);
STBICOCURDEF int stb_icocur_info(char const *filename, int *x, int *y, int *comp);
STBICOCURDEF int stb_icocur_query(char const *filename, int index,
    int *x, int *y, int *comp, int *bpp, int *is_png);
STBICOCURDEF int stb_icocur_hotspot(char const *filename, int index, int *xhot, int *yhot);
STBICOCURDEF int stb_icocur_is_png(char const *filename, int index);
STBICOCURDEF stb_icocur_uc *stb_icocur_load(char const *filename,
    int *x, int *y, int *comp, int req_comp);
STBICOCURDEF stb_icocur_uc *stb_icocur_load_index(char const *filename, int index,
    int *x, int *y, int *comp, int req_comp);
#endif

#ifdef __cplusplus
}
#endif

#endif /* STB_ICOCUR_H_INCLUDED */

/* -------------------------------------------------------------------- */
/* implementation                                                        */
/* -------------------------------------------------------------------- */
#ifdef STB_ICOCUR_IMPLEMENTATION

#ifndef STB_ICOCUR_MALLOC
#include <stdlib.h>
#define STB_ICOCUR_MALLOC(sz) malloc(sz)
#endif
#ifndef STB_ICOCUR_REALLOC
#include <stdlib.h>
#define STB_ICOCUR_REALLOC(p,sz) realloc(p,sz)
#endif
#ifndef STB_ICOCUR_FREE
#include <stdlib.h>
#define STB_ICOCUR_FREE(p) free(p)
#endif
#include <string.h>

#ifdef STB_ICOCUR_USE_STB_IMAGE
/* Fallback PNG decoder via stb_image. The user must compile stb_image
 * as well. We only declare the symbol here; linking resolves it. */
extern stb_icocur_uc *stbi_load_from_memory(stb_icocur_uc const *buffer, int len,
    int *x, int *y, int *comp, int req_comp);
#endif

/* failure state */
static char const *stb_icocur__failure_reason = "no error";
static int stb_icocur__flip_vertically = 0;
#ifndef STB_ICOCUR_NO_PNG
static stb_icocur_png_decoder_func stb_icocur__png_decoder = 0;
#endif

static void stb_icocur__set_error(char const *reason)
{
    stb_icocur__failure_reason = reason;
}

/* ---------------- little endian helpers ---------------- */

static unsigned int stb_icocur__ru16(stb_icocur_uc const *p)
{
    return ((unsigned int) p[0]) | (((unsigned int) p[1]) << 8);
}

static unsigned int stb_icocur__ru32(stb_icocur_uc const *p)
{
    return ((unsigned int) p[0])
         | (((unsigned int) p[1]) << 8)
         | (((unsigned int) p[2]) << 16)
         | (((unsigned int) p[3]) << 24);
}

static int stb_icocur__ri32(stb_icocur_uc const *p)
{
    return (int) stb_icocur__ru32(p);
}

static unsigned int stb_icocur__ru32be(stb_icocur_uc const *p)
{
    return (((unsigned int) p[0]) << 24)
         | (((unsigned int) p[1]) << 16)
         | (((unsigned int) p[2]) << 8)
         | ((unsigned int) p[3]);
}

/* ---------------- directory parsing ---------------- */

typedef struct stb_icocur__direntry stb_icocur__direntry;
struct stb_icocur__direntry
{
    int width;        /* 1..256 */
    int height;       /* 1..256 */
    int bytes_in_res;
    int image_offset;
    int hotspot_x;    /* CUR only */
    int hotspot_y;    /* CUR only */
};

#define STB_ICOCUR__MAX_ENTRIES 1024
#define STB_ICOCUR__MAX_DIM 8192
#define STB_ICOCUR__MAX_PIXELS (16u*1024u*1024u)

static int stb_icocur__parse_dir(stb_icocur_uc const *b, int len,
    int *ptype, stb_icocur__direntry **pentries, int *pcount)
{
    int type, count, i;
    stb_icocur__direntry *entries;
    if (b == 0 || len < 6) {
        stb_icocur__set_error("not an ICO/CUR file (too short)");
        return 0;
    }
    if (b[0] != 0 || b[1] != 0) {
        stb_icocur__set_error("not an ICO/CUR file (bad reserved)");
        return 0;
    }
    type = (int) stb_icocur__ru16(b + 2);
    if (type != 1 && type != 2) {
        stb_icocur__set_error("not an ICO/CUR file (bad type)");
        return 0;
    }
    count = (int) stb_icocur__ru16(b + 4);
    if (count <= 0 || count > STB_ICOCUR__MAX_ENTRIES) {
        stb_icocur__set_error("bad ICO/CUR entry count");
        return 0;
    }
    if (len < 6 + 16 * count) {
        stb_icocur__set_error("truncated ICO/CUR directory");
        return 0;
    }
    entries = (stb_icocur__direntry *) STB_ICOCUR_MALLOC(sizeof(*entries) * (size_t) count);
    if (!entries) {
        stb_icocur__set_error("out of memory");
        return 0;
    }
    for (i = 0; i < count; ++i) {
        stb_icocur_uc const *e = b + 6 + i * 16;
        int w, h, bytes, off;
        w = e[0] == 0 ? 256 : e[0];
        h = e[1] == 0 ? 256 : e[1];
        bytes = (int) stb_icocur__ru32(e + 8);
        off = (int) stb_icocur__ru32(e + 12);
        entries[i].width = w;
        entries[i].height = h;
        entries[i].bytes_in_res = bytes;
        entries[i].image_offset = off;
        entries[i].hotspot_x = (int) stb_icocur__ru16(e + 4);
        entries[i].hotspot_y = (int) stb_icocur__ru16(e + 6);
        if (bytes <= 0 || off < 0 || off >= len || bytes > len || off + bytes > len || off + bytes < off) {
            STB_ICOCUR_FREE(entries);
            stb_icocur__set_error("bad ICO/CUR entry offset/size");
            return 0;
        }
        if (w <= 0 || h <= 0 || w > STB_ICOCUR__MAX_DIM || h > STB_ICOCUR__MAX_DIM) {
            STB_ICOCUR_FREE(entries);
            stb_icocur__set_error("bad ICO/CUR entry dimensions");
            return 0;
        }
    }
    *ptype = type;
    *pentries = entries;
    *pcount = count;
    return 1;
}

/* ---------------- PNG helpers ---------------- */

static int stb_icocur__is_png_sig(stb_icocur_uc const *d, int n)
{
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    int i;
    if (n < 8) return 0;
    for (i = 0; i < 8; ++i) if (d[i] != sig[i]) return 0;
    return 1;
}

/* Parse PNG IHDR for dimensions. Returns 1 on success. */
static int stb_icocur__png_info(stb_icocur_uc const *d, int n,
    int *px, int *py, int *pcomp, int *pbpp)
{
    unsigned int w, h;
    int bitdepth, colortype, channels;
    if (!stb_icocur__is_png_sig(d, n)) return 0;
    /* need IHDR: 8 sig + 4 len + 4 "IHDR" + 13 data + 4 crc = 33 */
    if (n < 33) {
        stb_icocur__set_error("truncated PNG entry");
        return 0;
    }
    if (stb_icocur__ru32be(d + 8) != 13) {
        stb_icocur__set_error("bad PNG IHDR length");
        return 0;
    }
    if (d[12] != 'I' || d[13] != 'H' || d[14] != 'D' || d[15] != 'R') {
        stb_icocur__set_error("bad PNG (missing IHDR)");
        return 0;
    }
    w = stb_icocur__ru32be(d + 16);
    h = stb_icocur__ru32be(d + 20);
    bitdepth = d[24];
    colortype = d[25];
    if (w == 0 || h == 0 || w > 16384 || h > 16384) {
        stb_icocur__set_error("bad PNG dimensions");
        return 0;
    }
    switch (colortype) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 3; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default:
        stb_icocur__set_error("bad PNG color type");
        return 0;
    }
    if (!(bitdepth == 1 || bitdepth == 2 || bitdepth == 4 ||
          bitdepth == 8 || bitdepth == 16)) {
        stb_icocur__set_error("bad PNG bit depth");
        return 0;
    }
    if (colortype == 3 && bitdepth > 8) {
        stb_icocur__set_error("bad PNG palette depth");
        return 0;
    }
    if ((colortype == 2 || colortype == 4 || colortype == 6) && bitdepth != 8 && bitdepth != 16) {
        stb_icocur__set_error("bad PNG bit depth");
        return 0;
    }
    if (px) *px = (int) w;
    if (py) *py = (int) h;
    if (pcomp) *pcomp = channels;
    if (pbpp) {
        if (colortype == 3) *pbpp = bitdepth;
        else *pbpp = bitdepth * channels;
    }
    return 1;
}

/* ---------------- BMP header parsing ---------------- */

typedef struct stb_icocur__bmpinfo stb_icocur__bmpinfo;
struct stb_icocur__bmpinfo
{
    int width;
    int height;          /* xor height, always > 0 */
    int top_down;        /* 1 if negative biHeight */
    int has_and;         /* 1 if AND mask present */
    int bpp;
    int compression;     /* 0..3 */
    int palette_count;
    int palette_offset;  /* within entry */
    int palette_size;    /* 3 or 4 */
    int header_size;
    int xor_offset;      /* within entry */
    int xor_size;        /* for non-RLE; 0 for RLE */
    int and_offset;      /* within entry */
    int and_size;
    unsigned int red_mask;
    unsigned int green_mask;
    unsigned int blue_mask;
    unsigned int alpha_mask;
    int has_alpha_mask;
};

static void stb_icocur__mask_info(unsigned int mask, int *pshift, int *pbits)
{
    int shift = 0, bits = 0;
    if (mask == 0) { *pshift = 0; *pbits = 0; return; }
    while (((mask >> shift) & 1u) == 0u) ++shift;
    while (((mask >> shift) >> bits) & 1u) ++bits;
    *pshift = shift;
    *pbits = bits;
}

static unsigned char stb_icocur__scale_bits(unsigned int v, int bits)
{
    unsigned int max;
    if (bits <= 0) return 0;
    if (bits >= 8) return (unsigned char) (v >> (bits - 8));
    max = (1u << bits) - 1u;
    if (max == 0) return 0;
    return (unsigned char) ((v * 255u + (max >> 1)) / max);
}

static int stb_icocur__parse_bmp(stb_icocur_uc const *d, int n,
    int dir_w, int dir_h, stb_icocur__bmpinfo *bi)
{
    unsigned int hdrsize;
    int bw, bh_signed, bh_abs, top_down;
    int planes, bpp, comp;
    int pal_count, pal_off, pal_size, xor_off;
    unsigned int clr_used;
    /* silence unused warnings when called with same dir dims */
    (void) dir_w;

    if (n < 4) {
        stb_icocur__set_error("truncated BMP entry");
        return 0;
    }
    hdrsize = stb_icocur__ru32(d);
    if (hdrsize == 12) {
        /* BITMAPCOREHEADER */
        int cw, ch;
        if (n < 12) {
            stb_icocur__set_error("truncated BMP core header");
            return 0;
        }
        cw = (int) stb_icocur__ru16(d + 4);
        ch = (int) stb_icocur__ru16(d + 6);
        planes = (int) stb_icocur__ru16(d + 8);
        bpp = (int) stb_icocur__ru16(d + 10);
        if (cw <= 0 || ch == 0 || cw > STB_ICOCUR__MAX_DIM) {
            stb_icocur__set_error("bad BMP core dimensions");
            return 0;
        }
        top_down = ch < 0 ? 1 : 0;
        bh_abs = ch < 0 ? -ch : ch;
        if (bh_abs > STB_ICOCUR__MAX_DIM) {
            stb_icocur__set_error("bad BMP core dimensions");
            return 0;
        }
        if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24) {
            stb_icocur__set_error("unsupported BMP core bit depth");
            return 0;
        }
        if (bpp <= 8) {
            pal_count = 1 << bpp;
            pal_size = 3;
            pal_off = 12;
            xor_off = 12 + pal_count * 3;
        } else {
            pal_count = 0;
            pal_size = 3;
            pal_off = 12;
            xor_off = 12;
        }
        bi->width = cw;
        bi->compression = 0;
        bi->palette_count = pal_count;
        bi->palette_offset = pal_off;
        bi->palette_size = pal_size;
        bi->header_size = 12;
        bi->xor_offset = xor_off;
        bi->bpp = bpp;
        bi->top_down = top_down;
        bi->red_mask = 0;
        bi->green_mask = 0;
        bi->blue_mask = 0;
        bi->alpha_mask = 0;
        bi->has_alpha_mask = 0;
        /* height / AND detection shared below */
        bw = cw;
        bh_signed = ch;
        comp = 0;
        clr_used = 0;
        (void) planes; (void) comp; (void) clr_used;
    } else if (hdrsize >= 40 && hdrsize <= 256) {
        int bi_w, bi_h;
        if (n < 40) {
            stb_icocur__set_error("truncated BMP info header");
            return 0;
        }
        if ((int) hdrsize > n) {
            stb_icocur__set_error("truncated BMP header");
            return 0;
        }
        bi_w = stb_icocur__ri32(d + 4);
        bi_h = stb_icocur__ri32(d + 8);
        planes = (int) stb_icocur__ru16(d + 12);
        bpp = (int) stb_icocur__ru16(d + 14);
        comp = (int) stb_icocur__ru32(d + 16);
        clr_used = stb_icocur__ru32(d + 32);
        if (bi_w <= 0 || bi_h == 0 || bi_w > STB_ICOCUR__MAX_DIM) {
            stb_icocur__set_error("bad BMP dimensions");
            return 0;
        }
        top_down = bi_h < 0 ? 1 : 0;
        bh_abs = bi_h < 0 ? -bi_h : bi_h;
        if (bh_abs > STB_ICOCUR__MAX_DIM * 2) {
            stb_icocur__set_error("bad BMP dimensions");
            return 0;
        }
        if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) {
            stb_icocur__set_error("unsupported BMP bit depth");
            return 0;
        }
        if (comp != 0 && comp != 1 && comp != 2 && comp != 3) {
            stb_icocur__set_error("unsupported BMP compression");
            return 0;
        }
        if (comp == 1 && bpp != 8) {
            stb_icocur__set_error("bad RLE8 bit depth");
            return 0;
        }
        if (comp == 2 && bpp != 4) {
            stb_icocur__set_error("bad RLE4 bit depth");
            return 0;
        }
        if (comp == 3 && bpp != 16 && bpp != 32) {
            stb_icocur__set_error("bad BITFIELDS bit depth");
            return 0;
        }
        if (bpp <= 8) {
            if (clr_used > 0) {
                if (clr_used > (unsigned)(1 << bpp)) clr_used = (unsigned)(1 << bpp);
                pal_count = (int) clr_used;
            } else {
                pal_count = 1 << bpp;
            }
        } else {
            pal_count = 0;
            if (clr_used != 0 && hdrsize == 40) {
                /* tolerated: some writers set clrUsed with 24/32bpp; ignore */
            }
        }
        pal_size = 4;
        pal_off = (int) hdrsize;
        xor_off = (int) hdrsize + pal_count * 4;
        bi->red_mask = 0;
        bi->green_mask = 0;
        bi->blue_mask = 0;
        bi->alpha_mask = 0;
        bi->has_alpha_mask = 0;
        if (comp == 3) {
            if (hdrsize == 40) {
                if (n < 52) {
                    stb_icocur__set_error("truncated BITFIELDS masks");
                    return 0;
                }
                bi->red_mask = stb_icocur__ru32(d + 40);
                bi->green_mask = stb_icocur__ru32(d + 44);
                bi->blue_mask = stb_icocur__ru32(d + 48);
                xor_off = 52 + pal_count * 4;
                pal_off = 52;
            } else if (hdrsize >= 52) {
                bi->red_mask = stb_icocur__ru32(d + 40);
                bi->green_mask = stb_icocur__ru32(d + 44);
                bi->blue_mask = stb_icocur__ru32(d + 48);
            }
            if (hdrsize >= 56) {
                bi->alpha_mask = stb_icocur__ru32(d + 52);
                bi->has_alpha_mask = bi->alpha_mask != 0;
            }
        }
        if (bi->red_mask == 0 && bi->green_mask == 0 && bi->blue_mask == 0) {
            /* keep zero; decoder applies defaults */
        }
        bi->width = bi_w;
        bi->compression = comp;
        bi->palette_count = pal_count;
        bi->palette_offset = pal_off;
        bi->palette_size = pal_size;
        bi->header_size = (int) hdrsize;
        bi->xor_offset = xor_off;
        bi->bpp = bpp;
        bi->top_down = top_down;
        bw = bi_w;
        bh_signed = bi_h;
        (void) planes;
    } else {
        stb_icocur__set_error("unsupported BMP header size");
        return 0;
    }

    /* palette bounds */
    if (bi->palette_count > 0) {
        long need = (long) bi->palette_offset + (long) bi->palette_count * (long) bi->palette_size;
        if (need > n || need < 0 || bi->palette_offset < 0) {
            stb_icocur__set_error("truncated BMP palette");
            return 0;
        }
    }
    if (bi->xor_offset < 0 || bi->xor_offset > n) {
        stb_icocur__set_error("truncated BMP data");
        return 0;
    }

    /* height / AND-mask detection */
    {
        int header_h = bh_abs;
        int h2 = (header_h % 2 == 0) ? header_h / 2 : -1;
        int h = -1, has_and = 0;
        int row_bytes_h2 = 0, and_size_h2 = 0;
        int row_bytes_h = 0;
        (void) bw; (void) bh_signed;

        if (bi->compression == 1 || bi->compression == 2) {
            /* RLE: rely on directory height */
            if (h2 > 0 && h2 == dir_h) { h = h2; has_and = 1; }
            else if (header_h == dir_h) { h = header_h; has_and = 0; }
            else if (h2 > 0) { h = h2; has_and = 1; }
            else { h = header_h; has_and = 0; }
            bi->height = h;
            bi->has_and = has_and;
            bi->xor_size = 0;
            bi->and_offset = 0;
            bi->and_size = 0;
        } else {
            /* non-RLE: prefer directory match, verify fit */
            if (h2 > 0 && h2 == dir_h) {
                row_bytes_h2 = ((bi->bpp * bi->width + 31) / 32) * 4;
                and_size_h2 = ((bi->width + 31) / 32) * 4 * h2;
                h = h2;
                if (bi->xor_offset + row_bytes_h2 * h2 + and_size_h2 <= n) has_and = 1;
                else if (bi->xor_offset + row_bytes_h2 * h2 <= n) has_and = 0;
                else {
                    stb_icocur__set_error("truncated BMP pixel data");
                    return 0;
                }
            } else if (header_h == dir_h) {
                row_bytes_h = ((bi->bpp * bi->width + 31) / 32) * 4;
                if (bi->xor_offset + row_bytes_h * header_h > n) {
                    stb_icocur__set_error("truncated BMP pixel data");
                    return 0;
                }
                h = header_h;
                has_and = 0;
            } else {
                /* broken directory; try double then single by fit */
                int fit_double = 0, fit_single = 0;
                if (h2 > 0) {
                    row_bytes_h2 = ((bi->bpp * bi->width + 31) / 32) * 4;
                    and_size_h2 = ((bi->width + 31) / 32) * 4 * h2;
                    if (bi->xor_offset + row_bytes_h2 * h2 <= n) fit_double = 1;
                }
                {
                    int rb = ((bi->bpp * bi->width + 31) / 32) * 4;
                    if (bi->xor_offset + rb * header_h <= n) fit_single = 1;
                    row_bytes_h = rb;
                }
                if (fit_double && h2 == dir_h) {
                    h = h2;
                    has_and = (bi->xor_offset + row_bytes_h2 * h2 + and_size_h2 <= n) ? 1 : 0;
                } else if (fit_single && header_h == dir_h) {
                    h = header_h; has_and = 0;
                } else if (fit_double) {
                    h = h2;
                    has_and = (bi->xor_offset + row_bytes_h2 * h2 + and_size_h2 <= n) ? 1 : 0;
                    /* prefer single if double lacks full data but single fits? */
                    if (!has_and && fit_single && header_h <= STB_ICOCUR__MAX_DIM) {
                        /* ambiguous; keep double without AND only if dir suggests it */
                        if (dir_h != h2) { h = header_h; has_and = 0; }
                    }
                } else if (fit_single) {
                    h = header_h; has_and = 0;
                } else {
                    stb_icocur__set_error("truncated BMP pixel data");
                    return 0;
                }
            }
            if (h <= 0 || h > STB_ICOCUR__MAX_DIM || bi->width > STB_ICOCUR__MAX_DIM) {
                stb_icocur__set_error("bad BMP dimensions");
                return 0;
            }
            if ((unsigned long) bi->width * (unsigned long) h > STB_ICOCUR__MAX_PIXELS) {
                stb_icocur__set_error("image too large");
                return 0;
            }
            bi->height = h;
            bi->has_and = has_and;
            if (bi->compression == 0 || bi->compression == 3) {
                {
                    int rb = ((bi->bpp * bi->width + 31) / 32) * 4;
                    bi->xor_size = rb * h;
                }
                if (has_and) {
                    bi->and_size = ((bi->width + 31) / 32) * 4 * h;
                    bi->and_offset = bi->xor_offset + bi->xor_size;
                    if (bi->and_offset + bi->and_size > n) {
                        /* truncated AND mask: treat as opaque */
                        bi->has_and = 0;
                        bi->and_size = 0;
                    }
                } else {
                    bi->and_size = 0;
                    bi->and_offset = bi->xor_offset + bi->xor_size;
                }
                if (bi->xor_offset + bi->xor_size > n) {
                    stb_icocur__set_error("truncated BMP pixel data");
                    return 0;
                }
            } else {
                bi->xor_size = 0;
                bi->and_size = 0;
                bi->and_offset = 0;
            }
        }
    }
    return 1;
}

/* ---------------- pixel decoding ---------------- */

static void stb_icocur__flip_rows(stb_icocur_uc *img, int w, int h, int comp)
{
    int y, x;
    stb_icocur_uc tmp[4];
    for (y = 0; y < h / 2; ++y) {
        stb_icocur_uc *r0 = img + (size_t) y * (size_t) w * (size_t) comp;
        stb_icocur_uc *r1 = img + (size_t) (h - 1 - y) * (size_t) w * (size_t) comp;
        for (x = 0; x < w; ++x) {
            int c;
            for (c = 0; c < comp; ++c) tmp[c] = r0[x * comp + c];
            for (c = 0; c < comp; ++c) r0[x * comp + c] = r1[x * comp + c];
            for (c = 0; c < comp; ++c) r1[x * comp + c] = tmp[c];
        }
    }
}

static stb_icocur_uc *stb_icocur__convert(stb_icocur_uc *rgba, int w, int h, int req_comp)
{
    size_t count;
    stb_icocur_uc *out;
    size_t i, n;
    if (req_comp == 4 || req_comp == 0) return rgba;
    count = (size_t) w * (size_t) h;
    out = (stb_icocur_uc *) STB_ICOCUR_MALLOC(count * (size_t) req_comp);
    if (!out) {
        STB_ICOCUR_FREE(rgba);
        stb_icocur__set_error("out of memory");
        return 0;
    }
    n = count;
    for (i = 0; i < n; ++i) {
        unsigned char r = rgba[i * 4 + 0];
        unsigned char g = rgba[i * 4 + 1];
        unsigned char b = rgba[i * 4 + 2];
        unsigned char a = rgba[i * 4 + 3];
        unsigned char yv = (unsigned char) (((r * 77u) + (g * 150u) + (b * 29u)) >> 8);
        if (req_comp == 1) out[i] = yv;
        else if (req_comp == 2) { out[i * 2 + 0] = yv; out[i * 2 + 1] = a; }
        else if (req_comp == 3) { out[i * 3 + 0] = r; out[i * 3 + 1] = g; out[i * 3 + 2] = b; }
    }
    STB_ICOCUR_FREE(rgba);
    return out;
}

/* Decode RLE8 data. Returns bytes consumed, or -1 on error.
 * out must be w*h*4 zeroed or preallocated; decoded pixels set RGB, A=255.
 * pal_r/g/b hold palette.
 */
static int stb_icocur__decode_rle8(stb_icocur_uc const *src, int src_len,
    stb_icocur_uc *out, int w, int h, int top_down,
    stb_icocur_uc const *pal_r, stb_icocur_uc const *pal_g, stb_icocur_uc const *pal_b, int pal_count)
{
    int pos = 0;
    int x = 0;
    int y = top_down ? 0 : h - 1;
    int ydir = top_down ? 1 : -1;
    int done = 0;
    while (!done) {
        int count, val, k;
        if (pos + 2 > src_len) {
            stb_icocur__set_error("truncated RLE8 data");
            return -1;
        }
        count = src[pos++];
        val = src[pos++];
        if (count > 0) {
            /* encoded run */
            if (val < 0 || val >= pal_count) {
                stb_icocur__set_error("bad RLE8 palette index");
                return -1;
            }
            for (k = 0; k < count; ++k) {
                if (x >= w) {
                    stb_icocur__set_error("bad RLE8 run");
                    return -1;
                }
                if (y < 0 || y >= h) {
                    stb_icocur__set_error("bad RLE8 row");
                    return -1;
                }
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 0] = pal_r[val];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 1] = pal_g[val];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 2] = pal_b[val];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = 255;
                ++x;
            }
        } else {
            if (val == 0) {
                x = 0;
                y += ydir;
                if (top_down ? (y >= h) : (y < 0)) {
                    /* allow exactly h rows; further EOL before EOB is tolerable */
                    if (top_down ? (y > h) : (y < -1)) {
                        stb_icocur__set_error("bad RLE8 end of line");
                        return -1;
                    }
                }
            } else if (val == 1) {
                done = 1;
            } else if (val == 2) {
                int dx, dy;
                if (pos + 2 > src_len) {
                    stb_icocur__set_error("truncated RLE8 delta");
                    return -1;
                }
                dx = src[pos++];
                dy = src[pos++];
                x += dx;
                y += ydir * dy;
                if (x < 0 || x > w || y < -1 || y > h) {
                    stb_icocur__set_error("bad RLE8 delta");
                    return -1;
                }
            } else {
                /* absolute mode: val literal bytes */
                int n = val;
                int j;
                if (pos + n > src_len) {
                    stb_icocur__set_error("truncated RLE8 data");
                    return -1;
                }
                for (j = 0; j < n; ++j) {
                    int idx = src[pos++];
                    if (x >= w || y < 0 || y >= h) {
                        stb_icocur__set_error("bad RLE8 absolute run");
                        return -1;
                    }
                    if (idx < 0 || idx >= pal_count) {
                        stb_icocur__set_error("bad RLE8 palette index");
                        return -1;
                    }
                    out[((size_t) y * (size_t) w + (size_t) x) * 4 + 0] = pal_r[idx];
                    out[((size_t) y * (size_t) w + (size_t) x) * 4 + 1] = pal_g[idx];
                    out[((size_t) y * (size_t) w + (size_t) x) * 4 + 2] = pal_b[idx];
                    out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = 255;
                    ++x;
                }
                if ((n & 1) == 1) {
                    if (pos >= src_len) {
                        stb_icocur__set_error("truncated RLE8 data");
                        return -1;
                    }
                    ++pos; /* pad to word */
                }
            }
        }
        /* early exit safety: if we consumed all rows and hit EOB, fine */
        if (pos >= src_len && !done) {
            /* RLE data ended without EOB; tolerate if image filled */
            done = 1;
        }
    }
    return pos;
}

static int stb_icocur__decode_rle4(stb_icocur_uc const *src, int src_len,
    stb_icocur_uc *out, int w, int h, int top_down,
    stb_icocur_uc const *pal_r, stb_icocur_uc const *pal_g, stb_icocur_uc const *pal_b, int pal_count)
{
    int pos = 0;
    int x = 0;
    int y = top_down ? 0 : h - 1;
    int ydir = top_down ? 1 : -1;
    int done = 0;
    while (!done) {
        int count, val, k;
        if (pos + 2 > src_len) {
            stb_icocur__set_error("truncated RLE4 data");
            return -1;
        }
        count = src[pos++];
        val = src[pos++];
        if (count > 0) {
            int hi = (val >> 4) & 15, lo = val & 15;
            if (hi >= pal_count || lo >= pal_count) {
                stb_icocur__set_error("bad RLE4 palette index");
                return -1;
            }
            for (k = 0; k < count; ++k) {
                int idx = (k & 1) == 0 ? hi : lo;
                if (x >= w || y < 0 || y >= h) {
                    stb_icocur__set_error("bad RLE4 run");
                    return -1;
                }
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 0] = pal_r[idx];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 1] = pal_g[idx];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 2] = pal_b[idx];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = 255;
                ++x;
            }
        } else {
            if (val == 0) {
                x = 0;
                y += ydir;
            } else if (val == 1) {
                done = 1;
            } else if (val == 2) {
                int dx, dy;
                if (pos + 2 > src_len) {
                    stb_icocur__set_error("truncated RLE4 delta");
                    return -1;
                }
                dx = src[pos++];
                dy = src[pos++];
                x += dx;
                y += ydir * dy;
                if (x < 0 || x > w || y < -1 || y > h) {
                    stb_icocur__set_error("bad RLE4 delta");
                    return -1;
                }
            } else {
                int n = val; /* pixels */
                int bytes = (n + 1) / 2;
                int j;
                if (pos + bytes > src_len) {
                    stb_icocur__set_error("truncated RLE4 data");
                    return -1;
                }
                for (j = 0; j < n; ++j) {
                    int b = src[pos + j / 2];
                    int idx = (j & 1) == 0 ? ((b >> 4) & 15) : (b & 15);
                    if (x >= w || y < 0 || y >= h) {
                        stb_icocur__set_error("bad RLE4 absolute run");
                        return -1;
                    }
                    if (idx >= pal_count) {
                        stb_icocur__set_error("bad RLE4 palette index");
                        return -1;
                    }
                    out[((size_t) y * (size_t) w + (size_t) x) * 4 + 0] = pal_r[idx];
                    out[((size_t) y * (size_t) w + (size_t) x) * 4 + 1] = pal_g[idx];
                    out[((size_t) y * (size_t) w + (size_t) x) * 4 + 2] = pal_b[idx];
                    out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = 255;
                    ++x;
                }
                pos += bytes;
                /* absolute data padded to word boundary */
                if ((((bytes & 1) == 1))) {
                    if (pos >= src_len) {
                        stb_icocur__set_error("truncated RLE4 data");
                        return -1;
                    }
                    ++pos;
                }
            }
        }
        if (pos >= src_len && !done) done = 1;
    }
    return pos;
}

/* Core BMP entry decoder. Returns RGBA buffer (caller converts req_comp).
 * Sets *px,*py. On failure returns NULL.
 */
static stb_icocur_uc *stb_icocur__decode_bmp(stb_icocur_uc const *d, int n,
    int dir_w, int dir_h, int *px, int *py)
{
    stb_icocur__bmpinfo bi;
    stb_icocur_uc pal_r[256], pal_g[256], pal_b[256];
    stb_icocur_uc *out = 0;
    size_t pixcount;
    int w, h, x, y;
    int use_alpha = 0;
    int has_nonzero_alpha = 0;
    int i;

    for (i = 0; i < 256; ++i) pal_r[i] = pal_g[i] = pal_b[i] = 0;

    if (!stb_icocur__parse_bmp(d, n, dir_w, dir_h, &bi)) return 0;
    w = bi.width;
    h = bi.height;

    pixcount = (size_t) w * (size_t) h;
    out = (stb_icocur_uc *) STB_ICOCUR_MALLOC(pixcount * 4);
    if (!out) {
        stb_icocur__set_error("out of memory");
        return 0;
    }
    memset(out, 0, pixcount * 4);

    /* load palette */
    if (bi.palette_count > 0) {
        int k;
        for (k = 0; k < bi.palette_count; ++k) {
            stb_icocur_uc const *pe = d + bi.palette_offset + k * bi.palette_size;
            unsigned char b = pe[0], g = pe[1], r = pe[2];
            pal_r[k] = r; pal_g[k] = g; pal_b[k] = b;
        }
    }

    if (bi.compression == 1) {
        int consumed;
        consumed = stb_icocur__decode_rle8(d + bi.xor_offset, n - bi.xor_offset,
            out, w, h, bi.top_down, pal_r, pal_g, pal_b,
            bi.palette_count > 0 ? bi.palette_count : 256);
        if (consumed < 0) { STB_ICOCUR_FREE(out); return 0; }
        /* AND mask follows RLE data if room */
        bi.and_offset = bi.xor_offset + consumed;
        bi.and_size = ((w + 31) / 32) * 4 * h;
        if (bi.has_and && bi.and_offset + bi.and_size <= n) {
            /* keep has_and */
        } else {
            bi.has_and = 0;
        }
    } else if (bi.compression == 2) {
        int consumed;
        consumed = stb_icocur__decode_rle4(d + bi.xor_offset, n - bi.xor_offset,
            out, w, h, bi.top_down, pal_r, pal_g, pal_b,
            bi.palette_count > 0 ? bi.palette_count : 16);
        if (consumed < 0) { STB_ICOCUR_FREE(out); return 0; }
        bi.and_offset = bi.xor_offset + consumed;
        bi.and_size = ((w + 31) / 32) * 4 * h;
        if (bi.has_and && bi.and_offset + bi.and_size <= n) {
            /* keep */
        } else {
            bi.has_and = 0;
        }
    } else if (bi.bpp == 1) {
        int row_bytes = ((1 * w + 31) / 32) * 4;
        for (y = 0; y < h; ++y) {
            int src_y = bi.top_down ? y : (h - 1 - y);
            stb_icocur_uc const *row = d + bi.xor_offset + (size_t) src_y * (size_t) row_bytes;
            for (x = 0; x < w; ++x) {
                int byte = row[x >> 3];
                int bit = (byte >> (7 - (x & 7))) & 1;
                if (bit < 0 || bit >= 256 || (bi.palette_count > 0 && bit >= bi.palette_count)) {
                    /* tolerate missing palette entries as black */
                    bit = 0;
                }
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 0] = pal_r[bit];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 1] = pal_g[bit];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 2] = pal_b[bit];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = 255;
            }
        }
    } else if (bi.bpp == 4) {
        int row_bytes = ((4 * w + 31) / 32) * 4;
        for (y = 0; y < h; ++y) {
            int src_y = bi.top_down ? y : (h - 1 - y);
            stb_icocur_uc const *row = d + bi.xor_offset + (size_t) src_y * (size_t) row_bytes;
            for (x = 0; x < w; ++x) {
                int nib = (x & 1) == 0 ? ((row[x >> 1] >> 4) & 15) : (row[x >> 1] & 15);
                if (bi.palette_count > 0 && nib >= bi.palette_count) nib = 0;
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 0] = pal_r[nib];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 1] = pal_g[nib];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 2] = pal_b[nib];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = 255;
            }
        }
    } else if (bi.bpp == 8) {
        int row_bytes = ((8 * w + 31) / 32) * 4;
        for (y = 0; y < h; ++y) {
            int src_y = bi.top_down ? y : (h - 1 - y);
            stb_icocur_uc const *row = d + bi.xor_offset + (size_t) src_y * (size_t) row_bytes;
            for (x = 0; x < w; ++x) {
                int idx = row[x];
                if (bi.palette_count > 0 && idx >= bi.palette_count) idx = 0;
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 0] = pal_r[idx];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 1] = pal_g[idx];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 2] = pal_b[idx];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = 255;
            }
        }
    } else if (bi.bpp == 16) {
        int row_bytes = ((16 * w + 31) / 32) * 4;
        int rshift, rbits, gshift, gbits, bshift, bbits;
        int use_masks = (bi.compression == 3 &&
            (bi.red_mask != 0 || bi.green_mask != 0 || bi.blue_mask != 0));
        if (use_masks) {
            stb_icocur__mask_info(bi.red_mask, &rshift, &rbits);
            stb_icocur__mask_info(bi.green_mask, &gshift, &gbits);
            stb_icocur__mask_info(bi.blue_mask, &bshift, &bbits);
        } else {
            rshift = 10; rbits = 5;
            gshift = 5; gbits = 5;
            bshift = 0; bbits = 5;
        }
        for (y = 0; y < h; ++y) {
            int src_y = bi.top_down ? y : (h - 1 - y);
            stb_icocur_uc const *row = d + bi.xor_offset + (size_t) src_y * (size_t) row_bytes;
            for (x = 0; x < w; ++x) {
                unsigned int v = stb_icocur__ru16(row + x * 2);
                unsigned char r, g, b;
                r = stb_icocur__scale_bits((v >> rshift) & ((rbits >= 32) ? 0xFFFFFFFFu : ((rbits == 0) ? 0u : ((1u << rbits) - 1u))), rbits);
                g = stb_icocur__scale_bits((v >> gshift) & ((gbits >= 32) ? 0xFFFFFFFFu : ((gbits == 0) ? 0u : ((1u << gbits) - 1u))), gbits);
                b = stb_icocur__scale_bits((v >> bshift) & ((bbits >= 32) ? 0xFFFFFFFFu : ((bbits == 0) ? 0u : ((1u << bbits) - 1u))), bbits);
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 0] = r;
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 1] = g;
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 2] = b;
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = 255;
            }
        }
    } else if (bi.bpp == 24) {
        int row_bytes = ((24 * w + 31) / 32) * 4;
        for (y = 0; y < h; ++y) {
            int src_y = bi.top_down ? y : (h - 1 - y);
            stb_icocur_uc const *row = d + bi.xor_offset + (size_t) src_y * (size_t) row_bytes;
            for (x = 0; x < w; ++x) {
                unsigned char b = row[x * 3 + 0];
                unsigned char g = row[x * 3 + 1];
                unsigned char r = row[x * 3 + 2];
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 0] = r;
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 1] = g;
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 2] = b;
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = 255;
            }
        }
    } else if (bi.bpp == 32) {
        int row_bytes = w * 4;
        int rshift, rbits, gshift, gbits, bshift, bbits, ashift, abits;
        int use_masks = (bi.compression == 3 &&
            (bi.red_mask != 0 || bi.green_mask != 0 || bi.blue_mask != 0));
        if (use_masks) {
            stb_icocur__mask_info(bi.red_mask, &rshift, &rbits);
            stb_icocur__mask_info(bi.green_mask, &gshift, &gbits);
            stb_icocur__mask_info(bi.blue_mask, &bshift, &bbits);
            stb_icocur__mask_info(bi.alpha_mask, &ashift, &abits);
            if (bi.alpha_mask == 0) { ashift = 0; abits = 0; }
        } else {
            rshift = 16; rbits = 8;
            gshift = 8; gbits = 8;
            bshift = 0; bbits = 8;
            ashift = 24; abits = 8;
        }
        /* V4/V5 with alpha mask but BI_RGB? has_alpha_mask set */
        if (!use_masks && bi.has_alpha_mask) {
            stb_icocur__mask_info(bi.alpha_mask, &ashift, &abits);
            use_masks = 1;
            rshift = 16; rbits = 8;
            gshift = 8; gbits = 8;
            bshift = 0; bbits = 8;
        }
        for (y = 0; y < h; ++y) {
            int src_y = bi.top_down ? y : (h - 1 - y);
            stb_icocur_uc const *row = d + bi.xor_offset + (size_t) src_y * (size_t) row_bytes;
            for (x = 0; x < w; ++x) {
                unsigned char b0 = row[x * 4 + 0];
                unsigned char g0 = row[x * 4 + 1];
                unsigned char r0 = row[x * 4 + 2];
                unsigned char a0 = row[x * 4 + 3];
                unsigned char r, g, b, a;
                if (use_masks) {
                    unsigned int v = stb_icocur__ru32(row + x * 4);
                    r = stb_icocur__scale_bits((v >> rshift) & ((rbits >= 32) ? 0xFFFFFFFFu : ((rbits == 0) ? 0u : ((1u << rbits) - 1u))), rbits);
                    g = stb_icocur__scale_bits((v >> gshift) & ((gbits >= 32) ? 0xFFFFFFFFu : ((gbits == 0) ? 0u : ((1u << gbits) - 1u))), gbits);
                    b = stb_icocur__scale_bits((v >> bshift) & ((bbits >= 32) ? 0xFFFFFFFFu : ((bbits == 0) ? 0u : ((1u << bbits) - 1u))), bbits);
                    if (abits > 0)
                        a = stb_icocur__scale_bits((v >> ashift) & ((abits >= 32) ? 0xFFFFFFFFu : ((1u << abits) - 1u)), abits);
                    else
                        a = 255;
                } else {
                    r = r0; g = g0; b = b0; a = a0;
                }
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 0] = r;
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 1] = g;
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 2] = b;
                out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = a;
                if (a != 0) has_nonzero_alpha = 1;
            }
        }
        use_alpha = has_nonzero_alpha;
        if (!use_alpha) {
            /* force opaque; AND mask will carve transparency */
            for (y = 0; y < h; ++y)
                for (x = 0; x < w; ++x)
                    out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = 255;
        }
    } else {
        STB_ICOCUR_FREE(out);
        stb_icocur__set_error("unsupported BMP bit depth");
        return 0;
    }

    /* AND mask */
    if (bi.has_and && !use_alpha) {
        int and_row = ((w + 31) / 32) * 4;
        if (bi.and_offset + and_row * h <= n) {
            for (y = 0; y < h; ++y) {
                int src_y = bi.top_down ? y : (h - 1 - y);
                stb_icocur_uc const *arow = d + bi.and_offset + (size_t) src_y * (size_t) and_row;
                for (x = 0; x < w; ++x) {
                    int bit = (arow[x >> 3] >> (7 - (x & 7))) & 1;
                    if (bit) {
                        out[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] = 0;
                    }
                }
            }
        } else {
            /* truncated AND mask already downgraded to opaque in parser */
        }
    }

    if (px) *px = w;
    if (py) *py = h;
    return out;
}

/* ---------------- entry query / load ---------------- */

static int stb_icocur__query_index(stb_icocur_uc const *b, int len, int index,
    int *px, int *py, int *pcomp, int *pbpp, int *pis_png,
    stb_icocur__direntry *pentry_out)
{
    int type, count;
    stb_icocur__direntry *entries;
    stb_icocur_uc const *d;
    int n;
    int is_png;
    if (!stb_icocur__parse_dir(b, len, &type, &entries, &count)) return 0;
    if (index < 0 || index >= count) {
        STB_ICOCUR_FREE(entries);
        stb_icocur__set_error("entry index out of range");
        return 0;
    }
    d = b + entries[index].image_offset;
    n = entries[index].bytes_in_res;
    if (pentry_out) *pentry_out = entries[index];
    is_png = stb_icocur__is_png_sig(d, n);
    if (is_png) {
#ifndef STB_ICOCUR_NO_PNG
        {
            int x, y, comp, bpp;
            if (!stb_icocur__png_info(d, n, &x, &y, &comp, &bpp)) {
                STB_ICOCUR_FREE(entries);
                return 0;
            }
            if (px) *px = x;
            if (py) *py = y;
            if (pcomp) *pcomp = comp;
            if (pbpp) *pbpp = bpp;
            if (pis_png) *pis_png = 1;
            STB_ICOCUR_FREE(entries);
            return 1;
        }
#else
        STB_ICOCUR_FREE(entries);
        stb_icocur__set_error("PNG-compressed entry not supported (STB_ICOCUR_NO_PNG)");
        return 0;
#endif
    } else {
        stb_icocur__bmpinfo bi;
        if (!stb_icocur__parse_bmp(d, n, entries[index].width, entries[index].height, &bi)) {
            STB_ICOCUR_FREE(entries);
            return 0;
        }
        if (px) *px = bi.width;
        if (py) *py = bi.height;
        if (pcomp) *pcomp = 4;
        if (pbpp) *pbpp = bi.bpp;
        if (pis_png) *pis_png = 0;
        STB_ICOCUR_FREE(entries);
        return 1;
    }
}

static stb_icocur_uc *stb_icocur__load_index(stb_icocur_uc const *b, int len, int index,
    int *px, int *py, int *pcomp, int req_comp)
{
    int type, count;
    stb_icocur__direntry *entries;
    stb_icocur_uc const *d;
    int n;
    stb_icocur_uc *out = 0;
    int x = 0, y = 0, comp = 0;
    if (req_comp < 0 || req_comp > 4) {
        stb_icocur__set_error("bad req_comp (must be 0..4)");
        return 0;
    }
    if (!stb_icocur__parse_dir(b, len, &type, &entries, &count)) return 0;
    if (index < 0 || index >= count) {
        STB_ICOCUR_FREE(entries);
        stb_icocur__set_error("entry index out of range");
        return 0;
    }
    d = b + entries[index].image_offset;
    n = entries[index].bytes_in_res;
    {
        int dir_w = entries[index].width;
        int dir_h = entries[index].height;
        STB_ICOCUR_FREE(entries);
        if (stb_icocur__is_png_sig(d, n)) {
#ifdef STB_ICOCUR_NO_PNG
            stb_icocur__set_error("PNG-compressed entry not supported (STB_ICOCUR_NO_PNG)");
            return 0;
#else
            stb_icocur_png_decoder_func dec = stb_icocur__png_decoder;
#ifdef STB_ICOCUR_USE_STB_IMAGE
            if (!dec) dec = stbi_load_from_memory;
#endif
            if (!dec) {
                stb_icocur__set_error("PNG-compressed icon requires a PNG decoder (define STB_ICOCUR_USE_STB_IMAGE or call stb_icocur_set_png_decoder)");
                return 0;
            }
            out = dec(d, n, &x, &y, &comp, req_comp);
            if (!out) {
                /* keep stb_image error if it set one? we set generic only if none */
                if (stb_icocur__failure_reason == 0 || stb_icocur__failure_reason[0] == '\0')
                    stb_icocur__set_error("PNG decoder failed");
                else {
                    /* if decoder was stb_image it does not set our error; set one */
                    stb_icocur__set_error("PNG decoder failed");
                }
                return 0;
            }
            if (x <= 0 || y <= 0) {
                stb_icocur__set_error("bad PNG dimensions");
                return 0;
            }
            /* apply our flip flag on top (decoder may have its own) */
            if (stb_icocur__flip_vertically) {
                int eff = req_comp ? req_comp : comp;
                if (eff >= 1 && eff <= 4) stb_icocur__flip_rows(out, x, y, eff);
            }
#endif
        } else {
            out = stb_icocur__decode_bmp(d, n, dir_w, dir_h, &x, &y);
            if (!out) return 0;
            comp = 4;
            if (stb_icocur__flip_vertically) stb_icocur__flip_rows(out, x, y, 4);
            if (req_comp && req_comp != 4) {
                out = stb_icocur__convert(out, x, y, req_comp);
                if (!out) return 0;
            }
        }
    }
    if (px) *px = x;
    if (py) *py = y;
    if (pcomp) *pcomp = comp;
    return out;
}

/* best index: largest area, break ties with bpp/channels */
static int stb_icocur__best_index(stb_icocur_uc const *b, int len)
{
    int type, count, i, best;
    stb_icocur__direntry *entries;
    long best_score_area = -1;
    int best_score_bpp = -1;
    if (!stb_icocur__parse_dir(b, len, &type, &entries, &count)) return -1;
    best = -1;
    for (i = 0; i < count; ++i) {
        int x, y, comp, bpp;
        stb_icocur_uc const *d = b + entries[i].image_offset;
        int n = entries[i].bytes_in_res;
        int ok = 0;
        if (stb_icocur__is_png_sig(d, n)) {
#ifndef STB_ICOCUR_NO_PNG
            ok = stb_icocur__png_info(d, n, &x, &y, &comp, &bpp);
#else
            ok = 0;
#endif
            if (!ok) {
                /* fall back to directory dims for scoring */
                x = entries[i].width; y = entries[i].height;
                bpp = 32; comp = 4;
                ok = 1; /* scorable but not decodable alone */
            }
        } else {
            stb_icocur__bmpinfo bi;
            if (stb_icocur__parse_bmp(d, n, entries[i].width, entries[i].height, &bi)) {
                x = bi.width; y = bi.height; bpp = bi.bpp; comp = 4;
                ok = 1;
            } else {
                /* clear parser error; try next */
                ok = 0;
            }
        }
        if (ok) {
            long area = (long) x * (long) y;
            if (area > best_score_area || (area == best_score_area && bpp > best_score_bpp)) {
                best_score_area = area;
                best_score_bpp = bpp;
                best = i;
            }
        }
    }
    STB_ICOCUR_FREE(entries);
    /* reset error if we found something but parser left an error from a bad entry */
    if (best >= 0) stb_icocur__set_error("no error");
    else stb_icocur__set_error("no decodable entry");
    return best;
}

/* ---------------- read-all helpers ---------------- */

#ifndef STB_ICOCUR_NO_STDIO
static stb_icocur_uc *stb_icocur__read_file(char const *filename, int *plen)
{
    FILE *f;
    long len, got;
    stb_icocur_uc *buf;
    if (!filename || !plen) {
        stb_icocur__set_error("bad filename");
        return 0;
    }
    f = fopen(filename, "rb");
    if (!f) {
        stb_icocur__set_error("could not open file");
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        stb_icocur__set_error("could not seek file");
        return 0;
    }
    len = ftell(f);
    if (len < 0) {
        /* ftell failed (e.g. pipe); read incrementally */
        size_t cap = 16384, used = 0;
        fclose(f);
        f = fopen(filename, "rb");
        if (!f) {
            stb_icocur__set_error("could not open file");
            return 0;
        }
        buf = (stb_icocur_uc *) STB_ICOCUR_MALLOC(cap);
        if (!buf) { fclose(f); stb_icocur__set_error("out of memory"); return 0; }
        for (;;) {
            size_t r;
            if (used == cap) {
                size_t ncap = cap * 2;
                stb_icocur_uc *nbuf;
                if (ncap > 64u*1024u*1024u) {
                    STB_ICOCUR_FREE(buf); fclose(f);
                    stb_icocur__set_error("file too large");
                    return 0;
                }
                nbuf = (stb_icocur_uc *) STB_ICOCUR_REALLOC(buf, ncap);
                if (!nbuf) { STB_ICOCUR_FREE(buf); fclose(f); stb_icocur__set_error("out of memory"); return 0; }
                buf = nbuf; cap = ncap;
            }
            r = fread(buf + used, 1, cap - used, f);
            used += r;
            if (r == 0) break;
        }
        fclose(f);
        *plen = (int) used;
        return buf;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        stb_icocur__set_error("could not seek file");
        return 0;
    }
    if (len > 64*1024*1024) {
        fclose(f);
        stb_icocur__set_error("file too large");
        return 0;
    }
    if (len < 6) {
        fclose(f);
        stb_icocur__set_error("not an ICO/CUR file (too short)");
        return 0;
    }
    buf = (stb_icocur_uc *) STB_ICOCUR_MALLOC((size_t) len);
    if (!buf) { fclose(f); stb_icocur__set_error("out of memory"); return 0; }
    got = (long) fread(buf, 1, (size_t) len, f);
    fclose(f);
    if (got != len) {
        STB_ICOCUR_FREE(buf);
        stb_icocur__set_error("could not read file");
        return 0;
    }
    *plen = (int) len;
    return buf;
}
#endif

static stb_icocur_uc *stb_icocur__read_callbacks(stb_icocur_callbacks const *c, void *user, int *plen)
{
    size_t cap, used;
    stb_icocur_uc *buf;
    if (!c || !c->read || !plen) {
        stb_icocur__set_error("bad callbacks");
        return 0;
    }
    cap = 16384; used = 0;
    buf = (stb_icocur_uc *) STB_ICOCUR_MALLOC(cap);
    if (!buf) { stb_icocur__set_error("out of memory"); return 0; }
    for (;;) {
        int n;
        if (c->eof && c->eof(user)) break;
        if (used == cap) {
            size_t ncap = cap * 2;
            stb_icocur_uc *nbuf;
            if (ncap > 64u*1024u*1024u) {
                STB_ICOCUR_FREE(buf);
                stb_icocur__set_error("stream too large");
                return 0;
            }
            nbuf = (stb_icocur_uc *) STB_ICOCUR_REALLOC(buf, ncap);
            if (!nbuf) { STB_ICOCUR_FREE(buf); stb_icocur__set_error("out of memory"); return 0; }
            buf = nbuf; cap = ncap;
        }
        n = c->read(user, (char *) (buf + used), (int) (cap - used > 2147483647 ? 2147483647 : cap - used));
        if (n <= 0) break;
        used += (size_t) n;
        if (used > 64u*1024u*1024u) {
            STB_ICOCUR_FREE(buf);
            stb_icocur__set_error("stream too large");
            return 0;
        }
    }
    *plen = (int) used;
    return buf;
}

/* ---------------- public API ---------------- */

char const *stb_icocur_failure_reason(void)
{
    return stb_icocur__failure_reason;
}

void stb_icocur_set_flip_vertically_on_load(int flag)
{
    stb_icocur__flip_vertically = flag ? 1 : 0;
}

void stb_icocur_free(void *ptr)
{
    if (ptr) STB_ICOCUR_FREE(ptr);
}

#ifndef STB_ICOCUR_NO_PNG
void stb_icocur_set_png_decoder(stb_icocur_png_decoder_func f)
{
    stb_icocur__png_decoder = f;
}
#endif

int stb_icocur_is_ico_from_memory(stb_icocur_uc const *buffer, int len)
{
    if (!buffer || len < 6) return 0;
    if (buffer[0] != 0 || buffer[1] != 0) return 0;
    return stb_icocur__ru16(buffer + 2) == 1;
}

int stb_icocur_is_cur_from_memory(stb_icocur_uc const *buffer, int len)
{
    if (!buffer || len < 6) return 0;
    if (buffer[0] != 0 || buffer[1] != 0) return 0;
    return stb_icocur__ru16(buffer + 2) == 2;
}

int stb_icocur_type_from_memory(stb_icocur_uc const *buffer, int len)
{
    if (!buffer || len < 6) return 0;
    if (buffer[0] != 0 || buffer[1] != 0) return 0;
    {
        int t = (int) stb_icocur__ru16(buffer + 2);
        if (t == 1 || t == 2) return t;
    }
    return 0;
}

int stb_icocur_count_from_memory(stb_icocur_uc const *buffer, int len)
{
    int type, count;
    stb_icocur__direntry *entries;
    if (!stb_icocur__parse_dir(buffer, len, &type, &entries, &count)) return 0;
    STB_ICOCUR_FREE(entries);
    return count;
}

int stb_icocur_info_from_memory(stb_icocur_uc const *buffer, int len, int *x, int *y, int *comp)
{
    int best, ok;
    int qx = 0, qy = 0, qc = 0;
    best = stb_icocur__best_index(buffer, len);
    if (best < 0) return 0;
    ok = stb_icocur__query_index(buffer, len, best, &qx, &qy, &qc, 0, 0, 0);
    if (!ok) return 0;
    if (x) *x = qx;
    if (y) *y = qy;
    if (comp) *comp = qc;
    return 1;
}

int stb_icocur_query_from_memory(stb_icocur_uc const *buffer, int len, int index,
    int *x, int *y, int *comp, int *bpp, int *is_png)
{
    return stb_icocur__query_index(buffer, len, index, x, y, comp, bpp, is_png, 0);
}

int stb_icocur_hotspot_from_memory(stb_icocur_uc const *buffer, int len, int index,
    int *xhot, int *yhot)
{
    int type, count;
    stb_icocur__direntry *entries;
    if (!stb_icocur__parse_dir(buffer, len, &type, &entries, &count)) return 0;
    if (type != 2) {
        STB_ICOCUR_FREE(entries);
        stb_icocur__set_error("not a CUR file (no hotspot)");
        return 0;
    }
    if (index < 0 || index >= count) {
        STB_ICOCUR_FREE(entries);
        stb_icocur__set_error("entry index out of range");
        return 0;
    }
    if (xhot) *xhot = entries[index].hotspot_x;
    if (yhot) *yhot = entries[index].hotspot_y;
    STB_ICOCUR_FREE(entries);
    return 1;
}

int stb_icocur_is_png_from_memory(stb_icocur_uc const *buffer, int len, int index)
{
    int is_png = 0;
    if (!stb_icocur__query_index(buffer, len, index, 0, 0, 0, 0, &is_png, 0)) return -1;
    return is_png ? 1 : 0;
}

stb_icocur_uc *stb_icocur_load_from_memory(stb_icocur_uc const *buffer, int len,
    int *x, int *y, int *comp, int req_comp)
{
    int best, count;
    int type;
    stb_icocur__direntry *entries = 0;
    stb_icocur_uc *out = 0;
    if (req_comp < 0 || req_comp > 4) {
        stb_icocur__set_error("bad req_comp (must be 0..4)");
        return 0;
    }
    best = stb_icocur__best_index(buffer, len);
    if (best < 0) return 0;
    /* try best first, then fall back by score order */
    if (!stb_icocur__parse_dir(buffer, len, &type, &entries, &count)) return 0;
    {
        /* build order: best, then rest sorted by area desc */
        int *order;
        int k, m;
        order = (int *) STB_ICOCUR_MALLOC(sizeof(int) * (size_t) count);
        if (!order) { STB_ICOCUR_FREE(entries); stb_icocur__set_error("out of memory"); return 0; }
        for (k = 0; k < count; ++k) order[k] = k;
        /* simple selection sort by (area, bpp) using query; failures sort last */
        for (k = 0; k < count; ++k) {
            int bestk = k;
            int bx = 0, by = 0, bb = -1;
            int jx = 0, jy = 0, jb = -1;
            {
                int tx, ty, tc, tb, tp;
                if (stb_icocur__query_index(buffer, len, order[k], &tx, &ty, &tc, &tb, &tp, 0)) {
                    bx = tx; by = ty; bb = tb;
                    if (tp) bb += 100; /* slight PNG preference on ties? keep */
                } else {
                    bx = -1; by = -1; bb = -2;
                }
                bestk = k;
                for (m = k + 1; m < count; ++m) {
                    int ux, uy, uc, ub, up;
                    long barea, uarea;
                    if (!stb_icocur__query_index(buffer, len, order[m], &ux, &uy, &uc, &ub, &up, 0)) {
                        ux = -1; uy = -1; ub = -2;
                    } else if (up) {
                        /* keep */
                    }
                    barea = (long) bx * (long) by;
                    uarea = (long) ux * (long) uy;
                    if (uarea > barea || (uarea == barea && ub > bb)) {
                        bestk = m;
                        bx = ux; by = uy; bb = ub;
                        jx = 0; jy = 0; jb = 0;
                        (void) jx; (void) jy; (void) jb;
                    }
                }
            }
            if (bestk != k) {
                int t = order[k];
                order[k] = order[bestk];
                order[bestk] = t;
            }
        }
        STB_ICOCUR_FREE(entries);
        entries = 0;
        /* ensure best is first (sort already does, but enforce) */
        {
            int pos = -1;
            for (k = 0; k < count; ++k) if (order[k] == best) { pos = k; break; }
            if (pos > 0) {
                int t = order[0];
                order[0] = order[pos];
                order[pos] = t;
            }
        }
        for (k = 0; k < count; ++k) {
            char const *prev_err = stb_icocur__failure_reason;
            (void) prev_err;
            out = stb_icocur__load_index(buffer, len, order[k], x, y, comp, req_comp);
            if (out) break;
            /* if PNG decoder missing, try next BMP entry instead of failing */
        }
        STB_ICOCUR_FREE(order);
    }
    if (!out) {
        /* last error preserved */
        return 0;
    }
    return out;
}

stb_icocur_uc *stb_icocur_load_index_from_memory(stb_icocur_uc const *buffer, int len,
    int index, int *x, int *y, int *comp, int req_comp)
{
    return stb_icocur__load_index(buffer, len, index, x, y, comp, req_comp);
}

/* callbacks */

int stb_icocur_is_ico_from_callbacks(stb_icocur_callbacks const *c, void *user)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_callbacks(c, user, &len);
    if (!buf) return 0;
    r = stb_icocur_is_ico_from_memory(buf, len);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_is_cur_from_callbacks(stb_icocur_callbacks const *c, void *user)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_callbacks(c, user, &len);
    if (!buf) return 0;
    r = stb_icocur_is_cur_from_memory(buf, len);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_type_from_callbacks(stb_icocur_callbacks const *c, void *user)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_callbacks(c, user, &len);
    if (!buf) return 0;
    r = stb_icocur_type_from_memory(buf, len);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_count_from_callbacks(stb_icocur_callbacks const *c, void *user)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_callbacks(c, user, &len);
    if (!buf) return 0;
    r = stb_icocur_count_from_memory(buf, len);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_info_from_callbacks(stb_icocur_callbacks const *c, void *user,
    int *x, int *y, int *comp)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_callbacks(c, user, &len);
    if (!buf) return 0;
    r = stb_icocur_info_from_memory(buf, len, x, y, comp);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_query_from_callbacks(stb_icocur_callbacks const *c, void *user,
    int index, int *x, int *y, int *comp, int *bpp, int *is_png)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_callbacks(c, user, &len);
    if (!buf) return 0;
    r = stb_icocur_query_from_memory(buf, len, index, x, y, comp, bpp, is_png);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_hotspot_from_callbacks(stb_icocur_callbacks const *c, void *user,
    int index, int *xhot, int *yhot)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_callbacks(c, user, &len);
    if (!buf) return 0;
    r = stb_icocur_hotspot_from_memory(buf, len, index, xhot, yhot);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_is_png_from_callbacks(stb_icocur_callbacks const *c, void *user, int index)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_callbacks(c, user, &len);
    if (!buf) return -1;
    r = stb_icocur_is_png_from_memory(buf, len, index);
    STB_ICOCUR_FREE(buf);
    return r;
}

stb_icocur_uc *stb_icocur_load_from_callbacks(stb_icocur_callbacks const *c, void *user,
    int *x, int *y, int *comp, int req_comp)
{
    int len = 0;
    stb_icocur_uc *buf, *out;
    buf = stb_icocur__read_callbacks(c, user, &len);
    if (!buf) return 0;
    out = stb_icocur_load_from_memory(buf, len, x, y, comp, req_comp);
    STB_ICOCUR_FREE(buf);
    return out;
}

stb_icocur_uc *stb_icocur_load_index_from_callbacks(stb_icocur_callbacks const *c, void *user,
    int index, int *x, int *y, int *comp, int req_comp)
{
    int len = 0;
    stb_icocur_uc *buf, *out;
    buf = stb_icocur__read_callbacks(c, user, &len);
    if (!buf) return 0;
    out = stb_icocur_load_index_from_memory(buf, len, index, x, y, comp, req_comp);
    STB_ICOCUR_FREE(buf);
    return out;
}

#ifndef STB_ICOCUR_NO_STDIO

int stb_icocur_is_ico(char const *filename)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_file(filename, &len);
    if (!buf) return 0;
    r = stb_icocur_is_ico_from_memory(buf, len);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_is_cur(char const *filename)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_file(filename, &len);
    if (!buf) return 0;
    r = stb_icocur_is_cur_from_memory(buf, len);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_type(char const *filename)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_file(filename, &len);
    if (!buf) return 0;
    r = stb_icocur_type_from_memory(buf, len);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_count(char const *filename)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_file(filename, &len);
    if (!buf) return 0;
    r = stb_icocur_count_from_memory(buf, len);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_info(char const *filename, int *x, int *y, int *comp)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_file(filename, &len);
    if (!buf) return 0;
    r = stb_icocur_info_from_memory(buf, len, x, y, comp);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_query(char const *filename, int index,
    int *x, int *y, int *comp, int *bpp, int *is_png)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_file(filename, &len);
    if (!buf) return 0;
    r = stb_icocur_query_from_memory(buf, len, index, x, y, comp, bpp, is_png);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_hotspot(char const *filename, int index, int *xhot, int *yhot)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_file(filename, &len);
    if (!buf) return 0;
    r = stb_icocur_hotspot_from_memory(buf, len, index, xhot, yhot);
    STB_ICOCUR_FREE(buf);
    return r;
}

int stb_icocur_is_png(char const *filename, int index)
{
    int len = 0, r;
    stb_icocur_uc *buf = stb_icocur__read_file(filename, &len);
    if (!buf) return -1;
    r = stb_icocur_is_png_from_memory(buf, len, index);
    STB_ICOCUR_FREE(buf);
    return r;
}

stb_icocur_uc *stb_icocur_load(char const *filename,
    int *x, int *y, int *comp, int req_comp)
{
    int len = 0;
    stb_icocur_uc *buf, *out;
    buf = stb_icocur__read_file(filename, &len);
    if (!buf) return 0;
    out = stb_icocur_load_from_memory(buf, len, x, y, comp, req_comp);
    STB_ICOCUR_FREE(buf);
    return out;
}

stb_icocur_uc *stb_icocur_load_index(char const *filename, int index,
    int *x, int *y, int *comp, int req_comp)
{
    int len = 0;
    stb_icocur_uc *buf, *out;
    buf = stb_icocur__read_file(filename, &len);
    if (!buf) return 0;
    out = stb_icocur_load_index_from_memory(buf, len, index, x, y, comp, req_comp);
    STB_ICOCUR_FREE(buf);
    return out;
}

#endif /* STB_ICOCUR_NO_STDIO */

#endif /* STB_ICOCUR_IMPLEMENTATION */

/*
------------------------------------------------------------------------------
LICENSE (MIT, same choice as stb_image)
------------------------------------------------------------------------------
Copyright (c) 2026 stb_icocur contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
*/
