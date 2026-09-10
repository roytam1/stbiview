/* stb_jbig2.h - v0.1 - JBIG2 decoder - public domain
 *
 * Single-file JBIG2 decoder in stb_image style.
 * Decodes JBIG2 (ITU-T T.88) into 1-bit monochrome images.
 *
 * USAGE:
 *   #define STB_JBIG2_IMPLEMENTATION
 *   #include "stb_jbig2.h"
 *   int w, h;
 *   unsigned char *img = stb_jbig2_decode(data, len, &w, &h);
 *   if (img) { process 1bpp image; stb_jbig2_free(img); }
 *
 * See end of file for license (public domain).
 */
#ifndef STB_JBIG2_H
#define STB_JBIG2_H
#ifdef __cplusplus
extern "C" {
#endif
#define STB_JBIG2_VERSION 1

unsigned char *stb_jbig2_decode(const unsigned char *data, int size, int *width, int *height);
unsigned char *stb_jbig2_decode_embedded(const unsigned char *data, int size, int *width, int *height);
unsigned char *stb_jbig2_decode_file(const char *filename, int *width, int *height);
void stb_jbig2_free(void *p);

typedef struct stb_jbig2_context stb_jbig2_context;
typedef struct stb_jbig2_image stb_jbig2_image;

stb_jbig2_context *stb_jbig2_create(stb_jbig2_context *shared);
void stb_jbig2_destroy(stb_jbig2_context *ctx);
int stb_jbig2_submit(stb_jbig2_context *ctx, const unsigned char *data, int size);
stb_jbig2_image *stb_jbig2_page_out(stb_jbig2_context *ctx);
void stb_jbig2_release_page(stb_jbig2_context *ctx, stb_jbig2_image *img);
int stb_jbig2_image_width(stb_jbig2_image *img);
int stb_jbig2_image_height(stb_jbig2_image *img);
int stb_jbig2_image_stride(stb_jbig2_image *img);
unsigned char *stb_jbig2_image_data(stb_jbig2_image *img);
int stb_jbig2_image_getpixel(stb_jbig2_image *img, int x, int y);
int stb_jbig2_complete_page(stb_jbig2_context *ctx);
#define STB_JBIG2_OPTION_EMBEDDED 1
stb_jbig2_context *stb_jbig2_create_ex(int options, stb_jbig2_context *shared);

#ifdef __cplusplus
}
#endif
#endif /* STB_JBIG2_H */

/* ========== IMPLEMENTATION ========== */
#ifdef STB_JBIG2_IMPLEMENTATION
#ifndef STB_JBIG2__IMPLEMENTATION_ONCE
#define STB_JBIG2__IMPLEMENTATION_ONCE

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef unsigned char  sj_u8;
typedef unsigned short sj_u16;
typedef unsigned int   sj_u32;
typedef int            sj_i32;
typedef short          sj_i16;
typedef signed char    sj_i8;

#define SJ_UNKNOWN ((sj_u32)~0U)

typedef enum { SJ_COMPOSE_OR=0, SJ_COMPOSE_AND=1, SJ_COMPOSE_XOR=2, SJ_COMPOSE_XNOR=3, SJ_COMPOSE_REPLACE=4 } sj_compose_op;
typedef enum { SJ_PAGE_FREE, SJ_PAGE_NEW, SJ_PAGE_COMPLETE, SJ_PAGE_RETURNED, SJ_PAGE_RELEASED } sj_page_state;
typedef enum { SJ_FILE_HDR, SJ_FILE_SEQ_HDR, SJ_FILE_SEQ_BODY, SJ_FILE_RND_HDR, SJ_FILE_RND_BODY, SJ_FILE_EOF } sj_file_state;

struct stb_jbig2_image { sj_u32 width, height, stride; sj_u8 *data; int refcount; };

typedef struct { sj_u32 number; sj_u8 flags; sj_u32 page_assoc; size_t data_len; int ref_seg_count; sj_u32 *ref_segs; sj_u32 rows; void *result; } sj_seg;

typedef struct {
    sj_page_state state; sj_u32 number, height, width;
    sj_u32 x_res, y_res; sj_u16 stripe_size; int striped;
    sj_u32 end_row; sj_u8 flags; stb_jbig2_image *image;
} sj_page;

typedef struct {
    sj_u32 C, A; int CT; sj_u32 next_word; size_t next_word_bytes;
    int err; const sj_u8 *data; size_t data_size, offset;
} sj_arith;

typedef sj_u8 sj_cx;
typedef struct { sj_cx IAx[512]; } sj_int_ctx;
typedef struct { sj_u8 len; sj_cx *x; } sj_iaid_ctx;

typedef struct {
    int log_size, n_entries, has_oob;
    sj_u8 *preflens, *rangelens;
    sj_i32 *rangelows;
} sj_huff_table;

typedef struct {
    const sj_u8 *data; size_t size, offset; int bit_off;
} sj_huff_state;

typedef struct { sj_u32 n_symbols; stb_jbig2_image **glyphs; } sj_sym_dict;
typedef struct { int n; stb_jbig2_image **patterns; sj_u32 HPW, HPH; } sj_pat_dict;

struct stb_jbig2_context {
    sj_u8 *buf; size_t buf_size, buf_rd, buf_wr;
    sj_file_state state; sj_u8 hdr_flags; sj_u32 n_pages;
    sj_u32 seg_max, n_segs, seg_idx; sj_seg **segs;
    sj_u32 cur_page, max_page; sj_page *pages;
};

/* --- Byte readers --- */
static sj_u16 sj_get16(const sj_u8 *p) { return (sj_u16)((p[0]<<8)|p[1]); }
static sj_i16 sj_geti16(const sj_u8 *p) { sj_u16 v=sj_get16(p); return (sj_i16)((v^0x8000)-0x8000); }
static sj_u32 sj_get32(const sj_u8 *p) { return ((sj_u32)sj_get16(p)<<16)|sj_get16(p+2); }

/* --- Image --- */
static stb_jbig2_image *sj_img_new(sj_u32 w, sj_u32 h) {
    stb_jbig2_image *im; sj_u32 s;
    if (!w || !h) return NULL;
    im = (stb_jbig2_image *)calloc(1, sizeof(*im));
    if (!im) return NULL;
    s = ((w-1)>>3)+1;
    if (h > 0x7fffffffu/s) { free(im); return NULL; }
    im->data = (sj_u8 *)calloc(1, (size_t)h*s);
    if (!im->data) { free(im); return NULL; }
    im->width=w; im->height=h; im->stride=s; im->refcount=1;
    return im;
}
static stb_jbig2_image *sj_img_ref(stb_jbig2_image *im) { if(im) im->refcount++; return im; }
static void sj_img_release(stb_jbig2_image *im) {
    if (!im) return;
    if (--im->refcount <= 0) { free(im->data); free(im); }
}
static void sj_img_clear(stb_jbig2_image *im, int v) { memset(im->data, v?0xFF:0x00, im->stride*im->height); }

static int sj_img_getpixel(stb_jbig2_image *im, int x, int y) {
    if (x<0||y<0||x>=(int)im->width||y>=(int)im->height) return 0;
    return (im->data[(x>>3)+y*im->stride] >> (7-(x&7))) & 1;
}

static void sj_img_setpixel(stb_jbig2_image *im, int x, int y, int v) {
    sj_u8 *p;
    if (x<0||y<0||x>=(int)im->width||y>=(int)im->height) return;
    p=&im->data[(x>>3)+y*im->stride];
    if (v) *p|=(sj_u8)(0x80>>(x&7));
    else   *p&=(sj_u8)~(0x80>>(x&7));
}

static int sj_img_compose(stb_jbig2_image *dst, stb_jbig2_image *src, int sx, int sy, sj_compose_op op) {
    sj_u32 w,h,shift,bytewidth,j;
    sj_u8 *ss,*dd,lmask,rmask;
    int late,i;
    if (!src) return 0;
    w=src->width; h=src->height; shift=(sx&7); ss=src->data;
    if (sx<0) { w=(w<(sj_u32)(-sx))?0:w+sx; ss+=(-sx-1)>>3; sx=0; }
    if (sy<0) { h=(h<(sj_u32)(-sy))?0:h+sy; ss+=(-sy)*src->stride; sy=0; }
    if ((sj_u32)sx+w>dst->width) w=(dst->width<(sj_u32)sx)?0:dst->width-(sj_u32)sx;
    if ((sj_u32)sy+h>dst->height) h=(dst->height<(sj_u32)sy)?0:dst->height-(sj_u32)sy;
    if (!w||!h) return 0;
    lmask=255>>(sx&7); rmask=((sx+w)&7)==0?255:~(255>>((sx+w)&7));
    dd=dst->data+(sy*dst->stride)+((sj_u32)sx>>3);
    bytewidth=(((sj_u32)sx+w-1)>>3)-((sj_u32)sx>>3)+1;
    if (bytewidth==1) lmask&=rmask;
    { sj_u32 stride_end = (src->width+7)>>3;
      late = (sx >= 0) ? (ss+bytewidth > src->data+stride_end)
                       : (ss+bytewidth >= src->data+stride_end); }
    for (j=0;j<h;j++) {
        sj_u8 *s=ss, *d=dd;
        /* left byte */
        { sj_u8 v;
          { sj_u8 sl=(s>src->data)?*(s-1):0; v=(sj_u8)(((sl<<8)|s[0])>>shift); }
          switch(op){
            case SJ_COMPOSE_OR:     *d|=v&lmask; break;
            case SJ_COMPOSE_AND:    *d&=(v&lmask)|~lmask; break;
            case SJ_COMPOSE_XOR:    *d^=v&lmask; break;
            case SJ_COMPOSE_XNOR:   *d^=(~v)&lmask; break;
            default: *d=(v&lmask)|(*d&~lmask); break;
          }
          d++; s++;
        }
        /* central bytes */
        for (i=1;i<(int)bytewidth-1;i++) {
            sj_u8 v = shift? (sj_u8)(((s[-1]<<8)|s[0])>>shift) : *s;
            switch(op){
              case SJ_COMPOSE_OR: *d++|=v; break;
              case SJ_COMPOSE_AND: *d++&=v; break;
              case SJ_COMPOSE_XOR: *d++^=v; break;
              case SJ_COMPOSE_XNOR: *d++^=~v; break;
              default: *d++=v; break;
            }
            s++;
        }
        /* right byte */
        if (bytewidth>1) {
            sj_u8 sn=late?0:*s;
            sj_u8 v = shift? (sj_u8)(((s[-1]<<8)|sn)>>shift) : sn;
            switch(op){
              case SJ_COMPOSE_OR: *d|=v&rmask; break;
              case SJ_COMPOSE_AND: *d&=(v&rmask)|~rmask; break;
              case SJ_COMPOSE_XOR: *d^=v&rmask; break;
              case SJ_COMPOSE_XNOR: *d^=(~v)&rmask; break;
              default: *d=(v&rmask)|(*d&~rmask); break;
            }
        }
        ss+=src->stride; dd+=dst->stride;
    }
    return 0;
}

