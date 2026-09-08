/* stb_jbig.h - v1.0 - JBIG1 decoder in stb_image style - public domain

   Do this:
      #define STB_JBIG_IMPLEMENTATION
   before you include this file in *one* C or C++ file to create the implementation.

   QUICK NOTES:
      - JBIG1 (ISO 11544:1993 / ITU-T T.82) decoder only
      - Single-header library, stb_image style
      - Decodes bi-level images with optional multi-plane support
      - Supports progressive (multi-layer) JBIG streams
      - Pure C89 implementation

   Basic usage:
      int x, y, n;
      unsigned char *data = stbi_jbig_load_from_memory(buf, len, &x, &y, &n);
      // data is 1-bit-per-pixel bitmap, packed MSB-first
      // n is number of planes
      // x is width, y is height
      stbi_jbig_free(data);

LICENSE
   See end of file for license information.
*/

#ifndef STBI_INCLUDE_STB_JBIG_H
#define STBI_INCLUDE_STB_JBIG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STBJBIGDEF
#ifdef STB_JBIG_STATIC
#define STBJBIGDEF static
#else
#define STBJBIGDEF extern
#endif
#endif

/* Opaque decoder context */
typedef struct _stbi_jbig_context stbi_jbig_context;

/* Decode JBIG data from file path.
   Returns a bitmap with 1 bit per pixel, packed MSB-first.
   *x = width, *y = height, *planes = number of bit planes.
   Returns NULL on failure. */
STBJBIGDEF unsigned char *stbi_jbig_load_from_file(const char *path,
    int *x, int *y, int *planes);

/* Decode JBIG data from memory buffer.
   Returns a bitmap with 1 bit per pixel, packed MSB-first.
   *x = width, *y = height, *planes = number of bit planes.
   Returns NULL on failure. */
STBJBIGDEF unsigned char *stbi_jbig_load_from_memory(
    const unsigned char *buffer, int len,
    int *x, int *y, int *planes);

/* Decode JBIG data from memory, returning RGB24 (3 bytes per pixel).
   Black pixels = (0,0,0), white pixels = (255,255,255).
   *x = width, *y = height.
   Returns NULL on failure. Caller must free with stbi_jbig_free(). */
STBJBIGDEF unsigned char *stbi_jbig_load_rgb_from_memory(
    const unsigned char *buffer, int len,
    int *x, int *y);

/* Decode JBIG data from file path, returning RGB24 (3 bytes per pixel).
   Black pixels = (0,0,0), white pixels = (255,255,255).
   *x = width, *y = height.
   Returns NULL on failure. Caller must free with stbi_jbig_free(). */
STBJBIGDEF unsigned char *stbi_jbig_load_rgb_from_file(const char *path,
    int *x, int *y);

/* Free memory allocated by stbi_jbig_load* */
STBJBIGDEF void stbi_jbig_free(void *ptr);

/* Get last error message */
STBJBIGDEF const char *stbi_jbig_failure_reason(void);

/* Incremental decode API */
STBJBIGDEF void stbi_jbig_init_context(stbi_jbig_context *s);
STBJBIGDEF int  stbi_jbig_in(stbi_jbig_context *s,
                             const unsigned char *data, size_t len,
                             size_t *bytes_read);
STBJBIGDEF int  stbi_jbig_get_width(const stbi_jbig_context *s);
STBJBIGDEF int  stbi_jbig_get_height(const stbi_jbig_context *s);
STBJBIGDEF int  stbi_jbig_get_planes(const stbi_jbig_context *s);
STBJBIGDEF unsigned char *stbi_jbig_get_image(stbi_jbig_context *s);
STBJBIGDEF void stbi_jbig_destroy(stbi_jbig_context *s);

/* Check if data starts with JBIG magic */
STBJBIGDEF int stbi_jbig_test_memory(const unsigned char *buffer, int len);

#ifdef __cplusplus
}
#endif

#endif /* STBI_INCLUDE_STB_JBIG_H */

/* ======================================================================== */
/*                         IMPLEMENTATION                                    */
/* ======================================================================== */

#ifdef STB_JBIG_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifndef STBJBIG_MALLOC
#define STBJBIG_MALLOC(sz) malloc(sz)
#define STBJBIG_REALLOC(p,newsz) realloc(p,newsz)
#define STBJBIG_FREE(p) free(p)
#endif

/* error strings */
static const char *stbi__jbig_g_failure_reason;

STBJBIGDEF const char *stbi_jbig_failure_reason(void)
{
    return stbi__jbig_g_failure_reason;
}

static int stbi__jbig_err(const char *str)
{
    stbi__jbig_g_failure_reason = str;
    return 0;
}

/* ======================================================================== */
/*                    Arithmetic Decoder                                     */
/* ======================================================================== */

/* Probability estimation tables from T.82 Table 24 */
static const unsigned short jbig__lsztab[113] = {
    0x5a1d, 0x2586, 0x1114, 0x080b, 0x03d8, 0x01da, 0x00e5, 0x006f,
    0x0036, 0x001a, 0x000d, 0x0006, 0x0003, 0x0001, 0x5a7f, 0x3f25,
    0x2cf2, 0x207c, 0x17b9, 0x1182, 0x0cef, 0x09a1, 0x072f, 0x055c,
    0x0406, 0x0303, 0x0240, 0x01b1, 0x0144, 0x00f5, 0x00b7, 0x008a,
    0x0068, 0x004e, 0x003b, 0x002c, 0x5ae1, 0x484c, 0x3a0d, 0x2ef1,
    0x261f, 0x1f33, 0x19a8, 0x1518, 0x1177, 0x0e74, 0x0bfb, 0x09f8,
    0x0861, 0x0706, 0x05cd, 0x04de, 0x040f, 0x0363, 0x02d4, 0x025c,
    0x01f8, 0x01a4, 0x0160, 0x0125, 0x00f6, 0x00cb, 0x00ab, 0x008f,
    0x5b12, 0x4d04, 0x412c, 0x37d8, 0x2fe8, 0x293c, 0x2379, 0x1edf,
    0x1aa9, 0x174e, 0x1424, 0x119c, 0x0f6b, 0x0d51, 0x0bb6, 0x0a40,
    0x5832, 0x4d1c, 0x438e, 0x3bdd, 0x34ee, 0x2eae, 0x299a, 0x2516,
    0x5570, 0x4ca9, 0x44d9, 0x3e22, 0x3824, 0x32b4, 0x2e17, 0x56a8,
    0x4f46, 0x47e5, 0x41cf, 0x3c3d, 0x375e, 0x5231, 0x4c0f, 0x4639,
    0x415e, 0x5627, 0x50e7, 0x4b85, 0x5597, 0x504f, 0x5a10, 0x5522,
    0x59eb
};

static const unsigned char jbig__nmpstab[113] = {
     1,   2,   3,   4,   5,   6,   7,   8,
     9,  10,  11,  12,  13,  13,  15,  16,
    17,  18,  19,  20,  21,  22,  23,  24,
    25,  26,  27,  28,  29,  30,  31,  32,
    33,  34,  35,   9,  37,  38,  39,  40,
    41,  42,  43,  44,  45,  46,  47,  48,
    49,  50,  51,  52,  53,  54,  55,  56,
    57,  58,  59,  60,  61,  62,  63,  32,
    65,  66,  67,  68,  69,  70,  71,  72,
    73,  74,  75,  76,  77,  78,  79,  48,
    81,  82,  83,  84,  85,  86,  87,  71,
    89,  90,  91,  92,  93,  94,  86,  96,
    97,  98,  99, 100,  93, 102, 103, 104,
    99, 106, 107, 103, 109, 107, 111, 109,
   111
};

/* nlpstab: NLPS in bits[6:0], SWTCH in bit[7] */
static const unsigned char jbig__nlpstab[113] = {
   129,  14,  16,  18,  20,  23,  25,  28,
    30,  33,  35,   9,  10,  12, 143,  36,
    38,  39,  40,  42,  43,  45,  46,  48,
    49,  51,  52,  54,  56,  57,  59,  60,
    62,  63,  32,  33, 165,  64,  65,  67,
    68,  69,  70,  72,  73,  74,  75,  77,
    78,  79,  48,  50,  50,  51,  52,  53,
    54,  55,  56,  57,  58,  59,  61,  61,
   193,  80,  81,  82,  83,  84,  86,  87,
    87,  72,  72,  74,  74,  75,  77,  77,
   208,  88,  89,  90,  91,  92,  93,  86,
   216,  95,  96,  97,  99,  99,  93, 223,
   101, 102, 103, 104,  99, 105, 106, 107,
   103, 233, 108, 109, 110, 111, 238, 112,
   240
};