/* --- Arithmetic Coder --- */
typedef struct { sj_u16 Qe; sj_u8 mps_xor, lps_xor; } sj_qe;

#define SJ_MPS(i,n) ((i)^(n))
#define SJ_LPS(i,n,s) ((i)^(n)^((s)<<7))

static const sj_qe sj_QE[47] = {
    {0x5601,SJ_MPS(0,1),SJ_LPS(0,1,1)},{0x3401,SJ_MPS(1,2),SJ_LPS(1,6,0)},
    {0x1801,SJ_MPS(2,3),SJ_LPS(2,9,0)},{0x0AC1,SJ_MPS(3,4),SJ_LPS(3,12,0)},
    {0x0521,SJ_MPS(4,5),SJ_LPS(4,29,0)},{0x0221,SJ_MPS(5,38),SJ_LPS(5,33,0)},
    {0x5601,SJ_MPS(6,7),SJ_LPS(6,6,1)},{0x5401,SJ_MPS(7,8),SJ_LPS(7,14,0)},
    {0x4801,SJ_MPS(8,9),SJ_LPS(8,14,0)},{0x3801,SJ_MPS(9,10),SJ_LPS(9,14,0)},
    {0x3001,SJ_MPS(10,11),SJ_LPS(10,17,0)},{0x2401,SJ_MPS(11,12),SJ_LPS(11,18,0)},
    {0x1C01,SJ_MPS(12,13),SJ_LPS(12,20,0)},{0x1601,SJ_MPS(13,29),SJ_LPS(13,21,0)},
    {0x5601,SJ_MPS(14,15),SJ_LPS(14,14,1)},{0x5401,SJ_MPS(15,16),SJ_LPS(15,14,0)},
    {0x5101,SJ_MPS(16,17),SJ_LPS(16,15,0)},{0x4801,SJ_MPS(17,18),SJ_LPS(17,16,0)},
    {0x3801,SJ_MPS(18,19),SJ_LPS(18,17,0)},{0x3401,SJ_MPS(19,20),SJ_LPS(19,18,0)},
    {0x3001,SJ_MPS(20,21),SJ_LPS(20,19,0)},{0x2801,SJ_MPS(21,22),SJ_LPS(21,19,0)},
    {0x2401,SJ_MPS(22,23),SJ_LPS(22,20,0)},{0x2201,SJ_MPS(23,24),SJ_LPS(23,21,0)},
    {0x1C01,SJ_MPS(24,25),SJ_LPS(24,22,0)},{0x1801,SJ_MPS(25,26),SJ_LPS(25,23,0)},
    {0x1601,SJ_MPS(26,27),SJ_LPS(26,24,0)},{0x1401,SJ_MPS(27,28),SJ_LPS(27,25,0)},
    {0x1201,SJ_MPS(28,29),SJ_LPS(28,26,0)},{0x1101,SJ_MPS(29,30),SJ_LPS(29,27,0)},
    {0x0AC1,SJ_MPS(30,31),SJ_LPS(30,28,0)},{0x09C1,SJ_MPS(31,32),SJ_LPS(31,29,0)},
    {0x08A1,SJ_MPS(32,33),SJ_LPS(32,30,0)},{0x0521,SJ_MPS(33,34),SJ_LPS(33,31,0)},
    {0x0441,SJ_MPS(34,35),SJ_LPS(34,32,0)},{0x02A1,SJ_MPS(35,36),SJ_LPS(35,33,0)},
    {0x0221,SJ_MPS(36,37),SJ_LPS(36,34,0)},{0x0141,SJ_MPS(37,38),SJ_LPS(37,35,0)},
    {0x0111,SJ_MPS(38,39),SJ_LPS(38,36,0)},{0x0085,SJ_MPS(39,40),SJ_LPS(39,37,0)},
    {0x0049,SJ_MPS(40,41),SJ_LPS(40,38,0)},{0x0025,SJ_MPS(41,42),SJ_LPS(41,39,0)},
    {0x0015,SJ_MPS(42,43),SJ_LPS(42,40,0)},{0x0009,SJ_MPS(43,44),SJ_LPS(43,41,0)},
    {0x0005,SJ_MPS(44,45),SJ_LPS(44,42,0)},{0x0001,SJ_MPS(45,45),SJ_LPS(45,43,0)},
    {0x5601,SJ_MPS(46,46),SJ_LPS(46,46,0)}
};

static int sj_arith_renormd(sj_arith *as);
static int sj_arith_bytein(sj_arith *as);

static int sj_arith_load_word(sj_arith *as) {
    sj_u32 val=0;
    int ret=0;
    if (as->offset>=as->data_size) return 0;
    if (as->offset<as->data_size) { val|=(sj_u32)as->data[as->offset]<<24; ret++; }
    if (as->offset+1<as->data_size) { val|=(sj_u32)as->data[as->offset+1]<<16; ret++; }
    if (as->offset+2<as->data_size) { val|=(sj_u32)as->data[as->offset+2]<<8; ret++; }
    if (as->offset+3<as->data_size) { val|=(sj_u32)as->data[as->offset+3]; ret++; }
    as->next_word=val; as->next_word_bytes=(size_t)ret; as->offset+=(size_t)ret;
    return ret;
}

static int sj_arith_bytein(sj_arith *as) {
    sj_u8 B, B1;
    if (as->err||as->next_word_bytes==0) return -1;
    B=(sj_u8)((as->next_word>>24)&0xFF);
    if (B==0xFF) {
        if (as->next_word_bytes<=1) {
            if (sj_arith_load_word(as)==0) { as->next_word=0xFF900000; as->next_word_bytes=2; as->C+=0xFF00; as->CT=8; return 0; }
            B1=(sj_u8)((as->next_word>>24)&0xFF);
            if (B1>0x8F) { as->CT=8; as->next_word=0xFF000000|(as->next_word>>8); as->next_word_bytes=2; as->offset--; }
            else { as->C+=0xFE00-(B1<<9); as->CT=7; }
        } else {
            B1=(sj_u8)((as->next_word>>16)&0xFF);
            if (B1>0x8F) as->CT=8;
            else { as->next_word_bytes--; as->next_word<<=8; as->C+=0xFE00-(B1<<9); as->CT=7; }
        }
    } else {
        as->next_word<<=8; as->next_word_bytes--;
        if (as->next_word_bytes==0) {
            if (sj_arith_load_word(as)==0) { as->next_word=0xFF900000; as->next_word_bytes=2; as->C+=0xFF00; as->CT=8; return 0; }
        }
        B=(sj_u8)((as->next_word>>24)&0xFF);
        as->C+=0xFF00-(B<<8); as->CT=8;
    }
    return 0;
}

static int sj_arith_renormd(sj_arith *as) {
    do {
        if (as->CT==0 && sj_arith_bytein(as)<0) return -1;
        as->A<<=1; as->C<<=1; as->CT--;
    } while ((as->A&0x8000)==0);
    return 0;
}

static sj_arith *sj_arith_new(const sj_u8 *data, size_t size) {
    sj_arith *as;
    if (size<1) return NULL;
    as=(sj_arith *)calloc(1,sizeof(*as));
    if (!as) return NULL;
    as->data=data; as->data_size=size;
    as->next_word=((sj_u32)data[0]<<24)|((size>1?(sj_u32)data[1]:0)<<16)|((size>2?(sj_u32)data[2]:0)<<8)|(size>3?(sj_u32)data[3]:0);
    as->next_word_bytes=size<4?(int)size:4; as->offset=as->next_word_bytes;
    as->C=(~(as->next_word>>8))&0xFF0000;
    if (sj_arith_bytein(as)<0) { free(as); return NULL; }
    as->C<<=7; as->CT-=7; as->A=0x8000;
    return as;
}

static int sj_arith_decode(sj_arith *as, sj_cx *pcx) {
    sj_cx cx=*pcx; const sj_qe *pq; unsigned idx=cx&0x7f; int D;
    if (idx>=47) return -1;
    pq=&sj_QE[idx]; as->A-=pq->Qe;
    if ((as->C>>16)<as->A) {
        if ((as->A&0x8000)==0) {
            D=(as->A<pq->Qe)?(1-(cx>>7)):(cx>>7);
            *pcx^=(as->A<pq->Qe)?pq->lps_xor:pq->mps_xor;
            if (sj_arith_renormd(as)<0) return -1;
            return D;
        }
        return cx>>7;
    }
    as->C-=(as->A)<<16;
    D=(as->A<pq->Qe)?(cx>>7):(1-(cx>>7));
    *pcx^=(as->A<pq->Qe)?pq->mps_xor:pq->lps_xor;
    as->A=pq->Qe;
    if (sj_arith_renormd(as)<0) return -1;
    return D;
}

/* --- Arithmetic Integer (A.2) --- */
static sj_int_ctx *sj_int_ctx_new(void) { sj_int_ctx *c=(sj_int_ctx*)calloc(1,sizeof(*c)); return c; }
static void sj_int_ctx_free(sj_int_ctx *c) { free(c); }

static int sj_int_decode(sj_int_ctx *ctx, sj_arith *as, sj_i32 *res) {
    sj_cx *IAx=ctx->IAx; int PREV=1,S,bit,i; sj_i32 V; int n_tail,off;
    S=sj_arith_decode(as,&IAx[PREV]); if (S<0) return -1; PREV=(PREV<<1)|S;
    bit=sj_arith_decode(as,&IAx[PREV]); if (bit<0) return -1; PREV=(PREV<<1)|bit;
    if (bit) { bit=sj_arith_decode(as,&IAx[PREV]); if (bit<0) return -1; PREV=(PREV<<1)|bit;
      if (bit) { bit=sj_arith_decode(as,&IAx[PREV]); if (bit<0) return -1; PREV=(PREV<<1)|bit;
        if (bit) { bit=sj_arith_decode(as,&IAx[PREV]); if (bit<0) return -1; PREV=(PREV<<1)|bit;
          if (bit) { bit=sj_arith_decode(as,&IAx[PREV]); if (bit<0) return -1; PREV=(PREV<<1)|bit;
            if (bit) { n_tail=32; off=4436; } else { n_tail=12; off=340; }
          } else { n_tail=8; off=84; }
        } else { n_tail=6; off=20; }
      } else { n_tail=4; off=4; }
    } else { n_tail=2; off=0; }
    V=0;
    for (i=0;i<n_tail;i++) { bit=sj_arith_decode(as,&IAx[PREV]); if (bit<0) return -1; PREV=((PREV<<1)&511)|(PREV&256)|bit; V=(V<<1)|bit; }
    if (V>0x7fffffff-off) V=0x7fffffff; else V+=off;
    V=S?-V:V; *res=V; return (S&&V==0)?1:0;
}