/* Marker codes */
#define JBIG__MARKER_STUFF    0x00
#define JBIG__MARKER_RESERVE  0x01
#define JBIG__MARKER_SDNORM   0x02
#define JBIG__MARKER_SDRST    0x03
#define JBIG__MARKER_ABORT    0x04
#define JBIG__MARKER_NEWLEN   0x05
#define JBIG__MARKER_ATMOVE   0x06
#define JBIG__MARKER_COMMENT  0x07
#define JBIG__MARKER_ESC      0xff

/* Return codes */
#define JBIG__EOK        (0 << 4)
#define JBIG__EOK_INTR   (1 << 4)
#define JBIG__EAGAIN     (2 << 4)
#define JBIG__ENOMEM     (3 << 4)
#define JBIG__EABORT     (4 << 4)
#define JBIG__EMARKER    (5 << 4)
#define JBIG__EINVAL     (6 << 4)
#define JBIG__EIMPL      (7 << 4)
#define JBIG__ENOCONT    (8 << 4)

/* Option flags */
#define JBIG__HITOLO     0x08
#define JBIG__SEQ        0x04
#define JBIG__ILEAVE     0x02
#define JBIG__SMID       0x01

#define JBIG__LRLTWO     0x40
#define JBIG__VLENGTH    0x20
#define JBIG__TPDON      0x10
#define JBIG__TPBON      0x08
#define JBIG__DPON       0x04
#define JBIG__DPPRIV     0x02
#define JBIG__DPLAST     0x01

/* Contexts for TP special pixels */
#define JBIG__TPB2CX  0x195
#define JBIG__TPB3CX  0x0e5
#define JBIG__TPDCX   0xc3f

/* Maximum ATMOVEs per stripe */
#define JBIG__ATMOVES_MAX  64

/* Loop array indices */
#define JBIG__STRIPE  0
#define JBIG__LAYER   1
#define JBIG__PLANE   2

/* SDE ordering index table */
static const int jbig__iindex[8][3] = {
    { 2, 1, 0 },    /* no ordering bit set */
    { -1, -1, -1 },  /* SMID -> illegal */
    { 2, 0, 1 },    /* ILEAVE */
    { 1, 0, 2 },    /* SMID + ILEAVE */
    { 0, 2, 1 },    /* SEQ */
    { 1, 2, 0 },    /* SEQ + SMID */
    { 0, 1, 2 },    /* SEQ + ILEAVE */
    { -1, -1, -1 }  /* SEQ + SMID + ILEAVE -> illegal */
};