/* --- Arithmetic IAID (A.3) --- */
static sj_iaid_ctx *sj_iaid_new(sj_u8 len) {
    sj_iaid_ctx *c; size_t sz;
    if (sizeof(size_t)*8<=(unsigned)len) return NULL;
    sz=(size_t)1U<<len; c=(sj_iaid_ctx*)calloc(1,sizeof(*c));
    if (!c) return NULL;
    c->len=len;
    c->x=(sj_cx*)calloc(sz,sizeof(sj_cx));
    if (!c->x) { free(c); return NULL; }
    return c;
}
static void sj_iaid_free(sj_iaid_ctx *c) { if(c) { free(c->x); free(c); } }
static int sj_iaid_decode(sj_iaid_ctx *c, sj_arith *as, sj_i32 *res) {
    int PREV=1,i,D;
    for (i=0;i<c->len;i++) { D=sj_arith_decode(as,&c->x[PREV]); if (D<0) return -1; PREV=(PREV<<1)|D; }
    PREV-=1<<c->len; *res=PREV; return 0;
}

/* --- Huffman --- */
static sj_huff_state *sj_huff_new(const sj_u8 *data, size_t size) {
    sj_huff_state *hs=(sj_huff_state*)calloc(1,sizeof(*hs));
    if (!hs) return NULL;
    hs->data=data; hs->size=size; return hs;
}
static void sj_huff_free(sj_huff_state *hs) { free(hs); }

static int sj_huff_get_bits(sj_huff_state *hs, int n, int *err) {
    int r=0,i; *err=0;
    for (i=0;i<n;i++) {
        int bo=(int)(hs->offset+((hs->bit_off+i)>>3));
        int bp=7-((hs->bit_off+i)&7);
        if (bo>=(int)hs->size) { *err=1; return 0; }
        r=(r<<1)|((hs->data[bo]>>bp)&1);
    }
    hs->bit_off+=n; hs->offset+=hs->bit_off>>3; hs->bit_off&=7;
    return r;
}

/* Standard Huffman tables (simplified from jbig2dec) */
typedef struct { int plen, rlen, rlow; } sj_huff_line;

static const sj_huff_line sj_huff_A[] = {{1,0,0},{2,0,1},{3,0,2},{4,3,3},{6,0,11},{0,0,0}};
static const sj_huff_line sj_huff_B[] = {{1,0,0},{2,0,1},{3,0,2},{4,3,3},{5,6,5},{5,0,45},{6,0,44},{7,0,43},{8,0,42},{9,0,41},{10,0,40},{11,0,39},{12,0,38},{13,0,37},{14,0,36},{15,0,35},{16,0,34},{17,0,33},{18,0,32},{19,0,31},{20,0,30},{21,0,29},{22,0,28},{23,0,27},{24,0,26},{25,0,25},{26,0,24},{27,0,23},{28,0,22},{29,0,21},{30,0,20},{31,0,19},{32,0,18},{33,0,17},{34,0,16},{35,0,15},{36,0,14},{37,0,13},{38,0,12},{39,0,11},{40,0,10},{41,0,9},{42,0,8},{43,0,7},{44,0,6},{45,0,5},{46,0,4},{47,0,3},{48,0,2},{49,0,1},{0,0,0}};
static const sj_huff_line sj_huff_C[] = {{1,0,0},{2,0,1},{3,0,2},{4,3,3},{5,6,5},{5,32,0},{0,0,0}};
static const sj_huff_line sj_huff_D[] = {{1,0,0},{2,0,1},{3,0,2},{4,0,6},{5,0,7},{6,0,8},{7,0,9},{8,0,10},{9,0,11},{10,2,12},{12,7,16},{13,0,0},{0,0,0}};
static const sj_huff_line sj_huff_E[] = {{1,0,0},{2,0,1},{3,0,2},{4,0,6},{5,11,7},{5,0,0},{0,0,0}};
static const sj_huff_line sj_huff_F[] = {{2,0,0},{3,0,1},{4,0,2},{5,0,3},{5,2,4},{6,6,8},{8,10,72},{9,12,488},{10,12,2504},{11,12,6600},{12,12,21032},{13,0,0},{0,0,0}};
static const sj_huff_line sj_huff_G[] = {{3,0,0},{4,0,1},{5,0,2},{6,0,3},{7,0,4},{8,0,5},{9,0,6},{10,4,7},{12,8,135},{13,8,399},{14,0,0},{0,0,0}};
static const sj_huff_line sj_huff_H[] = {{2,0,0},{3,0,1},{4,0,2},{5,0,3},{5,1,4},{6,3,5},{7,6,13},{8,8,77},{9,10,333},{10,12,1365},{11,12,5461},{12,12,21845},{13,12,87381},{14,12,349525},{15,12,1398100},{16,12,5592396},{17,12,22369580},{18,12,89478316},{19,12,357913260},{20,12,1431653040},{21,12,0},{0,0,0}};

static const sj_huff_line *sj_huff_tables[] = {
    sj_huff_A, sj_huff_B, sj_huff_C, sj_huff_D, sj_huff_E,
    sj_huff_F, sj_huff_G, sj_huff_H
};
static const int sj_huff_table_oob[] = { 0,0,1,0,1,0,0,0 };

static int sj_huff_get(sj_huff_state *hs, int table_idx, int *oob) {
    /* Find table */
    const sj_huff_line *lines = (table_idx < 8) ? sj_huff_tables[table_idx] : sj_huff_A;
    int max_plen=0, i, total_bits;
    sj_u32 val;
    *oob=0;
    for (i=0; lines[i].plen||lines[i].rlen; i++)
        if (lines[i].plen>max_plen) max_plen=lines[i].plen;
    total_bits = max_plen;
    val = (sj_u32)sj_huff_get_bits(hs, total_bits, oob);
    if (*oob) return 0;
    /* Find matching entry */
    for (i=0; lines[i].plen||lines[i].rlen; i++) {
        if (lines[i].plen>0 && ((val>>(total_bits-lines[i].plen))==((sj_u32)(1<<lines[i].plen)-1)>>(lines[i].plen))) {
            /* Match - read extra bits */
            hs->bit_off += total_bits - lines[i].plen;
            hs->offset += hs->bit_off>>3; hs->bit_off&=7;
            if (lines[i].rlen>0) {
                int extra = sj_huff_get_bits(hs, lines[i].rlen, oob);
                if (*oob) return 0;
                return lines[i].rlow + extra;
            }
            return lines[i].rlow;
        }
    }
    *oob=1; return 0;
}

/* --- Segment management --- */
static sj_seg *sj_parse_seg_hdr(stb_jbig2_context *ctx, sj_u8 *buf, size_t buf_size, size_t *hdr_size) {
    sj_seg *r; sj_u8 rt; sj_u32 ref_cnt, ref_sz, pa_sz, off;
    (void)ctx;
    if (buf_size<11) return NULL;
    r=(sj_seg*)calloc(1,sizeof(*r)); if (!r) return NULL;
    r->number=sj_get32(buf); r->flags=buf[4];
    rt=buf[5];
    if ((rt&0xe0)==0xe0) { sj_u32 rtl=sj_get32(buf+5); ref_cnt=rtl&0x1fffffff; off=5+4+(ref_cnt+1)/8; }
    else { ref_cnt=(rt>>5); off=6; }
    r->ref_seg_count=ref_cnt;
    ref_sz=r->number<=256?1:r->number<=65536?2:4;
    pa_sz=r->flags&0x40?4:1;
    if (off+ref_cnt*ref_sz+pa_sz+4>buf_size) { free(r); return NULL; }
    if (ref_cnt) {
        sj_u32 i;
        r->ref_segs=(sj_u32*)malloc(ref_cnt*sizeof(sj_u32));
        if (!r->ref_segs) { free(r); return NULL; }
        for (i=0;i<ref_cnt;i++) {
            r->ref_segs[i]=(ref_sz==1)?buf[off]:(ref_sz==2)?sj_get16(buf+off):sj_get32(buf+off);
            off+=ref_sz;
        }
    }
    r->page_assoc=(pa_sz==4)?sj_get32(buf+off):buf[off];
    off+=pa_sz;
    r->rows=0xFFFFFFFFu; r->data_len=sj_get32(buf+off); *hdr_size=off+4;
    return r;
}

static void sj_free_seg(sj_seg *s) {
    if (!s) return;
    free(s->ref_segs);
    switch(s->flags&63) {
        case 0: if(s->result){ sj_sym_dict *d=s->result; sj_u32 i; for(i=0;i<d->n_symbols;i++) sj_img_release(d->glyphs[i]); free(d->glyphs); free(d); } break;
        case 4: case 40: if(s->result) sj_img_release((stb_jbig2_image*)s->result); break;
        case 16: if(s->result){ sj_pat_dict *p=s->result; int i; for(i=0;i<p->n;i++) sj_img_release(p->patterns[i]); free(p->patterns); free(p); } break;
        case 53: if(s->result){ sj_huff_table *t=s->result; free(t->preflens); free(t->rangelens); free(t->rangelows); free(t); } break;
    }
    free(s);
}

static sj_seg *sj_find_seg(stb_jbig2_context *ctx, sj_u32 num) {
    sj_u32 i;
    for (i=ctx->seg_idx;i>0;i--) if (ctx->segs[i-1]->number==num) return ctx->segs[i-1];
    return NULL;
}

/* --- Generic Region Decoders --- */
static int sj_gensize(int t) { return t==0?1<<16:t==1?1<<13:1<<10; }

#define SJ_OUTSIDE(x,y) ((y)<-128||(y)>0||(x)<-128||((y)<0&&(x)>127)||((y)==0&&(x)>=0))