/* deterministic prediction table from jbigkit, public domain */
static const unsigned char jbig__dptable[256 + 512 + 2048 + 4096] = {
  /* phase 0: offset=0 */
  0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,
  0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,2,2,2,2,2,2,0,2,2,2,2,2,2,2,
  0,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,2,2,2,2,2,2,0,2,0,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  /* phase 1: offset=256 */
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,
  0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,2,2,2,2,2,0,2,0,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,
  0,2,2,2,2,1,2,1,2,2,2,2,1,1,1,1,2,0,2,0,2,2,2,2,0,2,0,2,2,2,2,2,
  0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,2,2,2,2,0,2,2,2,2,2,2,2,
  0,2,0,2,2,2,2,2,2,2,2,2,2,0,2,0,2,2,0,0,2,2,2,2,2,0,0,2,2,2,2,2,
  0,2,2,2,2,1,2,1,2,2,2,2,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,
  1,2,1,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,1,1,2,2,2,2,2,0,2,2,2,2,2,2,
  2,2,2,2,2,0,2,0,2,2,2,2,0,0,0,0,0,2,0,2,2,2,2,2,0,2,2,2,2,2,2,2,
  0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,0,2,2,2,2,0,2,0,2,2,2,2,2,
  2,2,2,2,2,1,1,1,2,2,2,2,1,1,1,1,1,2,1,2,2,2,2,2,2,2,2,2,2,2,2,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,0,1,2,0,2,0,2,2,2,2,2,0,2,0,2,2,2,2,1,
  0,2,0,2,2,1,2,1,2,2,2,2,1,1,1,1,0,0,0,0,2,2,2,2,0,2,0,2,2,2,2,1,
  2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,0,0,0,2,2,2,2,2,
  2,2,2,2,2,1,2,1,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,1,2,1,2,2,2,2,1,
  2,2,2,2,2,2,2,2,0,2,0,2,2,1,2,2,2,2,2,2,2,2,2,2,0,0,0,2,2,2,2,2,
  /* phase 2: offset=768 */
  2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,2,2,2,2,1,1,1,1,
  0,2,2,2,2,1,2,1,2,2,2,2,1,2,1,2,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,
  2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,2,1,2,1,2,2,2,2,2,1,1,1,
  2,0,2,2,2,1,2,1,0,2,2,2,1,2,1,2,2,2,2,0,2,2,2,2,0,2,0,2,2,2,2,2,
  0,2,0,0,1,1,1,1,2,2,2,2,1,1,1,1,0,2,0,2,1,1,1,1,2,2,2,2,1,1,1,1,
  2,2,0,2,2,2,1,2,2,2,2,2,1,2,1,2,2,2,0,2,2,1,2,1,0,2,0,2,1,1,1,1,
  2,0,0,2,2,2,2,2,0,2,0,2,2,0,2,0,2,0,2,0,2,2,2,1,2,2,0,2,1,1,2,1,
  2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,2,2,2,2,1,1,1,1,
  0,0,0,0,2,2,2,2,0,0,0,0,2,2,2,2,0,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,0,2,2,2,2,1,0,2,2,2,1,1,1,1,2,0,2,2,2,2,2,2,0,2,0,2,2,1,2,1,
  2,0,2,0,2,2,2,2,0,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,
  0,2,2,2,1,2,1,2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,
  2,2,0,2,2,2,2,2,2,2,2,2,2,2,0,2,2,0,0,2,2,1,2,1,0,2,2,2,1,1,1,1,
  2,2,2,0,2,2,2,2,2,2,0,2,2,0,2,0,2,1,2,2,2,2,2,2,1,2,1,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,0,2,2,2,1,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,2,1,1,1,2,2,2,2,1,1,1,1,
  2,2,2,1,2,2,2,2,2,2,1,2,0,0,0,0,2,2,0,2,2,1,2,2,2,2,2,2,1,1,1,1,
  2,0,0,0,2,2,2,2,0,2,2,2,2,2,2,0,2,2,2,0,2,2,2,2,2,0,0,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,0,2,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,1,
  0,2,0,2,2,1,1,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,
  2,0,2,0,2,1,2,1,0,2,0,2,2,2,1,2,2,0,2,0,2,2,2,2,0,2,0,2,2,2,1,2,
  2,2,2,0,2,2,2,2,2,2,0,2,2,2,2,2,2,2,1,2,2,2,2,2,2,0,1,2,2,2,2,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  0,2,2,2,1,2,1,2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,
  2,0,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,2,2,1,2,1,0,2,2,2,1,1,1,1,
  2,0,2,0,2,1,2,2,0,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,2,1,2,2,
  2,0,2,2,2,2,2,2,2,0,2,2,2,2,2,2,2,0,2,0,2,2,2,2,0,0,0,0,2,1,2,1,
  2,2,2,2,2,2,2,0,2,2,2,2,2,2,2,0,2,2,2,2,2,1,2,0,0,2,2,2,1,2,2,2,
  0,0,2,0,2,2,2,2,0,2,0,2,2,0,2,0,1,1,1,2,2,2,2,2,2,2,2,2,2,1,1,1,
  2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,0,2,0,2,2,2,1,
  2,2,0,0,2,2,2,2,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,2,2,2,2,1,1,1,1,
  0,2,2,2,1,2,1,2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,
  2,0,0,2,2,2,2,2,0,2,0,2,2,2,2,2,1,0,1,2,2,2,2,1,0,2,2,2,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,0,2,2,0,2,0,2,1,2,2,2,2,2,2,2,2,0,2,2,1,2,2,
  0,2,0,0,1,1,1,1,0,2,2,2,1,1,1,1,2,2,2,2,2,2,2,2,2,0,2,2,1,2,1,1,
  2,2,0,2,2,1,2,2,2,2,2,2,1,2,2,2,2,0,2,2,2,2,2,2,0,2,0,2,1,2,1,1,
  2,0,2,0,2,2,2,2,0,2,0,2,2,1,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,1,
  2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  0,2,2,2,2,0,2,0,2,2,2,2,0,0,0,0,2,2,2,2,2,1,1,2,2,2,2,2,1,2,2,2,
  2,0,2,2,2,1,2,1,0,2,2,2,2,2,1,2,2,0,2,0,2,2,2,2,0,2,0,2,2,1,2,2,
  0,2,0,0,2,2,2,2,1,2,2,2,2,2,2,0,2,1,2,2,2,2,2,2,1,2,2,2,2,2,2,2,
  0,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,2,1,0,2,2,
  0,0,0,2,2,1,1,1,2,2,2,2,1,2,2,2,2,0,2,0,2,2,2,1,2,2,2,2,1,2,1,2,
  0,0,0,0,2,2,2,2,2,2,0,2,2,1,2,2,2,1,2,1,2,2,2,2,1,2,1,2,0,2,2,2,
  2,0,2,0,2,2,2,2,2,0,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  0,2,2,2,1,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,1,2,2,2,2,2,0,2,2,1,2,2,0,0,0,2,2,2,2,2,1,2,2,0,2,2,2,1,2,1,2,
  2,0,2,0,2,2,2,2,0,2,0,2,2,1,2,2,0,2,0,0,2,2,2,2,2,2,2,2,2,1,2,2,
  2,2,2,2,2,2,2,2,2,0,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,0,2,0,2,2,2,1,
  1,2,0,2,2,1,2,1,2,2,2,2,1,2,2,2,2,0,2,0,2,2,2,2,2,0,2,2,1,1,1,1,
  0,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,0,2,2,1,2,1,
  2,2,0,0,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,
  2,2,2,0,2,2,2,2,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,1,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,2,
  2,0,2,0,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1,0,2,0,2,2,2,1,2,
  2,0,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,
  2,0,2,0,2,2,2,2,2,0,2,0,2,2,2,2,2,0,2,0,2,2,2,2,0,0,0,0,2,1,2,1,
  2,2,2,2,2,1,2,1,0,2,0,2,2,2,2,2,2,0,2,0,2,2,2,2,0,2,0,2,2,2,2,1,
  2,0,2,0,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,0,
  2,0,2,0,2,2,2,1,2,2,2,0,2,2,2,1,2,0,2,0,2,2,2,2,0,0,0,2,2,2,2,1,
  2,0,2,0,2,2,2,2,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,2,
  /* phase 3: offset=2816 */
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,1,2,1,2,0,2,0,1,2,1,2,0,2,0,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,0,2,2,2,1,2,0,2,2,2,1,2,2,2,2,0,2,0,2,1,2,1,0,0,0,0,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,0,2,2,2,1,2,
  2,2,2,1,2,2,2,0,1,1,1,1,0,0,0,0,2,2,2,2,2,2,2,2,2,0,2,0,2,1,2,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,0,0,0,0,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,0,2,1,2,1,0,0,0,0,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,1,2,0,2,0,2,
  2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,0,2,0,2,1,2,1,2,0,2,0,2,1,2,1,
  2,0,0,0,2,1,1,1,0,0,0,0,1,1,1,1,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,
  2,0,2,2,2,1,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,0,0,2,0,1,1,2,1,
  2,2,2,0,2,2,2,1,2,1,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,
  0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,2,0,2,0,2,1,2,1,0,0,0,0,1,1,1,1,
  2,0,0,2,2,1,1,2,2,2,2,2,2,2,2,2,2,1,2,1,2,0,2,0,2,1,1,1,2,0,0,0,
  2,1,2,1,2,0,2,0,1,2,1,2,0,2,0,2,2,2,2,0,2,2,2,1,2,0,2,0,2,1,2,1,
  2,0,2,0,2,1,2,1,0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2,2,0,2,0,2,1,2,1,
  2,2,2,2,2,2,2,2,2,0,0,0,2,1,1,1,2,2,2,2,2,2,2,2,2,0,2,0,2,1,2,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,
  2,0,0,0,2,1,1,1,0,0,0,0,1,1,1,1,2,0,2,0,2,1,2,1,0,0,2,0,1,1,2,1,
  2,2,1,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,0,0,0,2,1,1,1,
  2,2,2,1,2,2,2,0,2,1,1,1,2,0,0,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,1,2,1,2,0,2,0,1,2,1,2,0,2,0,2,
  2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,2,1,1,1,0,0,0,0,1,1,1,1,
  2,0,2,2,2,1,2,2,0,0,2,0,1,1,2,1,2,1,2,1,2,0,2,0,2,2,2,2,2,2,2,2,
  2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,0,0,0,0,1,1,1,1,
  2,0,0,0,2,1,1,1,0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2,2,1,0,2,2,0,1,2,
  2,2,2,1,2,2,2,0,2,1,1,1,2,0,0,0,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,0,2,2,2,2,2,2,2,2,2,2,
  2,1,2,1,2,0,2,0,1,2,1,1,0,2,0,0,0,0,2,1,1,1,2,0,0,0,0,0,1,1,1,1,
  2,2,2,2,2,2,2,2,1,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,0,2,1,2,1,2,0,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,0,2,2,2,1,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,1,2,2,2,0,2,2,2,2,0,2,2,2,1,2,2,2,0,0,2,2,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,0,2,2,2,2,2,2,2,2,2,
  2,0,2,0,2,1,2,1,0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2,0,0,0,0,1,1,1,1,
  2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,0,0,0,2,1,1,1,
  2,2,2,0,2,2,2,1,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,
  2,0,2,2,2,1,2,2,2,0,2,0,2,1,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  2,1,2,1,2,0,2,0,1,2,1,2,0,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,1,2,1,2,0,2,0,1,2,1,1,0,2,0,0,2,0,2,2,2,1,2,2,0,2,1,2,1,2,0,2,
  2,2,2,1,2,2,2,0,2,2,1,2,2,2,0,2,2,1,2,2,2,0,2,2,2,2,0,2,2,2,1,2,
  0,0,2,0,1,1,2,1,0,0,1,0,1,1,0,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,0,2,2,2,1,1,2,2,2,0,2,2,2,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,
  2,2,2,2,2,2,2,2,2,1,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,0,0,2,2,1,1,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,0,2,2,2,2,2,2,2,2,2,
  2,0,0,0,2,1,1,1,0,0,0,0,1,1,1,1,2,2,2,1,2,2,2,0,2,1,2,1,2,0,2,0,
  2,1,2,2,2,0,2,2,2,2,2,2,2,2,2,2,0,2,0,0,1,2,1,1,2,0,0,0,2,1,1,1,
  2,2,2,2,2,2,2,2,2,1,1,1,2,0,0,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,0,2,2,2,1,2,2,0,2,2,2,1,2,2,1,2,1,2,0,2,0,2,0,2,2,2,1,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,0,2,2,2,1,1,1,2,0,0,0,
  2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,0,2,0,0,1,2,1,1,
  2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,0,2,2,2,1,2,2,2,
  2,1,2,1,2,0,2,0,2,1,2,2,2,0,2,2,2,2,2,0,2,2,2,1,2,0,2,0,2,1,2,1,
  2,0,2,0,2,1,2,1,0,2,0,2,1,2,1,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,
  2,2,2,2,2,2,2,2,0,2,0,2,1,2,1,2,2,2,2,2,2,2,2,2,0,1,0,0,1,0,1,1,
  2,2,2,2,2,2,2,2,2,1,2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,2,2,1,2,2,2,0,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,1,2,2,1,0,2,0,2,2,2,1,2,2,2,
  2,2,2,2,2,2,2,2,1,2,2,2,0,2,2,2,2,2,2,0,2,2,2,1,2,2,0,2,2,2,1,2,
  2,0,2,0,2,1,2,1,0,2,0,2,1,2,1,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,
  0,2,0,0,1,2,1,1,2,0,0,0,2,1,1,1,2,2,2,2,2,2,2,2,1,0,1,2,0,1,0,2,
  2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,1,2,2,2,0,2,2,1,1,2,2,0,0,2,2,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,1,2,1,2,0,2,0,2,1,2,2,2,0,2,2,2,0,2,2,2,1,2,2,0,2,2,2,1,2,2,2,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,1,2,2,2,0,2,2,2,
  2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,0,2,1,2,1,2,
  0,0,0,0,1,1,1,1,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,1,2,0,2,0,2,2,0,2,2,2,1,2,
  2,0,2,0,2,1,2,1,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,
  0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,1,2,2,2,0,1,1,2,1,0,0,2,0,2,0,2,2,2,1,2,2,0,2,2,2,1,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,0,2,2,2,2,0,2,2,2,1,2,
  2,0,2,0,2,1,2,1,0,2,0,2,1,2,1,2,2,2,2,2,2,2,2,2,2,1,2,2,2,0,2,2,
  0,2,0,0,1,2,1,1,0,2,0,2,1,2,1,2,2,2,2,2,2,2,2,2,0,0,0,2,1,1,1,2,
  2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,0,0,2,1,1,1,2,0,0,2,2,2,1,2,2,2,
  2,1,2,1,2,0,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,2,0,0,1,2,1,1,
  0,0,2,2,1,1,2,2,0,2,1,2,1,2,0,2,2,1,2,1,2,0,2,0,1,2,1,2,0,2,0,2,
  2,2,2,2,2,2,2,2,1,2,1,2,0,2,0,2,2,2,2,2,2,2,2,2,2,0,2,0,2,1,2,1,
  2,2,0,0,2,2,1,1,2,2,0,0,2,2,1,1,2,2,2,2,2,2,2,2,2,2,0,0,2,2,1,1,
  2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,0,2,0,0,1,2,1,1,
  2,2,2,0,2,2,2,1,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,1,1,1,2,0,0,0,2,
  2,2,2,2,2,2,2,2,1,1,1,2,0,0,0,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,
  2,0,2,0,2,1,2,1,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,0,0,0,2,1,1,1,
  2,0,2,2,2,1,2,2,0,2,2,2,1,2,2,2,2,0,2,0,2,1,2,1,2,2,2,2,2,2,2,2,
  2,0,2,0,2,1,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,0,2,0,2,1,2,1,2,1,2,0,2,0,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,0,2,0,2,1,2,1,1,2,1,2,0,2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,1,2,1,2,0,2,0,2,2,1,2,1,2,0,2,0,2,2,2,2,2,2,2,2,
  2,0,2,1,2,1,2,0,0,2,1,2,1,2,0,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  2,0,2,0,2,1,2,1,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,0,2,0,2,1,2,1,
  2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,1,2,1,2,0,2,0,1,1,1,2,0,0,0,2,2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,
  2,0,2,0,2,1,2,1,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  2,2,2,2,2,2,2,2,2,2,2,0,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
};

/* Arithmetic decoder state */
typedef struct {
    unsigned char st[4096];
    unsigned long c;
    unsigned long a;
    unsigned char *pscd_ptr;
    unsigned char *pscd_end;
    int ct;
    int startup;
    int nopadding;
} jbig__ardec_state;

/* Decoder state structure */
struct _stbi_jbig_context {
    /* from BIH */
    int d;
    int dl;
    unsigned long xd, yd;
    int planes;
    unsigned long l0;
    unsigned long stripes;
    int order;
    int options;
    int mx, my;
    char *dppriv;

    /* loop variables */
    unsigned long ii[3];

    /* image data pointers */
    unsigned char **lhp[2];

    /* status arrays */
    int **tx, **ty;
    jbig__ardec_state **s;
    int **reset;
    int **lntp;

    unsigned long bie_len;
    unsigned char buffer[20];
    int buf_len;
    unsigned long comment_skip;
    unsigned long x;
    unsigned long i;
    int at_moves;
    unsigned long at_line[JBIG__ATMOVES_MAX];
    int at_tx[JBIG__ATMOVES_MAX], at_ty[JBIG__ATMOVES_MAX];
    unsigned long line_h1, line_h2, line_h3;
    unsigned long line_l1, line_l2, line_l3;
    int pseudo;

    /* result */
    int finished;
};

/* Utility: ceil(x / 2^n) */
static unsigned long jbig__ceil_half(unsigned long x, int n)
{
    return (x + (1UL << n) - 1) >> n;
}

/* Calculate number of stripes */
static unsigned long jbig__stripes(unsigned long l0, unsigned long yd, int d)
{
    unsigned long l = l0 << d;
    if (l == 0) return 0;
    return (yd + l - 1) / l;
}

/* ======================================================================== */
/*                    Arithmetic Decoder Functions                           */
/* ======================================================================== */

static void jbig__arith_decode_init(jbig__ardec_state *s, int reuse_st)
{
    int i;
    if (!reuse_st)
        for (i = 0; i < 4096; s->st[i++] = 0);
    s->c = 0;
    s->a = 1;
    s->ct = 0;
    s->startup = 1;
    s->nopadding = 0;
}

static int jbig__arith_decode(jbig__ardec_state *s, int cx)
{
    unsigned lsz, ss;
    unsigned char *st;
    int pix;

    /* renormalization */
    while (s->a < 0x8000 || s->startup) {
        while (s->ct <= 8 && s->ct >= 0) {
            if (s->pscd_ptr >= s->pscd_end) {
                return -1;
            } else if (*s->pscd_ptr == 0xff) {
                if (s->pscd_ptr + 1 >= s->pscd_end)
                    return -1;
                if (*(s->pscd_ptr + 1) == JBIG__MARKER_STUFF) {
                    s->c |= 0xffUL << (8 - s->ct);
                    s->ct += 8;
                    s->pscd_ptr += 2;
                } else {
                    s->ct = -1;
                    if (s->nopadding) {
                        s->nopadding = 0;
                        return -2;
                    }
                }
            } else {
                s->c |= (unsigned long)*(s->pscd_ptr++) << (8 - s->ct);
                s->ct += 8;
            }
        }
        s->c <<= 1;
        s->a <<= 1;
        if (s->ct >= 0) s->ct--;
        if (s->a == 0x10000UL)
            s->startup = 0;
    }

    st = s->st + cx;
    ss = *st & 0x7f;
    lsz = jbig__lsztab[ss];

    if ((s->c >> 16) < (s->a -= lsz)) {
        if (s->a & 0xffff8000UL)
            return *st >> 7;
        if (s->a < lsz) {
            pix = 1 - (*st >> 7);
            *st &= 0x80;
            *st ^= jbig__nlpstab[ss];
        } else {
            pix = *st >> 7;
            *st &= 0x80;
            *st |= jbig__nmpstab[ss];
        }
    } else {
        if (s->a < lsz) {
            s->c -= s->a << 16;
            s->a = lsz;
            pix = *st >> 7;
            *st &= 0x80;
            *st |= jbig__nmpstab[ss];
        } else {
            s->c -= s->a << 16;
            s->a = lsz;
            pix = 1 - (*st >> 7);
            *st &= 0x80;
            *st ^= jbig__nlpstab[ss];
        }
    }

    return pix;
}

/* ======================================================================== */
/*                    JBIG Decoder Internal Functions                        */
/* ======================================================================== */

static void jbig__dec_destroy(stbi_jbig_context *s)
{
    int i;
    if (s->s) {
        for (i = 0; i < s->planes; i++) {
            STBJBIG_FREE(s->s[i]);
            STBJBIG_FREE(s->tx[i]);
            STBJBIG_FREE(s->ty[i]);
            STBJBIG_FREE(s->reset[i]);
            STBJBIG_FREE(s->lntp[i]);
            STBJBIG_FREE(s->lhp[0][i]);
            STBJBIG_FREE(s->lhp[1][i]);
        }
        STBJBIG_FREE(s->s);
        STBJBIG_FREE(s->tx);
        STBJBIG_FREE(s->ty);
        STBJBIG_FREE(s->reset);
        STBJBIG_FREE(s->lntp);
        STBJBIG_FREE(s->lhp[0]);
        STBJBIG_FREE(s->lhp[1]);
    }
    if (s->dppriv && s->dppriv != (char *)jbig__dptable)
        STBJBIG_FREE(s->dppriv);
}

static void jbig__dec_init(stbi_jbig_context *s)
{
    memset(s, 0, sizeof(*s));
    s->d = -1;
    s->pseudo = 1;
}