static int sj_decode_gb(stb_jbig2_image *im, sj_arith *as, sj_cx *ctx, int tpl, int tpgdon, sj_i8 gbat[8]) {
    sj_u32 W=im->width, H=im->height, x, y;
    if (tpl==0 && (SJ_OUTSIDE(gbat[0],gbat[1])||SJ_OUTSIDE(gbat[2],gbat[3])||SJ_OUTSIDE(gbat[4],gbat[5])||SJ_OUTSIDE(gbat[6],gbat[7]))) return -1;
    if ((tpl==1||tpl==2) && SJ_OUTSIDE(gbat[0],gbat[1])) return -1;
    for (y=0;y<H;y++) {
        sj_u32 out=0; int bits=8; sj_u8 *d=&im->data[y*im->stride];
        sj_u32 pd=0,ppd=0,ctx_val;
        int bit;
        if (tpgdon) {
            int tp=sj_arith_decode(as,&ctx[tpl==0?0x9B25:tpl==1?0x0795:tpl==2?0xE5:0x195]);
            if (tp<0) return -1;
            if (tp) { if(y>0) memcpy(d,d-im->stride,im->stride); else memset(d,0,im->stride); continue; }
        }
        /* Initialize pd, ppd from previous rows */
        pd=0; ppd=0;
        if (y>0) { sj_u8 *p=im->data+(y-1)*im->stride; pd=((sj_u32)p[0]<<8); if(W>8) pd|=(sj_u32)p[1]; }
        if (y>1) { sj_u8 *p=im->data+(y-2)*im->stride; ppd=((sj_u32)p[0]<<8); if(W>8) ppd|=(sj_u32)p[1]; }
        for (x=0;x<W;x++) {
            switch(tpl) {
            case 0:
                ctx_val=out&0xF;
                ctx_val|=sj_img_getpixel(im,(int)x+gbat[0],(int)y+gbat[1])<<4;
                ctx_val|=(pd>>8)&0x3E0;
                ctx_val|=sj_img_getpixel(im,(int)x+gbat[2],(int)y+gbat[3])<<10;
                ctx_val|=sj_img_getpixel(im,(int)x+gbat[4],(int)y+gbat[5])<<11;
                ctx_val|=(ppd>>2)&0x7000;
                ctx_val|=sj_img_getpixel(im,(int)x+gbat[6],(int)y+gbat[7])<<15;
                break;
            case 1:
                ctx_val=out&0x7;
                ctx_val|=sj_img_getpixel(im,(int)x+gbat[0],(int)y+gbat[1])<<3;
                ctx_val|=(pd>>9)&0x1F0;
                ctx_val|=(ppd>>4)&0x1E00;
                break;
            case 2:
                ctx_val=out&0x3;
                ctx_val|=sj_img_getpixel(im,(int)x+gbat[0],(int)y+gbat[1])<<2;
                ctx_val|=(pd>>11)&0x78;
                ctx_val|=(ppd>>7)&0x380;
                break;
            default:
                ctx_val=out&0xF;
                ctx_val|=(pd>>9)&0x3E0;
                break;
            }
            bit=sj_arith_decode(as,&ctx[ctx_val]); if(bit<0) return -1;
            pd<<=1; ppd<<=1;
            out=(out<<1)|bit; bits--;
            *d=(sj_u8)(out<<bits);
            if (!bits) { bits=8; d++; if(x+9<W&&y>0){pd|=(sj_u32)im->data[(y-1)*im->stride+((x+9)>>3)];if(y>1)ppd|=(sj_u32)im->data[(y-2)*im->stride+((x+9)>>3)];} }
        }
        if (bits!=8) *d=(sj_u8)(out<<bits);
    }
    return 0;
}

/* --- Page management --- */
static int sj_page_info(stb_jbig2_context *ctx, sj_seg *seg, const sj_u8 *sd) {
    sj_page *pg;
    /* Find free page */
    { size_t idx=ctx->cur_page;
      while(ctx->pages[idx].state!=SJ_PAGE_FREE) {
        idx++;
        if(idx>=ctx->max_page) {
          sj_page *pp; ctx->max_page<<=2;
          pp=(sj_page*)realloc(ctx->pages,ctx->max_page*sizeof(sj_page));
          if(!pp) return -1;
          ctx->pages=pp;
          { size_t j; for(j=idx;j<ctx->max_page;j++) { ctx->pages[j].state=SJ_PAGE_FREE; ctx->pages[j].image=NULL; } }
        }
      }
      pg=&ctx->pages[idx]; ctx->cur_page=(sj_u32)idx;
    }
    pg->state=SJ_PAGE_NEW; pg->number=seg->page_assoc;
    if (seg->data_len<19) return -1;
    pg->width=sj_get32(sd); pg->height=sj_get32(sd+4);
    pg->x_res=sj_get32(sd+8); pg->y_res=sj_get32(sd+12);
    pg->flags=sd[16];
    { sj_i16 strip=sj_geti16(sd+17);
      if (strip&0x8000) { pg->striped=1; pg->stripe_size=strip&0x7FFF; }
      else { pg->striped=0; pg->stripe_size=0; }
    }
    if (pg->height==0xFFFFFFFF&&!pg->striped) { pg->striped=1; pg->stripe_size=0x7FFF; }
    pg->end_row=0;
    /* Allocate image */
    if (pg->height==0xFFFFFFFF) pg->image=sj_img_new(pg->width,pg->stripe_size);
    else pg->image=sj_img_new(pg->width,pg->height);
    if (!pg->image) return -1;
    sj_img_clear(pg->image,(pg->flags&4)!=0);
    return 0;
}

static int sj_end_of_page(stb_jbig2_context *ctx, sj_seg *seg, const sj_u8 *sd) {
    (void)seg; (void)sd;
    ctx->pages[ctx->cur_page].state=SJ_PAGE_COMPLETE;
    return 0;
}

static int sj_end_of_stripe(stb_jbig2_context *ctx, sj_seg *seg, const sj_u8 *sd) {
    (void)seg;
    ctx->pages[ctx->cur_page].end_row=sj_get32(sd); return 0;
}

/* --- Decode immediate generic region --- */
static int sj_decode_imm_gen(stb_jbig2_context *ctx, sj_seg *seg, const sj_u8 *sd) {
    sj_u32 GBW,GBH,X,Y; int tpl,tpgdon; sj_i8 gbat[8];
    stb_jbig2_image *im; sj_arith *as; sj_cx *stats; int ss,code; sj_u32 d_off;
    sj_u32 comp_op;
    if(seg->data_len<25) return -1;
    GBW=sj_get32(sd); GBH=sj_get32(sd+4); X=sj_get32(sd+8); Y=sj_get32(sd+12);
    tpl=sd[16]&3; tpgdon=(sd[16]>>2)&1;
    { int i; for(i=0;i<8;i++) gbat[i]=(sj_i8)sd[17+i]; }
    /* Check MMR flag */
    if (sd[16]&0x04) return -1; /* MMR not supported */
    im=sj_img_new(GBW,GBH); if(!im) return -1;
    ss=sj_gensize(tpl); stats=(sj_cx*)calloc(ss,sizeof(sj_cx)); if(!stats){sj_img_release(im);return-1;}
    d_off=25;
    as=sj_arith_new(sd+d_off,seg->data_len-d_off);
    if(!as){free(stats);sj_img_release(im);return -1;}
    comp_op=sd[16]&7;
    code=sj_decode_gb(im,as,stats,tpl,tpgdon,gbat);
    free(as); free(stats);
    if(code<0){sj_img_release(im);return -1;}
    seg->result=im;
    { sj_page *pg=&ctx->pages[ctx->cur_page];
      if(pg->image) sj_img_compose(pg->image,im,(int)X,(int)Y,(sj_compose_op)comp_op);
    }
    return 0;
}

/* --- Decode refinement region --- */
/* Refinement region: generic region decode using a reference image for context.
 * Output image is (ref_w+RDW) x (ref_h+RDH), initialized to zero.
 * For each pixel, a 13-bit (template0) or 10-bit (template1) context is built
 * from already-decoded output pixels and reference image pixels. */
static stb_jbig2_image *sj_decode_refine_region(sj_arith *as,
    stb_jbig2_image *ref, sj_i32 rdw, sj_i32 rdh, sj_i32 refdx, sj_i32 refdy, int tpl, sj_i8 grat[4],
    sj_cx *gr_stats)
{
    sj_u32 GRW, GRH, x, y;
    stb_jbig2_image *im;
    if (!ref) return NULL;
    { sj_i32 w2 = (sj_i32)ref->width + rdw, h2 = (sj_i32)ref->height + rdh;
      if (w2 <= 0 || h2 <= 0) return NULL;
      GRW = (sj_u32)w2; GRH = (sj_u32)h2;
    }
    im = sj_img_new(GRW, GRH);
    if (!im) return NULL;
    for (y = 0; y < GRH; y++) {
        sj_u32 out = 0; int bits = 8; sj_u8 *d = &im->data[y * im->stride];
        for (x = 0; x < GRW; x++) {
            sj_u32 ctx_val = 0;
            if (tpl == 0) {
                ctx_val |= (sj_u32)sj_img_getpixel(im, (int)x - 1, (int)y) << 0;
                ctx_val |= (sj_u32)sj_img_getpixel(im, (int)x + 1, (int)y - 1) << 1;
                ctx_val |= (sj_u32)sj_img_getpixel(im, (int)x + 0, (int)y - 1) << 2;
                ctx_val |= (sj_u32)sj_img_getpixel(im, (int)x + grat[0], (int)y + grat[1]) << 3;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + 1, (int)y - refdy + 1) << 4;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + 0, (int)y - refdy + 1) << 5;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx - 1, (int)y - refdy + 1) << 6;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + 1, (int)y - refdy + 0) << 7;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + 0, (int)y - refdy + 0) << 8;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx - 1, (int)y - refdy + 0) << 9;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + 1, (int)y - refdy - 1) << 10;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + 0, (int)y - refdy - 1) << 11;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + grat[2], (int)y - refdy + grat[3]) << 12;
            } else {
                ctx_val |= (sj_u32)sj_img_getpixel(im, (int)x - 1, (int)y) << 0;
                ctx_val |= (sj_u32)sj_img_getpixel(im, (int)x + 1, (int)y - 1) << 1;
                ctx_val |= (sj_u32)sj_img_getpixel(im, (int)x + 0, (int)y - 1) << 2;
                ctx_val |= (sj_u32)sj_img_getpixel(im, (int)x - 1, (int)y - 1) << 3;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + 1, (int)y - refdy + 1) << 4;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + 0, (int)y - refdy + 1) << 5;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + 1, (int)y - refdy + 0) << 6;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + 0, (int)y - refdy + 0) << 7;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx - 1, (int)y - refdy + 0) << 8;
                ctx_val |= (sj_u32)sj_img_getpixel(ref, (int)x - refdx + 0, (int)y - refdy - 1) << 9;
            }
            { int bit = sj_arith_decode(as, &gr_stats[ctx_val]);
              if (bit < 0) { free(gr_stats); sj_img_release(im); return NULL; }
              out = (out << 1) | (sj_u32)bit; bits--;
              *d = (sj_u8)(out << bits);
              if (!bits) { bits = 8; d++; }
            }
        }
        if (bits != 8) *d = (sj_u8)(out << bits);
    }
    return im;
}

/* --- Decode text region --- */
static int sj_decode_text(stb_jbig2_context *ctx, sj_seg *seg, const sj_u8 *sd) {
    sj_u32 SBW,SBH,SBNUM,SBX,SBY;
    int SBHUFF,SBREFINE,LOGSBSTRIPS,SBSTRIPS,REFCORNER,TRANSPOSED,SBCOMBOP,SBDEFPIXEL,SBDSOFFSET,SBRTEMPLATE;
    sj_i8 sbrat[4];
    stb_jbig2_image *im;
    sj_u32 d_off;
    if(seg->data_len<17) return -1;
    SBW=sj_get32(sd); SBH=sj_get32(sd+4); SBX=sj_get32(sd+8); SBY=sj_get32(sd+12);
    {
        sj_u16 flags=sj_get16(sd+17);
        SBHUFF=flags&1; SBREFINE=(flags>>1)&1;
        LOGSBSTRIPS=(flags>>2)&3; SBSTRIPS=1<<LOGSBSTRIPS;
        REFCORNER=(flags>>4)&3; TRANSPOSED=(flags>>6)&1;
        SBCOMBOP=(flags>>7)&3; SBDEFPIXEL=(flags>>9)&1;
        SBDSOFFSET=(flags>>10)&0x1f; if(SBDSOFFSET>0x0f) SBDSOFFSET-=0x20;
        SBRTEMPLATE=(flags>>15)&1;
    }
    d_off=19;
    memset(sbrat, 0, sizeof(sbrat));
    if(SBREFINE&&!SBRTEMPLATE) { sbrat[0]=(sj_i8)sd[d_off]; sbrat[1]=(sj_i8)sd[d_off+1]; sbrat[2]=(sj_i8)sd[d_off+2]; sbrat[3]=(sj_i8)sd[d_off+3]; d_off+=4; }
    if(d_off+4>seg->data_len) return -1;
    SBNUM=sj_get32(sd+d_off); d_off+=4;
    im=sj_img_new(SBW,SBH); if(!im) return -1;
    sj_img_clear(im,(sj_u8)SBDEFPIXEL);
    if(SBHUFF) {
        /* Huffman path - simplified, just scan symbols */
        sj_u32 inst; sj_u32 curx=0;
        sj_huff_state *hs=sj_huff_new(sd+d_off,seg->data_len-d_off);
        if (!hs) { sj_img_release(im); return -1; }
        for(inst=0;inst<SBNUM;inst++) {
            int oob=0, sym=sj_huff_get(hs,0,&oob);
            if(oob) break;
            { int si;
              for(si=0;si<seg->ref_seg_count;si++) {
                sj_seg *rs=sj_find_seg(ctx,seg->ref_segs[si]);
                if(rs&&rs->result&&(rs->flags&63)==0) {
                  sj_sym_dict *d=rs->result;
                  if((sj_u32)sym<d->n_symbols&&d->glyphs[sym]) {
                    sj_img_compose(im,d->glyphs[sym],(int)curx,0,SJ_COMPOSE_OR);
                    curx+=d->glyphs[sym]->width;
                  }
                  break;
                }
              }
            }
        }
        sj_huff_free(hs);
    } else {
        /* Arithmetic path (6.4) */
        sj_arith *as;
        sj_int_ctx *iadt,*iafs,*iads,*iait,*iari,*iardw,*iardh,*iardx,*iardy;
        sj_iaid_ctx *iaid;
        sj_u32 sbnusyms=0, ninstances=0;
        sj_i32 stript, firsts, curs, dt, dfs, ids;
        sj_u32 index;
        int iaidsz=0;
        sj_cx *gr_stats=NULL;
        /* Count total symbols in referred dictionaries */
        for(index=0;index<seg->ref_seg_count;index++) {
            sj_seg *rs=sj_find_seg(ctx,seg->ref_segs[index]);
            if(rs&&rs->result&&(rs->flags&63)==0) {
                sj_sym_dict *d=rs->result;
                sbnusyms+=d->n_symbols;
            }
        }
        /* Compute IAID size: ceil(log2(SBNUMSYMS)) = smallest k where 2^k >= SBNUMSYMS */
        { iaidsz=0; while(((sj_u32)1u<<iaidsz)<sbnusyms) iaidsz++; }
        if(iaidsz<1) iaidsz=1;
        as=sj_arith_new(sd+d_off,seg->data_len-d_off);
        if(!as){sj_img_release(im);return -1;}
        iadt=sj_int_ctx_new(); iafs=sj_int_ctx_new();
        iads=sj_int_ctx_new(); iait=sj_int_ctx_new();
        iari=sj_int_ctx_new(); iaid=sj_iaid_new((sj_u8)iaidsz);
        iardw=sj_int_ctx_new(); iardh=sj_int_ctx_new();
        iardx=sj_int_ctx_new(); iardy=sj_int_ctx_new();
        {
            int ctx_sz = SBRTEMPLATE ? (1 << 10) : (1 << 13);
            gr_stats = (sj_cx *)calloc((size_t)ctx_sz, sizeof(sj_cx));
        }

        /* 6.4.5 (1): decode STRIPT */
        { int rc=sj_int_decode(iadt,as,&stript); if(rc) goto text_done; }
        stript*=-(sj_i32)SBSTRIPS;
            firsts=0;
            /* 6.4.5 (3) */
            while(ninstances<SBNUM) {
                sj_i32 id; int ri=0; int first_symbol=1;

                /* 6.4.5 (3b): decode DT */
                { int rc=sj_int_decode(iadt,as,&dt); if(rc) break; }
                dt*=(sj_i32)SBSTRIPS; stript+=dt;
                for(;;) {
                    sj_i32 curt_val;
                    if(first_symbol) {
                        /* 6.4.7: decode DFS */
                        { int rc=sj_int_decode(iafs,as,&dfs); if(rc<0) goto text_done; if(rc>0) goto text_done; }
                        firsts+=dfs; curs=firsts;
                        first_symbol=0;
                    } else {
                        /* (3c.ii guard): check instance count before decoding IDS */
                        if(ninstances>SBNUM) break;
                        /* 6.4.8: decode IDS */
                        { int rc=sj_int_decode(iads,as,&ids); if(rc<0) goto text_done; if(rc>0) break; }
                        curs+=ids+SBDSOFFSET;
                    }
                /* decode CURT */
                if(SBSTRIPS==1) curt_val=0;
                else { int rc=sj_int_decode(iait,as,&curt_val); if(rc) goto text_done; }
                {
                    sj_i32 t=stript+curt_val;
                    sj_u32 x_pos,y_pos;
                    sj_sym_dict *dict=NULL;
                    sj_seg *rs=NULL;
                    sj_i32 rdw=0,rdh=0,rdx=0,rdy=0;
                    (void)t;
                    /* decode symbol ID */
                    { int rc=sj_iaid_decode(iaid,as,&id); if(rc) goto text_done; }
                    /* decode refinement indicator */
                    if(SBREFINE) { int rc=sj_int_decode(iari,as,&ri); if(rc) goto text_done; }
                    /* decode refinement data (to sync arithmetic state) */
                    if(ri) {
                        { int rc=sj_int_decode(iardw,as,&rdw); if(rc) goto text_done; }
                        { int rc=sj_int_decode(iardh,as,&rdh); if(rc) goto text_done; }
                        { int rc=sj_int_decode(iardx,as,&rdx); if(rc) goto text_done; }
                        { int rc=sj_int_decode(iardy,as,&rdy); if(rc) goto text_done; }
                    }
                    /* look up glyph in dictionaries */
                    { int si;
                      for(si=0;si<seg->ref_seg_count;si++) {
                        sj_seg *rss=sj_find_seg(ctx,seg->ref_segs[si]);
                        if(rss&&rss->result&&(rss->flags&63)==0) {
                          sj_sym_dict *d=rss->result;
                          if((sj_u32)id<d->n_symbols) { dict=d; rs=rss; break; }
                          id-=(sj_i32)d->n_symbols;
                        }
                      }
                    }
                    (void)rs;
                    if(dict&&dict->glyphs[id]) {
                        stb_jbig2_image *ib=dict->glyphs[id];
                        if(ri) {
                            /* Refine the base glyph: decode refinement region */
                            sj_i32 refdx = (rdw >> 1) + rdx;
                            sj_i32 refdy = (rdh >> 1) + rdy;
                            stb_jbig2_image *refined = sj_decode_refine_region(as,
                                ib, rdw, rdh, refdx, refdy, SBRTEMPLATE, sbrat, gr_stats);
                            if (refined) {
                                ib = refined;
                            }
                        }
                    /* (3c.vi) CURS update before position calc */
                    if(!TRANSPOSED&&REFCORNER>1) curs+=(int)ib->width-1;
                    else if(TRANSPOSED&&!(REFCORNER&1)) curs+=(int)ib->height-1;
                    /* (3c.vii) S = CURS */
                    /* (3c.viii) Position calculation */
                    /* REFCORNER: 0=BOTTOMLEFT 1=TOPLEFT 2=BOTTOMRIGHT 3=TOPRIGHT */
                    if(!TRANSPOSED) {
                        switch(REFCORNER) {
                            case 0: /* BOTTOMLEFT */
                                x_pos=(sj_u32)curs;
                                y_pos=(sj_u32)(stript+curt_val-(int)ib->height+1);
                                break;
                            case 1: /* TOPLEFT */
                                x_pos=(sj_u32)curs;
                                y_pos=(sj_u32)(stript+curt_val);
                                break;
                            case 2: /* BOTTOMRIGHT */
                                x_pos=(sj_u32)(curs-(int)ib->width+1);
                                y_pos=(sj_u32)(stript+curt_val-(int)ib->height+1);
                                break;
                            default: /* TOPRIGHT */
                                x_pos=(sj_u32)(curs-(int)ib->width+1);
                                y_pos=(sj_u32)(stript+curt_val);
                                break;
                        }
                    } else {
                        switch(REFCORNER) {
                            case 0: /* BOTTOMLEFT */
                                x_pos=(sj_u32)(stript+curt_val);
                                y_pos=(sj_u32)(curs-(int)ib->height+1);
                                break;
                            case 1: /* TOPLEFT */
                                x_pos=(sj_u32)(stript+curt_val);
                                y_pos=(sj_u32)curs;
                                break;
                            case 2: /* BOTTOMRIGHT */
                                x_pos=(sj_u32)(stript+curt_val-(int)ib->width+1);
                                y_pos=(sj_u32)(curs-(int)ib->height+1);
                                break;
                            default: /* TOPRIGHT */
                                x_pos=(sj_u32)(stript+curt_val-(int)ib->width+1);
                                y_pos=(sj_u32)curs;
                                break;
                        }
                    }
                        sj_img_compose(im,ib,(int)x_pos,(int)y_pos,(sj_compose_op)SBCOMBOP);
                    /* (3c.x) CURS update after compose - must use refined glyph dimensions */
                    if(!TRANSPOSED&&REFCORNER<2) curs+=(int)ib->width-1;
                    else if(TRANSPOSED&&(REFCORNER&1)) curs+=(int)ib->height-1;
                    /* Release refined image (not dictionary glyph) - after (3c.x) uses it */
                    if(ri && ib!=dict->glyphs[id]) sj_img_release(ib);
                    ib = dict->glyphs[id];
                    }
                    /* (3c.xi) NINSTANCES++ — per reference, inside for(;;) */
                    ninstances++;
                }
            }
        }
text_done:
        sj_int_ctx_free(iadt); sj_int_ctx_free(iafs);
        sj_int_ctx_free(iads); sj_int_ctx_free(iait);
        sj_int_ctx_free(iari); sj_iaid_free(iaid);
        sj_int_ctx_free(iardw); sj_int_ctx_free(iardh);
        sj_int_ctx_free(iardx); sj_int_ctx_free(iardy);
        free(gr_stats); free(as);
    }
    seg->result=im;
    { sj_page *pg=&ctx->pages[ctx->cur_page];
      if(pg->image) sj_img_compose(pg->image,im,(int)SBX,(int)SBY,(sj_compose_op)SBCOMBOP);
    }
    return 0;
}