static size_t jbig__decode_pscd(stbi_jbig_context *s, unsigned char *data, size_t len)
{
    unsigned long stripe;
    unsigned int layer, plane;
    unsigned long hl, ll, y, hx, hy, lx, ly, hbpl, lbpl;
    unsigned char *hp, *lp1, *lp2;
    unsigned long line_h1, line_h2, line_h3;
    unsigned long line_l1, line_l2, line_l3;
    jbig__ardec_state *se;
    unsigned long x;
    long o;
    unsigned a;
    int n;
    int pix, slntp, tx;

    stripe = s->ii[jbig__iindex[s->order & 7][JBIG__STRIPE]];
    layer = s->ii[jbig__iindex[s->order & 7][JBIG__LAYER]];
    plane = s->ii[jbig__iindex[s->order & 7][JBIG__PLANE]];

    se = s->s[plane] + layer - s->dl;
    se->pscd_ptr = data;
    se->pscd_end = data + len;

    hl = s->l0 << layer;
    ll = hl >> 1;
    y = stripe * hl + s->i;
    hx = jbig__ceil_half(s->xd, s->d - layer);
    hy = jbig__ceil_half(s->yd, s->d - layer);
    lx = jbig__ceil_half(hx, 1);
    ly = jbig__ceil_half(hy, 1);
    hbpl = jbig__ceil_half(hx, 3);
    lbpl = jbig__ceil_half(lx, 3);

    hp  = s->lhp[layer & 1][plane] + (stripe * hl + s->i) * hbpl + (s->x >> 3);
    lp2 = s->lhp[(layer-1) & 1][plane] + (stripe * ll + (s->i >> 1)) * lbpl + (s->x >> 4);
    lp1 = lp2 + lbpl;

    line_h1 = s->line_h1;
    line_h2 = s->line_h2;
    line_h3 = s->line_h3;
    line_l1 = s->line_l1;
    line_l2 = s->line_l2;
    line_l3 = s->line_l3;
    x = s->x;

    if (s->x == 0 && s->i == 0 &&
        (stripe == 0 || s->reset[plane][layer - s->dl]) && s->pseudo) {
        s->tx[plane][layer - s->dl] = s->ty[plane][layer - s->dl] = 0;
        s->lntp[plane][layer - s->dl] = 1;
    }

    if (layer == 0) {
        /* Decode lowest resolution layer */
        for (; s->i < hl && y < hy; s->i++, y++) {
            /* adaptive template changes */
            if (x == 0 && s->pseudo)
                for (n = 0; n < s->at_moves; n++)
                    if (s->at_line[n] == s->i) {
                        s->tx[plane][layer - s->dl] = s->at_tx[n];
                        s->ty[plane][layer - s->dl] = s->at_ty[n];
                    }
            tx = s->tx[plane][layer - s->dl];

            /* typical prediction */
            if (s->options & JBIG__TPBON && s->pseudo) {
                slntp = jbig__arith_decode(se, (s->options & JBIG__LRLTWO) ? JBIG__TPB2CX : JBIG__TPB3CX);
                if (slntp < 0)
                    goto leave;
                s->lntp[plane][layer - s->dl] = !(slntp ^ s->lntp[plane][layer - s->dl]);
                if (!s->lntp[plane][layer - s->dl]) {
                    /* line is typical: copy previous line */
                    unsigned char *p1, *q1;
                    p1 = hp;
                    if (s->i == 0 && (stripe == 0 || s->reset[plane][layer - s->dl])) {
                        while (p1 < hp + hbpl) *p1++ = 0;
                    } else {
                        q1 = hp - hbpl;
                        while (q1 < hp) *p1++ = *q1++;
                    }
                    hp += hbpl;
                    continue;
                }
            }
            s->pseudo = 0;

            /* refill line buffers every 8 pixels */
            if (x == 0) {
                line_h1 = line_h2 = line_h3 = 0;
                if (s->i > 0 || (y > 0 && !s->reset[plane][layer - s->dl]))
                    line_h2 = (unsigned long)*(hp - hbpl) << 8;
                if (s->i > 1 || (y > 1 && !s->reset[plane][layer - s->dl]))
                    line_h3 = (unsigned long)*(hp - hbpl - hbpl) << 8;
            }

            /* decode line */
            while (x < hx) {
                if ((x & 7) == 0) {
                    if (x < hbpl * 8 - 8 &&
                        (s->i > 0 || (y > 0 && !s->reset[plane][layer - s->dl]))) {
                        line_h2 |= *(hp - hbpl + 1);
                        if (s->i > 1 || (y > 1 && !s->reset[plane][layer - s->dl]))
                            line_h3 |= *(hp - hbpl - hbpl + 1);
                    }
                }
                if (s->options & JBIG__LRLTWO) {
                    /* two line template */
                    do {
                        if (tx) {
                            if ((unsigned)tx > x)
                                a = 0;
                            else if (tx < 8)
                                a = ((line_h1 >> (tx - 5)) & 0x010);
                            else {
                                o = (long)((x - tx) - (x & ~7UL));
                                a = (unsigned long)(hp[o >> 3] >> (7 - (o & 7))) & 1;
                                a <<= 4;
                            }
                            pix = jbig__arith_decode(se, (((line_h2 >> 9) & 0x3e0) | a |
                                                          (line_h1 & 0x00f)));
                        } else {
                            pix = jbig__arith_decode(se, (((line_h2 >> 9) & 0x3f0) |
                                                          (line_h1 & 0x00f)));
                        }
                        if (pix < 0)
                            goto leave;
                        line_h1 = (line_h1 << 1) | (unsigned long)pix;
                        line_h2 <<= 1;
                    } while ((++x & 7) && x < hx);
                } else {
                    /* three line template */
                    do {
                        if (tx) {
                            if ((unsigned)tx > x)
                                a = 0;
                            else if (tx < 8)
                                a = ((line_h1 >> (tx - 3)) & 0x004);
                            else {
                                o = (long)((x - tx) - (x & ~7UL));
                                a = (unsigned long)(hp[o >> 3] >> (7 - (o & 7))) & 1;
                                a <<= 2;
                            }
                            pix = jbig__arith_decode(se, (((line_h3 >>  7) & 0x380) |
                                                          ((line_h2 >> 11) & 0x078) | a |
                                                          (line_h1 & 0x003)));
                        } else {
                            pix = jbig__arith_decode(se, (((line_h3 >>  7) & 0x380) |
                                                          ((line_h2 >> 11) & 0x07c) |
                                                          (line_h1 & 0x003)));
                        }
                        if (pix < 0)
                            goto leave;
                        line_h1 = (line_h1 << 1) | (unsigned long)pix;
                        line_h2 <<= 1;
                        line_h3 <<= 1;
                    } while ((++x & 7) && x < hx);
                }
                *hp++ = (unsigned char)line_h1;
            }
            *(hp - 1) <<= (unsigned)(hbpl * 8 - hx);
            x = 0;
            s->pseudo = 1;
        }
        s->x = x;

    } else {

        /* Decode differential layer */
        for (; s->i < hl && y < hy; s->i++, y++) {
            /* adaptive template changes */
            if (x == 0)
                for (n = 0; n < s->at_moves; n++)
                    if (s->at_line[n] == s->i) {
                        s->tx[plane][layer - s->dl] = s->at_tx[n];
                        s->ty[plane][layer - s->dl] = s->at_ty[n];
                    }
            tx = s->tx[plane][layer - s->dl];

            /* handle lower border of low-resolution image */
            if ((s->i >> 1) >= (int)(ll - 1) || (y >> 1) >= (int)(ly - 1))
                lp1 = lp2;

            /* typical prediction */
            if ((s->options & JBIG__TPDON) && s->pseudo) {
                if ((s->lntp[plane][layer - s->dl] = jbig__arith_decode(se, JBIG__TPDCX)) < 0)
                    goto leave;
            }
            s->pseudo = 0;

            if (x == 0) {
                line_h1 = line_h2 = line_h3 = line_l1 = line_l2 = line_l3 = 0;
                if (s->i > 0 || (y > 0 && !s->reset[plane][layer - s->dl])) {
                    line_h2 = (unsigned long)*(hp - hbpl) << 8;
                    if (s->i > 1 || (y > 1 && !s->reset[plane][layer - s->dl]))
                        line_h3 = (unsigned long)*(hp - hbpl - hbpl) << 8;
                }
                if (s->i > 1 || (y > 1 && !s->reset[plane][layer - s->dl]))
                    line_l3 = (unsigned long)*(lp2 - lbpl) << 8;
                line_l2 = (unsigned long)*lp2 << 8;
                line_l1 = (unsigned long)*lp1 << 8;
            }

            /* decode line */
            while (x < hx) {
                if ((x & 15) == 0)
                    if ((x >> 1) < lbpl * 8 - 8) {
                        line_l1 |= *(lp1 + 1);
                        line_l2 |= *(lp2 + 1);
                        if (s->i > 1 ||
                            (y > 1 && !s->reset[plane][layer - s->dl]))
                            line_l3 |= *(lp2 - lbpl + 1);
                    }
                do {
                    if ((x & 7) == 0)
                        if (x < hbpl * 8 - 8) {
                            if (s->i > 0 || (y > 0 && !s->reset[plane][layer - s->dl])) {
                                line_h2 |= *(hp + 1 - hbpl);
                                if (s->i > 1 || (y > 1 && !s->reset[plane][layer - s->dl]))
                                    line_h3 |= *(hp + 1 - hbpl - hbpl);
                            }
                        }
                    do {
                        if (!s->lntp[plane][layer - s->dl]) {
                            int cx;
                            cx = (int)(((line_l3 >> 14) & 0x007) |
                                      ((line_l2 >> 11) & 0x038) |
                                      ((line_l1 >> 8)  & 0x1c0));
                            if (cx == 0x000 || cx == 0x1ff) {
                                /* pixels are typical and have not to be decoded */
                                do {
                                    line_h1 = (line_h1 << 1) | (unsigned long)(cx & 1);
                                } while ((++x & 1) && x < hx);
                                line_h2 <<= 2;  line_h3 <<= 2;
                            } else
                                goto non_typical;
                        } else {
                        non_typical:
                            do {
                                /* deterministic prediction */
                                if (s->options & JBIG__DPON)
                                    if ((y & 1) == 0)
                                        if ((x & 1) == 0)
                                            pix = (int)((unsigned char *)s->dppriv)[((line_l3 >> 15) & 0x003) |
                                                    ((line_l2 >> 13) & 0x00c) |
                                                    ((line_h1 <<  4) & 0x010) |
                                                    ((line_h2 >>  9) & 0x0e0)];
                                        else
                                            pix = (int)((unsigned char *)s->dppriv)[(((line_l3 >> 15) & 0x003) |
                                                     ((line_l2 >> 13) & 0x00c) |
                                                     ((line_h1 <<  4) & 0x030) |
                                                     ((line_h2 >>  9) & 0x1c0)) + 256];
                                    else
                                        if ((x & 1) == 0)
                                            pix = (int)((unsigned char *)s->dppriv)[(((line_l3 >> 15) & 0x003) |
                                                     ((line_l2 >> 13) & 0x00c) |
                                                     ((line_h1 <<  4) & 0x010) |
                                                     ((line_h2 >>  9) & 0x0e0) |
                                                     ((line_h3 >>  6) & 0x700)) + 768];
                                        else
                                            pix = (int)((unsigned char *)s->dppriv)[(((line_l3 >> 15) & 0x003) |
                                                     ((line_l2 >> 13) & 0x00c) |
                                                     ((line_h1 <<  4) & 0x030) |
                                                     ((line_h2 >>  9) & 0x1c0) |
                                                     ((line_h3 >>  6) & 0xe00)) + 2816];
                                else
                                    pix = 2;

                                if (pix & 2) {
                                    int cx;
                                    if (tx)
                                        cx = (int)((line_h1         & 0x003) |
                                                  (((line_h1 << 2) >> (tx - 3)) & 0x010) |
                                                  ((line_h2 >> 12) & 0x00c) |
                                                  ((line_h3 >> 10) & 0x020));
                                    else
                                        cx = (int)((line_h1         & 0x003) |
                                                  ((line_h2 >> 12) & 0x01c) |
                                                  ((line_h3 >> 10) & 0x020));
                                    if (x & 1)
                                        cx |= (int)(((line_l2 >> 8) & 0x0c0) |
                                                   ((line_l1 >> 6) & 0x300)) | (1 << 10);
                                    else
                                        cx |= (int)(((line_l2 >> 9) & 0x0c0) |
                                                   ((line_l1 >> 7) & 0x300));
                                    cx |= (int)((y & 1) << 11);
                                    pix = jbig__arith_decode(se, cx);
                                    if (pix < 0)
                                        goto leave;
                                }

                                line_h1 = (line_h1 << 1) | (unsigned long)pix;
                                line_h2 <<= 1;
                                line_h3 <<= 1;

                            } while ((++x & 1) && x < hx);
                        }
                        line_l1 <<= 1; line_l2 <<= 1;  line_l3 <<= 1;
                    } while ((x & 7) && x < hx);
                    *hp++ = (unsigned char)line_h1;
                } while ((x & 15) && x < hx);
                ++lp1;
                ++lp2;
            }
            x = 0;

            *(hp - 1) <<= (unsigned)(hbpl * 8 - hx);
            if ((s->i & 1) == 0) {
                /* low resolution pixels are used twice */
                lp1 -= lbpl;
                lp2 -= lbpl;
            } else
                s->pseudo = 1;
        }
    }

leave:
    s->line_h1 = line_h1;
    s->line_h2 = line_h2;
    s->line_h3 = line_h3;
    s->line_l1 = line_l1;
    s->line_l2 = line_l2;
    s->line_l3 = line_l3;
    s->x = x;

    return (size_t)(se->pscd_ptr - data);
}

static int jbig__dec_in(stbi_jbig_context *s, unsigned char *data, size_t len, size_t *cnt)
{
    int i, j, required_length;
    unsigned long x, y;
    size_t dummy_cnt;

    if (!cnt) cnt = &dummy_cnt;
    *cnt = 0;
    if (len < 1) return JBIG__EAGAIN;

    /* Read 20-byte BIH */
    if (s->bie_len < 20) {
        while (s->bie_len < 20 && *cnt < len)
            s->buffer[s->bie_len++] = data[(*cnt)++];
        if (s->bie_len < 20)
            return JBIG__EAGAIN;

        /* Validate header */
        if (s->buffer[1] < s->buffer[0])
            return JBIG__EINVAL | 1;
        if (s->buffer[3] != 0)
            return JBIG__EINVAL | 2;
        if ((s->buffer[18] & 0xf0) != 0)
            return JBIG__EINVAL | 3;
        if ((s->buffer[19] & 0x80) != 0)
            return JBIG__EINVAL | 4;
        if (s->buffer[0] != s->d + 1)
            return JBIG__ENOCONT | 1;

        s->dl = s->buffer[0];
        s->d = s->buffer[1];
        if (s->dl == 0)
            s->planes = s->buffer[2];
        else if (s->planes != s->buffer[2])
            return JBIG__ENOCONT | 2;

        x = (((unsigned long)s->buffer[ 4] << 24) |
             ((unsigned long)s->buffer[ 5] << 16) |
             ((unsigned long)s->buffer[ 6] <<  8) |
             (unsigned long)s->buffer[ 7]);
        y = (((unsigned long)s->buffer[ 8] << 24) |
             ((unsigned long)s->buffer[ 9] << 16) |
             ((unsigned long)s->buffer[10] <<  8) |
             (unsigned long)s->buffer[11]);
        if (s->dl != 0 && ((s->xd << (s->d - s->dl + 1)) != x &&
                           (s->yd << (s->d - s->dl + 1)) != y))
            return JBIG__ENOCONT | 3;

        s->xd = x;
        s->yd = y;
        s->l0 = (((unsigned long)s->buffer[12] << 24) |
                 ((unsigned long)s->buffer[13] << 16) |
                 ((unsigned long)s->buffer[14] <<  8) |
                 (unsigned long)s->buffer[15]);
        if (s->yd == 0xffffffffUL)
            return JBIG__EIMPL | 1;
        if (!s->planes) return JBIG__EINVAL | 5;
        if (!s->xd)     return JBIG__EINVAL | 6;
        if (!s->yd)     return JBIG__EINVAL | 7;
        if (!s->l0)     return JBIG__EINVAL | 8;
        if (s->d > 31)  return JBIG__EIMPL | 2;
        if (s->d != 0 && s->l0 >= (1UL << (32 - s->d)))
            return JBIG__EIMPL | 3;
        s->mx = s->buffer[16];
        if (s->mx > 127) return JBIG__EINVAL | 9;
        s->my = s->buffer[17];
        s->order = s->buffer[18];
        if (jbig__iindex[s->order & 7][0] < 0)
            return JBIG__EINVAL | 10;
        if (s->dl != s->d && (s->order & JBIG__HITOLO || s->order & JBIG__SEQ))
            return JBIG__EIMPL | 5;
        s->options = s->buffer[19];

        if (s->dl == 0 || (s->options & JBIG__DPON && !(s->options & JBIG__DPPRIV)))
            s->dppriv = (char *)jbig__dptable;

        s->stripes = jbig__stripes(s->l0, s->yd, s->d);

        /* Initialize loop variables */
        s->ii[jbig__iindex[s->order & 7][JBIG__STRIPE]] = 0;
        s->ii[jbig__iindex[s->order & 7][JBIG__LAYER]] = s->dl;
        s->ii[jbig__iindex[s->order & 7][JBIG__PLANE]] = 0;

        if (s->dl == 0) {
            s->s      = (jbig__ardec_state **)STBJBIG_MALLOC(s->planes * sizeof(jbig__ardec_state *));
            s->tx     = (int **)STBJBIG_MALLOC(s->planes * sizeof(int *));
            s->ty     = (int **)STBJBIG_MALLOC(s->planes * sizeof(int *));
            s->reset  = (int **)STBJBIG_MALLOC(s->planes * sizeof(int *));
            s->lntp   = (int **)STBJBIG_MALLOC(s->planes * sizeof(int *));
            s->lhp[0] = (unsigned char **)STBJBIG_MALLOC(s->planes * sizeof(unsigned char *));
            s->lhp[1] = (unsigned char **)STBJBIG_MALLOC(s->planes * sizeof(unsigned char *));

            for (i = 0; i < s->planes; i++) {
                s->s[i]     = (jbig__ardec_state *)STBJBIG_MALLOC((size_t)(s->d - s->dl + 1) * sizeof(jbig__ardec_state));
                s->tx[i]    = (int *)STBJBIG_MALLOC((size_t)(s->d - s->dl + 1) * sizeof(int));
                s->ty[i]    = (int *)STBJBIG_MALLOC((size_t)(s->d - s->dl + 1) * sizeof(int));
                s->reset[i] = (int *)STBJBIG_MALLOC((size_t)(s->d - s->dl + 1) * sizeof(int));
                s->lntp[i]  = (int *)STBJBIG_MALLOC((size_t)(s->d - s->dl + 1) * sizeof(int));
                s->lhp[s->d & 1][i] = (unsigned char *)STBJBIG_MALLOC(s->yd * jbig__ceil_half(s->xd, 3));
                s->lhp[(s->d-1) & 1][i] = (unsigned char *)STBJBIG_MALLOC(jbig__ceil_half(s->yd, 1) * jbig__ceil_half(s->xd, 4));
            }
        }

        for (i = 0; i < s->planes; i++)
            for (j = 0; j <= s->d - s->dl; j++)
                jbig__arith_decode_init(s->s[i] + j, 0);

        s->comment_skip = 0;
        s->buf_len = 0;
        s->x = 0;
        s->i = 0;
        s->pseudo = 1;
        s->at_moves = 0;
    }

    /* BID processing loop */
    while (*cnt < len) {
        /* skip COMMENT contents */
        if (s->comment_skip) {
            if (s->comment_skip <= len - *cnt) {
                *cnt += s->comment_skip;
                s->comment_skip = 0;
            } else {
                s->comment_skip -= len - *cnt;
                *cnt = len;
            }
            continue;
        }

        /* load complete marker segments into s->buffer */
        if (s->buf_len > 0) {
            assert(s->buffer[0] == JBIG__MARKER_ESC);
            while (s->buf_len < 2 && *cnt < len)
                s->buffer[s->buf_len++] = data[(*cnt)++];
            if (s->buf_len < 2) continue;

            switch (s->buffer[1]) {
            case JBIG__MARKER_COMMENT: required_length = 6; break;
            case JBIG__MARKER_ATMOVE:  required_length = 8; break;
            case JBIG__MARKER_NEWLEN:  required_length = 6; break;
            case JBIG__MARKER_ABORT:
            case JBIG__MARKER_SDNORM:
            case JBIG__MARKER_SDRST:   required_length = 2; break;
            case JBIG__MARKER_STUFF:
                /* STUFF: the 0xff byte is valid PSCD data.
                   Forward it to the arithmetic decoder. */
                s->buf_len = 0;
                jbig__decode_pscd(s, s->buffer, 2);
                continue;
            default:
                return JBIG__EMARKER | s->buffer[1];
            }

            while (s->buf_len < required_length && *cnt < len)
                s->buffer[s->buf_len++] = data[(*cnt)++];
            if (s->buf_len < required_length) continue;
            /* now the buffer is filled with exactly one marker segment */
            switch (s->buffer[1]) {
            case JBIG__MARKER_COMMENT:
                s->comment_skip = (((unsigned long)s->buffer[2] << 24) |
                                   ((unsigned long)s->buffer[3] << 16) |
                                   ((unsigned long)s->buffer[4] <<  8) |
                                   (unsigned long)s->buffer[5]);
                break;
            case JBIG__MARKER_ATMOVE:
                if (s->at_moves < JBIG__ATMOVES_MAX) {
                    s->at_line[s->at_moves] = (((unsigned long)s->buffer[2] << 24) |
                                                ((unsigned long)s->buffer[3] << 16) |
                                                ((unsigned long)s->buffer[4] <<  8) |
                                                (unsigned long)s->buffer[5]);
                    s->at_tx[s->at_moves] = (int)(signed char)s->buffer[6];
                    s->at_ty[s->at_moves] = (int)(signed char)s->buffer[7];
                    s->at_moves++;
                }
                break;
            case JBIG__MARKER_NEWLEN:
                {
                    unsigned long new_yd;
                    new_yd = (((unsigned long)s->buffer[2] << 24) |
                              ((unsigned long)s->buffer[3] << 16) |
                              ((unsigned long)s->buffer[4] <<  8) |
                              (unsigned long)s->buffer[5]);
                    if (new_yd > s->yd) return JBIG__EINVAL | 12;
                    if (!(s->options & JBIG__VLENGTH)) return JBIG__EINVAL | 13;
                    s->yd = new_yd;
                    s->stripes = jbig__stripes(s->l0, s->yd, s->d);
                }
                break;
            case JBIG__MARKER_ABORT:
                return JBIG__EABORT;

            case JBIG__MARKER_SDNORM:
            case JBIG__MARKER_SDRST:
                /* finish any remaining pixels based on trailing zero bytes */
                jbig__decode_pscd(s, s->buffer, 2);

                /* reinitialize arithmetic decoder state */
                jbig__arith_decode_init(
                    s->s[s->ii[jbig__iindex[s->order & 7][JBIG__PLANE]]] +
                    s->ii[jbig__iindex[s->order & 7][JBIG__LAYER]] - s->dl,
                    s->ii[jbig__iindex[s->order & 7][JBIG__STRIPE]] != s->stripes - 1
                    && s->buffer[1] != JBIG__MARKER_SDRST);

                s->reset[s->ii[jbig__iindex[s->order & 7][JBIG__PLANE]]]
                    [s->ii[jbig__iindex[s->order & 7][JBIG__LAYER]] - s->dl] =
                    (s->buffer[1] == JBIG__MARKER_SDRST);

                /* prepare for next SDE */
                s->x = 0;
                s->i = 0;
                s->pseudo = 1;
                s->at_moves = 0;

                /* increment layer/stripe/plane loop variables */
                {
                    unsigned long is[3], ie[3];
                    int ii, jj;

                    is[jbig__iindex[s->order & 7][JBIG__STRIPE]] = 0;
                    ie[jbig__iindex[s->order & 7][JBIG__STRIPE]] = s->stripes - 1;
                    is[jbig__iindex[s->order & 7][JBIG__LAYER]] = s->dl;
                    ie[jbig__iindex[s->order & 7][JBIG__LAYER]] = s->d;
                    is[jbig__iindex[s->order & 7][JBIG__PLANE]] = 0;
                    ie[jbig__iindex[s->order & 7][JBIG__PLANE]] = s->planes - 1;
                    ii = 2;
                    do {
                        jj = 0;
                        if (++s->ii[ii] > ie[ii]) {
                            jj = 1;
                            if (ii > 0)
                                s->ii[ii] = is[ii];
                        }
                    } while (--ii >= 0 && jj);

                    s->buf_len = 0;

                    /* check whether this has been all SDEs */
                    if (jj) {
                        s->bie_len = 0;
                        return JBIG__EOK;
                    }
                }
                break;
            }
            s->buf_len = 0;
            continue;
        }

        /* check for marker or PSCD data */
        if (data[*cnt] == JBIG__MARKER_ESC)
            s->buffer[s->buf_len++] = data[(*cnt)++];
        else {
            /* we have found PSCD bytes */
            size_t consumed = jbig__decode_pscd(s, data + *cnt, len - *cnt);
            *cnt += consumed;
            if (*cnt < len && data[*cnt] != 0xff) {
                while (*cnt < len && data[*cnt] != 0xff)
                    ++(*cnt);
            }
        }
    }

    return JBIG__EAGAIN;
}