/* --- Symbol dictionary (segment type 0) --- */
static int sj_decode_sym_dict(stb_jbig2_context *ctx, sj_seg *seg, const sj_u8 *sd) {
    sj_sym_dict *dict; sj_u32 num_syms, num_new_syms, num_ex_syms, offset;
    sj_u16 flags; int sdhuff, sdrefagg, sdtemplate, sdrtemplate;
    int sdat_bytes; sj_u32 i;
    (void)ctx;
    if(seg->data_len<10) return -1;
    flags=sj_get16(sd);
    sdhuff=flags&1; sdrefagg=(flags>>1)&1;
    sdtemplate=(flags>>10)&3; sdrtemplate=(flags>>12)&1;
    sdat_bytes=sdhuff?0:(sdtemplate==0?8:2);
    offset=2+(sj_u32)sdat_bytes;
    if(sdrefagg&&!sdrtemplate) offset+=4;
    if(offset+8>seg->data_len) return -1;
    num_ex_syms=sj_get32(sd+offset);
    num_new_syms=sj_get32(sd+offset+4);
    offset+=8;
    num_syms=num_ex_syms;
    dict=(sj_sym_dict*)calloc(1,sizeof(*dict)); if(!dict) return -1;
    dict->n_symbols=num_syms;
    dict->glyphs=(stb_jbig2_image**)calloc(num_syms?num_syms:1,sizeof(stb_jbig2_image*));
    if(!dict->glyphs){free(dict);return -1;}
    if(sdhuff) {
        for(i=0;i<num_new_syms;i++) {
            dict->glyphs[i]=sj_img_new(1,1);
            if(dict->glyphs[i]) sj_img_clear(dict->glyphs[i],0);
        }
    } else {
        sj_arith *as; sj_cx *gb_stats, *gr_stats;
        int gb_sz=sdtemplate==0?65536:sdtemplate==1?8192:1024;
        int gr_sz=sdrtemplate?1024:8192;
        sj_u32 nsyms_decoded=0, hc_height=0;
        sj_u32 sym_width=0;
        sj_int_ctx *iadw, *iaex, *iaai;
        sj_int_ctx *iadh;
        gb_stats=(sj_cx*)calloc(gb_sz,sizeof(sj_cx));
        gr_stats=(sj_cx*)calloc(gr_sz,sizeof(sj_cx));
        if(!gb_stats||!gr_stats){free(gb_stats);free(gr_stats);free(dict->glyphs);free(dict);return -1;}
        as=sj_arith_new(sd+offset,seg->data_len-offset);
        if(!as){free(gb_stats);free(gr_stats);free(dict->glyphs);free(dict);return -1;}
        iadh=sj_int_ctx_new(); iadw=sj_int_ctx_new();
        iaex=sj_int_ctx_new(); iaai=sj_int_ctx_new();
        hc_height=0; nsyms_decoded=0;
        while(nsyms_decoded<num_new_syms) {
            sj_i32 hcdh; sj_u32 dw;
            { int rc=sj_int_decode(iadh,as,&hcdh);
              if(rc<0) goto sym_done;
              if(rc>0) goto sym_done;
            }            hc_height=(sj_u32)((sj_i32)hc_height+hcdh); sym_width=0;
            for(;;) {
                sj_i32 idw;
                { int rc=sj_int_decode(iadw,as,&idw);
                  if(rc<0) goto sym_done;
                  if(rc>0) break;
                }
                dw=(sj_u32)idw; sym_width+=dw;
                if(nsyms_decoded<num_new_syms) {
                    sj_i32 refagg_ninst=0;
                    if(!sdrefagg) {
                        sj_i8 gbat[8]={0}; int tpl=sdtemplate;
                        stb_jbig2_image *glyph;
                        sj_u32 stride;
                        { int j; for(j=0;j<sdat_bytes&&j<8;j++) gbat[j]=(sj_i8)sd[2+j]; }
                        glyph=sj_img_new(sym_width,hc_height);
                        if(!glyph) goto sym_done;
                        stride=(sym_width+7)>>3;
                        memset(glyph->data,0,(size_t)stride*hc_height);
                        { int rc=sj_decode_gb(glyph,as,gb_stats,tpl,0,gbat); if(rc<0){sj_img_release(glyph);goto sym_done;} }
                        dict->glyphs[nsyms_decoded]=glyph;
                    } else {
                        { int rc=sj_int_decode(iaai,as,&refagg_ninst); if(rc<0) goto sym_done; if(rc>0) goto sym_done; }
                        if(refagg_ninst==1) {
                            sj_i32 id,rdx,rdy;
                            { int rc=sj_int_decode(iaai,as,&id); if(rc<0)goto sym_done; }
                            { int rc=sj_int_decode(iadw,as,&rdx); if(rc<0)goto sym_done; }
                            { int rc=sj_int_decode(iadw,as,&rdy); if(rc<0)goto sym_done; }
                            if(id>=0 && (sj_u32)id<nsyms_decoded && dict->glyphs[id]) {
                                stb_jbig2_image *glyph=sj_img_new(sym_width,hc_height);
                                if(glyph) { sj_img_clear(glyph,0); sj_img_compose(glyph,dict->glyphs[id],(int)rdx,(int)rdy,SJ_COMPOSE_OR); dict->glyphs[nsyms_decoded]=glyph; }
                            } else { dict->glyphs[nsyms_decoded]=sj_img_new(sym_width,hc_height); }
                        } else {
                            dict->glyphs[nsyms_decoded]=sj_img_new(sym_width,hc_height);
                        }
                        if(dict->glyphs[nsyms_decoded]) sj_img_clear(dict->glyphs[nsyms_decoded],0);
                    }
                } else {
                    break;
                }
                nsyms_decoded++;
            }
        }
sym_done:
        (void)nsyms_decoded;
        sj_int_ctx_free(iadh); sj_int_ctx_free(iadw);
        sj_int_ctx_free(iaex); sj_int_ctx_free(iaai);
        free(as); free(gb_stats); free(gr_stats);
    }
    seg->result=dict;
    return 0;
}

/* --- Pattern dictionary (segment type 16) --- */
static int sj_decode_pat_dict(stb_jbig2_context *ctx, sj_seg *seg, const sj_u8 *sd) {
    sj_pat_dict *dict; sj_u32 i;
    sj_u8 flags; int hdmmr, hdtemplate;
    sj_u32 hdpw, hdph, graymax, n;
    stb_jbig2_image *image; sj_arith *as; sj_cx *stats;
    int ss; sj_i8 gbat[8];
    if(seg->data_len<7) return -1;
    flags=sd[0];
    hdmmr=flags&1; hdtemplate=(flags>>1)&3;
    hdpw=sd[1]; hdph=sd[2];
    graymax=sj_get32(sd+3);
    n=graymax+1;
    dict=(sj_pat_dict*)calloc(1,sizeof(*dict)); if(!dict) return -1;
    dict->HPW=hdpw; dict->HPH=hdph;
    dict->n=(int)n;
    dict->patterns=(stb_jbig2_image**)calloc(n?n:1,sizeof(stb_jbig2_image*));
    if(!dict->patterns){free(dict);return -1;}
    for(i=0;i<n;i++) {
        dict->patterns[i]=sj_img_new(hdpw,hdph);
        if(dict->patterns[i]) sj_img_clear(dict->patterns[i],0);
    }
    if(hdmmr) { seg->result=dict; return 0; }
    /* Decode collective bitmap using arithmetic-coded generic region */
    image=sj_img_new(hdpw*n,hdph);
    if(!image){seg->result=dict;return 0;}
    ss=sj_gensize(hdtemplate);
    stats=(sj_cx*)calloc(ss,sizeof(sj_cx));
    if(!stats){sj_img_release(image);seg->result=dict;return 0;}
    /* Template: gbat[0]=-hdpw, gbat[1]=0, rest per spec 6.7.5 */
    gbat[0]=(sj_i8)(-(int)hdpw); gbat[1]=0;
    gbat[2]=-3; gbat[3]=-1;
    gbat[4]=2; gbat[5]=-2;
    gbat[6]=-2; gbat[7]=-2;
    as=sj_arith_new(sd+7,seg->data_len-7);
    if(as) {
        sj_decode_gb(image,as,stats,hdtemplate,0,gbat);
        free(as);
    }
    free(stats);
    /* Copy out individual patterns from the collective bitmap */
    for(i=0;i<n;i++) {
        sj_u32 px,py;
        if(!dict->patterns[i]) continue;
        for(py=0;py<hdph;py++) {
            for(px=0;px<hdpw;px++) {
                sj_img_setpixel(dict->patterns[i],(int)px,(int)py,
                    sj_img_getpixel(image,(int)(i*hdpw+px),(int)py));
            }
        }
    }
    sj_img_release(image);
    seg->result=dict;
    return 0;
}