/* ======================================================================== */
/*                    Public API Implementation                              */
/* ======================================================================== */

STBJBIGDEF unsigned char *stbi_jbig_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *planes)
{
    stbi_jbig_context s;
    size_t cnt = 0;
    size_t total = 0;
    int result;

    if (!buffer || len < 1) {
        stbi__jbig_err("invalid input");
        return NULL;
    }

    jbig__dec_init(&s);

    result = jbig__dec_in(&s, (unsigned char *)buffer, (size_t)len, &cnt);
    total += cnt;

    /* for progressive files (D>0), keep feeding remaining BIEs */
    while (result == JBIG__EOK && total < (size_t)len) {
        result = jbig__dec_in(&s, (unsigned char *)buffer + total,
                              (size_t)len - total, &cnt);
        total += cnt;
    }

    if (result != JBIG__EOK) {
        stbi__jbig_err("JBIG decode failed");
        jbig__dec_destroy(&s);
        return NULL;
    }

    /* extract the image */
    {
        unsigned long hx, hy, hbpl;
        unsigned char *out;

        hx = s.xd;
        hy = s.yd;
        hbpl = jbig__ceil_half(hx, 3);

        out = (unsigned char *)STBJBIG_MALLOC(hbpl * (size_t)hy);
        if (!out) {
            jbig__dec_destroy(&s);
            stbi__jbig_err("out of memory");
            return NULL;
        }

        memcpy(out, s.lhp[s.d & 1][0], hbpl * (size_t)hy);

        *x = (int)hx;
        *y = (int)hy;
        *planes = s.planes;

        jbig__dec_destroy(&s);
        return out;
    }
}