/* --- Halftone region (segment types 20,22,23) --- */
static int sj_decode_halftone(stb_jbig2_context *ctx, sj_seg *seg, const sj_u8 *sd) {
    sj_u32 GBW,GBH,X,Y; sj_u32 comp_op;
    sj_u32 HMMR,HTEMPLATE,HENABLESKIP,HCOMBOP,HDEFPIXEL;
    sj_u32 HGW,HGH;
    sj_i32 HGX,HGY;
    sj_u16 HRX,HRY;
    sj_u32 offset;
    stb_jbig2_image *im;
    sj_pat_dict *hpats=NULL;
    sj_u32 i;
    if(seg->data_len<38) return -1;
    /* 7.4.5.1 - Region segment info (17 bytes) */
    GBW=sj_get32(sd); GBH=sj_get32(sd+4); X=sj_get32(sd+8); Y=sj_get32(sd+12);
    comp_op=sd[16]&7;
    offset=17;
    /* 7.4.5.1.1 - Halftone flags */
    { sj_u8 hf=sd[offset]; HMMR=hf&1; HTEMPLATE=(hf>>1)&3; HENABLESKIP=(hf>>3)&1;
      HCOMBOP=(hf>>4)&7; HDEFPIXEL=(hf>>7)&1; }
    offset++;
    /* 7.4.5.1.2 - Grid size and offset */
    HGW=sj_get32(sd+offset); HGH=sj_get32(sd+offset+4);
    { sj_u32 hx=sj_get32(sd+offset+8), hy=sj_get32(sd+offset+12);
      HGX=(sj_i32)hx; HGY=(sj_i32)hy; }
    offset+=16;
    /* 7.4.5.1.3 - Rotation vector */
    HRX=sj_get16(sd+offset); HRY=sj_get16(sd+offset+2);
    offset+=4;
    /* Get pattern dictionary from referred-to segments */
    for(i=0;i<(sj_u32)seg->ref_seg_count&&!hpats;i++) {
        sj_seg *rseg=NULL;
        sj_u32 j;
        for(j=0;j<ctx->n_segs;j++) {
            if(ctx->segs[j]->number==seg->ref_segs[i]){rseg=ctx->segs[j];break;}
        }
        if(rseg&&(rseg->flags&63)==16&&rseg->result) hpats=(sj_pat_dict*)rseg->result;
    }
    if(!hpats||hpats->n==0) return -1;
    /* Create halftone image */
    im=sj_img_new(GBW,GBH); if(!im) return -1;
    /* 6.6.5(1): Fill with HDEFPIXEL */
    sj_img_clear(im,(int)HDEFPIXEL);
    if(HMMR) { /* MMR not supported yet */ sj_img_release(im); return -1; }
    {
        sj_u32 HBPP=0, HNUMPATS=(sj_u32)hpats->n;
        sj_u32 gsstride;
        sj_u8 **gsplanes=NULL;
        sj_u16 **GI=NULL;
        sj_u32 ng,mg;
        /* 6.6.5(3): HBPP = ceil(log2(HNUMPATS)) */
        { sj_u32 tmp=HNUMPATS; while(tmp>(1U<<HBPP)) HBPP++; }
        if(HBPP>16) { sj_img_release(im); return -1; }
        if(HBPP==0) HBPP=1;
        /* 6.6.5(4): Decode gray-scale image (annex C.5) */
        gsstride=((HGW+7)>>3);
        gsplanes=(sj_u8**)calloc(HBPP,sizeof(sj_u8*));
        if(!gsplanes){sj_img_release(im);return -1;}
        for(i=0;i<HBPP;i++) {
            gsplanes[i]=(sj_u8*)calloc((size_t)gsstride*HGH,1);
            if(!gsplanes[i]){sj_u32 j; for(j=0;j<i;j++) free(gsplanes[j]); free(gsplanes); sj_img_release(im); return -1;}
        }
        /* C.5 step 1: Decode each bitplane */
        { int gb_ss=sj_gensize(HTEMPLATE);
          sj_cx *gb_stats=(sj_cx*)calloc(gb_ss,sizeof(sj_cx));
          sj_i8 gb_gbat[8];
          gb_gbat[0]=(HTEMPLATE<=1)?3:2; gb_gbat[1]=-1;
          gb_gbat[2]=-3; gb_gbat[3]=-1;
          gb_gbat[4]=2; gb_gbat[5]=-2;
          gb_gbat[6]=-2; gb_gbat[7]=-2;
          if(gb_stats) {
              sj_arith *as=sj_arith_new(sd+offset,seg->data_len-offset);
              if(as) {
                  /* C.5 step 1: Decode GSPLANES[GSBPP-1] first, then GSBPP-2 down to 0 */
                  { sj_u32 bp;
                    for(bp=0;bp<HBPP;bp++) {
                        stb_jbig2_image plane;
                        sj_u32 idx=HBPP-1-bp;
                        plane.width=HGW; plane.height=HGH;
                        plane.stride=gsstride; plane.data=gsplanes[idx]; plane.refcount=0;
                        sj_decode_gb(&plane,as,gb_stats,(int)HTEMPLATE,0,gb_gbat);
                    }
                  }
                  free(as);
              }
              free(gb_stats);
          }
        }
        /* C.5 step 3b: XOR consecutive bitplanes */
        for(i=HBPP-1;i>0;i--) {
            sj_u32 sz=(size_t)gsstride*HGH;
            sj_u32 k;
            for(k=0;k<sz;k++) gsplanes[i-1][k]^=gsplanes[i][k];
        }
        /* C.5 step 4: Build GSVALS from bitplanes */
        GI=(sj_u16**)calloc(HGW,sizeof(sj_u16*));
        if(GI) {
            for(ng=0;ng<HGW;ng++) {
                GI[ng]=(sj_u16*)calloc(HGH,sizeof(sj_u16));
                if(!GI[ng]){sj_u32 k; for(k=0;k<ng;k++) free(GI[k]); free(GI); GI=NULL; break;}
                for(mg=0;mg<HGH;mg++) {
                    sj_u16 val=0;
                    for(i=0;i<HBPP;i++) {
                        val+=(sj_u16)((gsplanes[i][mg*gsstride+(ng>>3)]>>(7-(ng&7)))&1)<<i;
                    }
                    GI[ng][mg]=val;
                }
            }
        }
        /* Free bitplanes */
        for(i=0;i<HBPP;i++) free(gsplanes[i]);
        free(gsplanes);
        /* 6.6.5(5): Place patterns */
        if(GI) {
            for(mg=0;mg<HGH;mg++) {
                for(ng=0;ng<HGW;ng++) {
                    sj_u16 gv=GI[ng][mg];
                    sj_u32 px,py;
                    sj_i32 gx,gy;
                    if(gv>=HNUMPATS) gv=(sj_u16)(HNUMPATS-1);
                    if(!hpats->patterns[gv]) continue;
                    /* Grid position: (HGX + mg*HRY + ng*HRX) >> 8 */
                    { sj_i32 hrx=(sj_i32)HRX, hry=(sj_i32)HRY;
                      sj_i32 mx=(sj_i32)mg, nx=(sj_i32)ng;
                      gx = (sj_i32)(((HGX + mx*hry + nx*hrx) >> 8));
                      gy = (sj_i32)(((HGY + mx*hrx - nx*hry) >> 8));
                    }
                    /* Compose pattern onto halftone image */
                    for(py=0;py<hpats->HPH;py++) {
                        int dyy=(int)(gy+py);
                        if(dyy<0||dyy>=(int)GBH) continue;
                        for(px=0;px<hpats->HPW;px++) {
                            int dxx=(int)(gx+px);
                            if(dxx<0||dxx>=(int)GBW) continue;
                            if(sj_img_getpixel(hpats->patterns[gv],(int)px,(int)py)) {
                                sj_img_setpixel(im,dxx,dyy,1);
                            }
                        }
                    }
                }
            }
            for(ng=0;ng<HGW;ng++) free(GI[ng]);
            free(GI);
        }
    }
    seg->result=im;
    { sj_page *pg=&ctx->pages[ctx->cur_page];
      if(pg->image) sj_img_compose(pg->image,im,(int)X,(int)Y,(sj_compose_op)comp_op);
    }
    return 0;
}

/* --- Refinement region (simplified) --- */
static int sj_decode_refinement(stb_jbig2_context *ctx, sj_seg *seg, const sj_u8 *sd) {
    sj_u32 GRW,GRH;
    stb_jbig2_image *im;
    if(seg->data_len<17) return -1;
    GRW=sj_get32(sd); GRH=sj_get32(sd+4);
    im=sj_img_new(GRW,GRH); if(!im) return -1;
    sj_img_clear(im,0);
    seg->result=im;
    /* Simplified: compose OR onto page */
    { sj_page *pg=&ctx->pages[ctx->cur_page];
      if(pg->image) sj_img_compose(pg->image,im,0,0,SJ_COMPOSE_OR);
    }
    return 0;
}

/* --- Segment dispatch --- */
static int sj_parse_seg(stb_jbig2_context *ctx, sj_seg *seg, const sj_u8 *sd) {
    (void)ctx;
    switch(seg->flags&63) {
        case 0:  return sj_decode_sym_dict(ctx,seg,sd);
        case 4: case 6: case 7:  return sj_decode_text(ctx,seg,sd);
        case 16: return sj_decode_pat_dict(ctx,seg,sd);
        case 20: case 22: case 23: return sj_decode_halftone(ctx,seg,sd);
        case 38: case 39: return sj_decode_imm_gen(ctx,seg,sd);
        case 40: case 42: case 43: return sj_decode_refinement(ctx,seg,sd);
        case 48: return sj_page_info(ctx,seg,sd);
        case 49: return sj_end_of_page(ctx,seg,sd);
        case 50: return sj_end_of_stripe(ctx,seg,sd);
        case 51: ctx->state=SJ_FILE_EOF; return 0;
        case 53: return 0; /* user huffman table - skip for now */
    }
    return 0;
}

/* --- Main decode loop --- */
static int sj_data_in(stb_jbig2_context *ctx, const sj_u8 *data, size_t size) {
    static const sj_u8 jbig2_id[8]={0x97,0x4a,0x42,0x32,0x0d,0x0a,0x1a,0x0a};
    /* Buffer management */
    if (!ctx->buf) {
        size_t sz=1024; while(sz<size) sz<<=1;
        ctx->buf=(sj_u8*)malloc(sz); if(!ctx->buf) return -1;
        ctx->buf_size=sz; ctx->buf_rd=0; ctx->buf_wr=0;
    } else if (size>ctx->buf_size-ctx->buf_wr) {
        size_t have=ctx->buf_wr-ctx->buf_rd; size_t sz;
        if(ctx->buf_rd<=(ctx->buf_size>>1)&&size<=ctx->buf_size-have) {
            memmove(ctx->buf,ctx->buf+ctx->buf_rd,have);
        } else {
            sj_u8 *nb; sz=have; while(sz<size+have) sz<<=1;
            nb=(sj_u8*)malloc(sz); if(!nb) return -1;
            memcpy(nb,ctx->buf+ctx->buf_rd,have); free(ctx->buf);
            ctx->buf=nb; ctx->buf_size=sz;
        }
        ctx->buf_wr-=ctx->buf_rd; ctx->buf_rd=0;
    }
    memcpy(ctx->buf+ctx->buf_wr,data,size); ctx->buf_wr+=size;

    for (;;) {
        size_t avail=ctx->buf_wr-ctx->buf_rd;
        /* Safety: if no progress, break */
        if (avail == 0 && ctx->state != SJ_FILE_HDR && ctx->state != SJ_FILE_EOF &&
            ctx->state != SJ_FILE_RND_BODY && ctx->state != SJ_FILE_SEQ_BODY) return 0;
        switch(ctx->state) {
        case SJ_FILE_HDR:
            if(avail<9) return 0;
            if(memcmp(ctx->buf+ctx->buf_rd,jbig2_id,8)) return -1;
            ctx->hdr_flags=ctx->buf[ctx->buf_rd+8];
            if(!(ctx->hdr_flags&2)) { if(avail<13)return 0; ctx->n_pages=sj_get32(ctx->buf+ctx->buf_rd+9); ctx->buf_rd+=13; }
            else { ctx->n_pages=0; ctx->buf_rd+=9; }
            ctx->state=(ctx->hdr_flags&1)?SJ_FILE_SEQ_HDR:SJ_FILE_RND_HDR;
            break;
        case SJ_FILE_SEQ_HDR: case SJ_FILE_RND_HDR: {
            sj_seg *seg; size_t hdr_sz;
            seg=sj_parse_seg_hdr(ctx,ctx->buf+ctx->buf_rd,avail,&hdr_sz);
            if(!seg) {
                if(ctx->state==SJ_FILE_RND_HDR && ctx->n_segs>0) {
                    ctx->state=SJ_FILE_RND_BODY;
                    break;
                }
                return 0;
            }
            ctx->buf_rd+=hdr_sz;
            if(ctx->n_segs>=ctx->seg_max) {
                sj_seg **ss; ctx->seg_max<<=2;
                ss=(sj_seg**)realloc(ctx->segs,ctx->seg_max*sizeof(sj_seg*));
                if(!ss){sj_free_seg(seg);ctx->state=SJ_FILE_EOF;return -1;}
                ctx->segs=ss;
            }
            ctx->segs[ctx->n_segs++]=seg;
            if(ctx->state==SJ_FILE_RND_HDR) {
                if((seg->flags&63)==51) ctx->state=SJ_FILE_RND_BODY;
            } else {
                ctx->state=SJ_FILE_SEQ_BODY;
            }
            break;
        }
        case SJ_FILE_SEQ_BODY: case SJ_FILE_RND_BODY: {
            sj_seg *seg=ctx->segs[ctx->seg_idx];
            if(seg->data_len>avail) return 0;
            sj_parse_seg(ctx,seg,ctx->buf+ctx->buf_rd);
            ctx->buf_rd+=seg->data_len; ctx->seg_idx++;
            if(ctx->state==SJ_FILE_RND_BODY&&ctx->seg_idx==ctx->n_segs) ctx->state=SJ_FILE_EOF;
            else if(ctx->state==SJ_FILE_SEQ_BODY) ctx->state=SJ_FILE_SEQ_HDR;
            break;
        }
        case SJ_FILE_EOF:
            return 0;
        }
    }
}

/* --- Public API --- */

stb_jbig2_context *stb_jbig2_create_ex(int options, stb_jbig2_context *shared) {
    stb_jbig2_context *ctx=(stb_jbig2_context*)calloc(1,sizeof(*ctx));
    if(!ctx) return NULL;
    (void)shared;
    ctx->state=(options&STB_JBIG2_OPTION_EMBEDDED)?SJ_FILE_SEQ_HDR:SJ_FILE_HDR;
    ctx->seg_max=16; ctx->segs=(sj_seg**)calloc(ctx->seg_max,sizeof(sj_seg*));
    if(!ctx->segs){free(ctx);return NULL;}
    ctx->max_page=4; ctx->pages=(sj_page*)calloc(ctx->max_page,sizeof(sj_page));
    if(!ctx->segs){free(ctx->segs);free(ctx);return NULL;}
    { unsigned i; for(i=0;i<ctx->max_page;i++){ctx->pages[i].state=SJ_PAGE_FREE;ctx->pages[i].image=NULL;} }
    return ctx;
}

stb_jbig2_context *stb_jbig2_create(stb_jbig2_context *shared) { return stb_jbig2_create_ex(0,shared); }

void stb_jbig2_destroy(stb_jbig2_context *ctx) {
    sj_u32 i;
    if(!ctx) return;
    free(ctx->buf);
    for(i=0;i<ctx->n_segs;i++) sj_free_seg(ctx->segs[i]);
    free(ctx->segs);
    for(i=0;i<=ctx->cur_page;i++) if(ctx->pages[i].image) sj_img_release(ctx->pages[i].image);
    free(ctx->pages); free(ctx);
}

int stb_jbig2_submit(stb_jbig2_context *ctx, const unsigned char *data, int size) {
    return sj_data_in(ctx,data,size);
}

stb_jbig2_image *stb_jbig2_page_out(stb_jbig2_context *ctx) {
    sj_u32 i;
    for(i=0;i<ctx->max_page;i++) {
        if(ctx->pages[i].state==SJ_PAGE_COMPLETE) {
            stb_jbig2_image *img=ctx->pages[i].image;
            if(!img) continue;
            ctx->pages[i].state=SJ_PAGE_RETURNED;
            return sj_img_ref(img);
        }
    }
    return NULL;
}

void stb_jbig2_release_page(stb_jbig2_context *ctx, stb_jbig2_image *img) {
    sj_u32 i;
    if(!img) return;
    for(i=0;i<ctx->max_page;i++) {
        if(ctx->pages[i].image==img) {
            sj_img_release(img);
            ctx->pages[i].state=SJ_PAGE_RELEASED;
            return;
        }
    }
}

int stb_jbig2_image_width(stb_jbig2_image *img) { return img?(int)img->width:0; }
int stb_jbig2_image_height(stb_jbig2_image *img) { return img?(int)img->height:0; }
int stb_jbig2_image_stride(stb_jbig2_image *img) { return img?(int)img->stride:0; }
unsigned char *stb_jbig2_image_data(stb_jbig2_image *img) { return img?img->data:NULL; }
int stb_jbig2_image_getpixel(stb_jbig2_image *img, int x, int y) {
    if(!img||x<0||y<0||x>=img->width||y>=img->height) return 0;
    return (img->data[(y*img->stride)+(x>>3)]>>(7-(x&7)))&1;
}

int stb_jbig2_complete_page(stb_jbig2_context *ctx) {
    if(ctx->pages[ctx->cur_page].image==NULL) return -1;
    ctx->pages[ctx->cur_page].state=SJ_PAGE_COMPLETE;
    return 0;
}

/* --- One-shot decode --- */
unsigned char *stb_jbig2_decode(const unsigned char *data, int size, int *width, int *height) {
    stb_jbig2_context *ctx; stb_jbig2_image *img; unsigned char *out;
    ctx=stb_jbig2_create(NULL); if(!ctx) return NULL;
    stb_jbig2_submit(ctx,data,size);
    img=stb_jbig2_page_out(ctx);
    stb_jbig2_destroy(ctx);
    if(!img) return NULL;
    *width=(int)img->width; *height=(int)img->height;
    out=(unsigned char*)malloc(img->stride*img->height);
    if(out) memcpy(out,img->data,img->stride*img->height);
    sj_img_release(img);
    return out;
}

unsigned char *stb_jbig2_decode_embedded(const unsigned char *data, int size, int *width, int *height) {
    stb_jbig2_context *ctx; stb_jbig2_image *img; unsigned char *out;
    ctx=stb_jbig2_create_ex(STB_JBIG2_OPTION_EMBEDDED,NULL); if(!ctx) return NULL;
    stb_jbig2_submit(ctx,data,size);
    img=stb_jbig2_page_out(ctx);
    stb_jbig2_destroy(ctx);
    if(!img) return NULL;
    *width=(int)img->width; *height=(int)img->height;
    out=(unsigned char*)malloc(img->stride*img->height);
    if(out) memcpy(out,img->data,img->stride*img->height);
    sj_img_release(img);
    return out;
}

void stb_jbig2_free(void *p) { free(p); }

unsigned char *stb_jbig2_decode_file(const char *filename, int *width, int *height) {
    FILE *f; unsigned char *data; long size;
    unsigned char *bits, *rgb;
    int w, h, x, y;
    f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return NULL; }
    data = (unsigned char *)malloc((size_t)size);
    if (!data) { fclose(f); return NULL; }
    if ((long)fread(data, 1, (size_t)size, f) != size) { free(data); fclose(f); return NULL; }
    fclose(f);
    bits = stb_jbig2_decode(data, (int)size, &w, &h);
    free(data);
    if (!bits) return NULL;
    rgb = (unsigned char *)malloc((size_t)w * (size_t)h * 3);
    if (rgb) {
        int stride = (w + 7) >> 3;
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                int bit = (bits[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1;
                unsigned char c = bit ? 0 : 255;
                int off = (y * w + x) * 3;
                rgb[off] = c; rgb[off+1] = c; rgb[off+2] = c;
            }
        }
    }
    stb_jbig2_free(bits);
    *width = w; *height = h;
    return rgb;
}

#endif /* STB_JBIG2__IMPLEMENTATION_ONCE */
#endif /* STB_JBIG2_IMPLEMENTATION */

/*
** stb_jbig2.h is dual licensed under either of:
**   - The Unlicense (public domain)
**   - MIT License
** See end of file for full text.
*/

/*
** This is free and unencumbered software released into the public domain.
**
** Anyone is free to copy, modify, publish, use, compile, sell, or distribute
** this software, either in source code form or as a compiled binary, for any
** purpose, commercial or non-commercial, and by any means.
**
** In jurisdictions that recognize copyright laws, the author or authors of
** this software dedicate any and all copyright interest in the software to the
** public domain. We make this dedication for the benefit of the public at
** large and to the detriment of our heirs and successors. We intend this
** dedication to be an overt act of relinquishment in perpetuity of all present
** and future rights to this software under copyright law.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
** ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
** WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