STBJBIGDEF unsigned char *stbi_jbig_load_from_file(const char *path, int *x, int *y, int *planes)
{
    FILE *f;
    unsigned char *buf;
    long filelen;
    unsigned char *out;

    if (!path) { stbi__jbig_err("NULL path"); return NULL; }

    f = fopen(path, "rb");
    if (!f) { stbi__jbig_err("cannot open file"); return NULL; }

    fseek(f, 0, SEEK_END);
    filelen = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (filelen <= 0) { fclose(f); stbi__jbig_err("empty file"); return NULL; }

    buf = (unsigned char *)STBJBIG_MALLOC((size_t)filelen);
    if (!buf) { fclose(f); stbi__jbig_err("out of memory"); return NULL; }

    if ((long)fread(buf, 1, (size_t)filelen, f) != filelen) {
        fclose(f);
        STBJBIG_FREE(buf);
        stbi__jbig_err("read failed");
        return NULL;
    }
    fclose(f);

    out = stbi_jbig_load_from_memory(buf, (int)filelen, x, y, planes);
    STBJBIG_FREE(buf);
    return out;
}

STBJBIGDEF unsigned char *stbi_jbig_load_rgb_from_memory(const unsigned char *buffer, int len, int *x, int *y)
{
    unsigned char *bmp;
    unsigned char *rgb;
    int w, h, p, bx, by, px;

    bmp = stbi_jbig_load_from_memory(buffer, len, &w, &h, &p);
    if (!bmp) return NULL;

    rgb = (unsigned char *)STBJBIG_MALLOC((size_t)w * (size_t)h * 3);
    if (!rgb) { stbi_jbig_free(bmp); stbi__jbig_err("out of memory"); return NULL; }

    bx = (w + 7) / 8;
    for (by = 0; by < h; by++) {
        for (px = 0; px < w; px++) {
            unsigned char byte = bmp[by * bx + (px >> 3)];
            int bit = (byte >> (7 - (px & 7))) & 1;
            unsigned char val = bit ? 0 : 255;
            unsigned char *dst = rgb + ((size_t)by * w + px) * 3;
            dst[0] = val;
            dst[1] = val;
            dst[2] = val;
        }
    }

    stbi_jbig_free(bmp);
    *x = w;
    *y = h;
    return rgb;
}

STBJBIGDEF unsigned char *stbi_jbig_load_rgb_from_file(const char *path, int *x, int *y)
{
    unsigned char *bmp;
    unsigned char *rgb;
    int w, h, p, bx, by, px;

    bmp = stbi_jbig_load_from_file(path, &w, &h, &p);
    if (!bmp) return NULL;

    rgb = (unsigned char *)STBJBIG_MALLOC((size_t)w * (size_t)h * 3);
    if (!rgb) { stbi_jbig_free(bmp); stbi__jbig_err("out of memory"); return NULL; }

    bx = (w + 7) / 8;
    for (by = 0; by < h; by++) {
        for (px = 0; px < w; px++) {
            unsigned char byte = bmp[by * bx + (px >> 3)];
            int bit = (byte >> (7 - (px & 7))) & 1;
            unsigned char val = bit ? 0 : 255;
            unsigned char *dst = rgb + ((size_t)by * w + px) * 3;
            dst[0] = val;
            dst[1] = val;
            dst[2] = val;
        }
    }

    stbi_jbig_free(bmp);
    *x = w;
    *y = h;
    return rgb;
}

STBJBIGDEF void stbi_jbig_free(void *ptr)
{
    STBJBIG_FREE(ptr);
}

STBJBIGDEF int stbi_jbig_test_memory(const unsigned char *buffer, int len)
{
    if (len < 20) return 0;
    if (buffer[1] < buffer[0]) return 0;
    if (buffer[3] != 0) return 0;
    if ((buffer[18] & 0xf0) != 0) return 0;
    if ((buffer[19] & 0x80) != 0) return 0;
    return 1;
}

/* Incremental API */
STBJBIGDEF void stbi_jbig_init_context(stbi_jbig_context *s)
{
    jbig__dec_init(s);
}

STBJBIGDEF int stbi_jbig_in(stbi_jbig_context *s, const unsigned char *data, size_t len, size_t *bytes_read)
{
    return jbig__dec_in(s, (unsigned char *)data, len, bytes_read);
}

STBJBIGDEF int stbi_jbig_get_width(const stbi_jbig_context *s)
{
    return (int)jbig__ceil_half(s->xd, s->d);
}

STBJBIGDEF int stbi_jbig_get_height(const stbi_jbig_context *s)
{
    return (int)jbig__ceil_half(s->yd, s->d);
}

STBJBIGDEF int stbi_jbig_get_planes(const stbi_jbig_context *s)
{
    return s->planes;
}

STBJBIGDEF unsigned char *stbi_jbig_get_image(stbi_jbig_context *s)
{
    unsigned long hx, hy, hbpl;
    unsigned char *out;

    if (!s->finished) return NULL;

    hx = jbig__ceil_half(s->xd, s->d);
    hy = jbig__ceil_half(s->yd, s->d);
    hbpl = jbig__ceil_half(hx, 3);

    out = (unsigned char *)STBJBIG_MALLOC(hbpl * (size_t)hy);
    if (!out) return NULL;

    memcpy(out, s->lhp[s->d & 1][0], hbpl * (size_t)hy);
    return out;
}

STBJBIGDEF void stbi_jbig_destroy(stbi_jbig_context *s)
{
    jbig__dec_destroy(s);
}

#endif /* STB_JBIG_IMPLEMENTATION */

/*
   This is free and unencumbered software released into the public domain.

   Anyone is free to copy, modify, publish, use, compile, sell, or
   distribute this software, either in source code form or as a compiled
   binary, for any purpose, commercial or non-commercial, and by any
   means.

   In jurisdictions that recognize copyright laws, the author or authors
   of this software dedicate any and all copyright interest in the
   software to the public domain. We make this dedication for the benefit
   of the public at large and to the detriment of our heirs and
   successors. We intend this dedication to be an overt act of
   relinquishment in perpetuity of all present and future rights to this
   software under copyright law.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
   OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
   OTHER DEALINGS IN THE SOFTWARE.

   For more information, please refer to <http://unlicense.org/>
*/
