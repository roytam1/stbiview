/*
    minitiff_v2_c89.c

    Small, human-readable TIFF decoder.

    C89-compatible core. No C99 types, declarations in for-loops, // comments,
    or C99-only language features are used.

    Output is always RGBA8.

    Supported by the core decoder:

      - Classic TIFF (version 42), not BigTIFF
      - Little-endian (II) and big-endian (MM)
      - Multiple IFDs/pages
      - Strips
      - RowsPerStrip
      - 1, 2, 4 and 8 bit grayscale
      - 1, 2, 4 and 8 bit palette images
      - 8 bit RGB and RGBA
      - Compression = 1   (none)
      - Compression = 5   (LZW)
      - Compression = 32773 (PackBits)
      - Predictor = 1 and 2 for byte-oriented data

    Optional features:

      MINITIFF_USE_STB_IMAGE

        Include stb_image.h and use stbi_load_from_memory() for JPEG-in-TIFF
        strips (Compression = 6). This is useful because stb_image already
        contains a good JPEG decoder.

        The user must provide stb_image.h in the include path.

        This source does NOT define STB_IMAGE_IMPLEMENTATION. The application
        should do that once, in one C file, before including stb_image.h.

      MINITIFF_USE_STB_ZLIB

        Use zlib for Deflate (Compression = 8 and 32946).

        zlib is deliberately a separate option. stb_image's internal zlib
        routines are not a stable public API, so this decoder does not depend
        on private stb_image symbols.

    Example:

        MiniTIFF_Image *image;

        image = tiff_load_file("picture.tif", 0);
        if (image) {
            unsigned char *rgba = image->pixels;
            unsigned long width = image->width;
            unsigned long height = image->height;

            ...

            tiff_free(image);
        }

    Optional stb_image build:

        cc -DMINITIFF_USE_STB_IMAGE -c minitiff_v2_c89.c

    The application must arrange for stb_image's implementation to be built
    exactly once, for example:

        #define STB_IMAGE_IMPLEMENTATION
        #include "stb_image.h"

    Optional stb_image build:

        #define STB_IMAGE_IMPLEMENTATION
        #define MINITIFF_USE_STB_IMAGE
        #define MINITIFF_USE_STB_ZLIB
        #include "minitiff_v2_c89.c"
*/
#ifndef _MINITFF_H
#define _MINITFF_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/*
    Optional stb_image support.

    MINITIFF_USE_STB_IMAGE:
        JPEG decoding through stb_image.

    MINITIFF_USE_STB_ZLIB:
        Deflate decoding through stb_image's internal zlib decoder.

    MINITIFF_USE_STB_ZLIB requires MINITIFF_USE_STB_IMAGE.

    IMPORTANT:
        Because stb_image's zlib functions are static/private, when
        MINITIFF_USE_STB_ZLIB is enabled this file must be compiled in
        the same translation unit as the stb_image implementation.

        A convenient arrangement is:

            #define STB_IMAGE_IMPLEMENTATION
            #define MINITIFF_USE_STB_IMAGE
            #define MINITIFF_USE_STB_ZLIB
            #include "minitiff_v2_c89.c"

        Do not compile stb_image.c separately in that configuration.
*/
#ifdef MINITIFF_USE_STB_IMAGE
#include "stb_image.h"
#endif

#ifdef MINITIFF_USE_STB_ZLIB
#ifndef MINITIFF_USE_STB_IMAGE
#error MINITIFF_USE_STB_ZLIB requires MINITIFF_USE_STB_IMAGE
#endif
#endif

/* for test code */
#ifdef TIFF_TEST
#define MINITIFF_IMPLEMENTATION
#endif

/* ------------------------------------------------------------------------- */
/* Public API                                                               */
/* ------------------------------------------------------------------------- */

typedef struct MiniTIFF_Image {
    unsigned long width;
    unsigned long height;
    unsigned long channels;
    unsigned long bits_per_channel;
    unsigned char *pixels;
} MiniTIFF_Image;

MiniTIFF_Image *tiff_load(const void *data, size_t size, unsigned page_index);
MiniTIFF_Image *tiff_load_file(const char *filename, unsigned page_index);
void tiff_free(MiniTIFF_Image *image);


#ifdef MINITIFF_IMPLEMENTATION
/* ------------------------------------------------------------------------- */
/* Internal TIFF structures                                                  */
/* ------------------------------------------------------------------------- */

typedef struct TIFF_Context {
    const unsigned char *data;
    size_t size;
    int little_endian;
    unsigned long first_ifd;
} TIFF_Context;


typedef struct TIFF_Entry {
    unsigned short tag;
    unsigned short type;
    unsigned long count;
    unsigned long value;
} TIFF_Entry;


typedef struct TIFF_Page {
    unsigned long width;
    unsigned long height;
    unsigned long rows_per_strip;

    unsigned short compression;
    unsigned short photometric;
    unsigned short samples_per_pixel;
    unsigned short planar_config;
    unsigned short predictor;

    unsigned short bits_per_sample[4];
    unsigned short bits_count;

    unsigned short extra_samples[4];
    unsigned short extra_count;

    unsigned long *strip_offsets;
    unsigned long *strip_byte_counts;
    unsigned long strip_count;

    unsigned short *color_map;
    unsigned long color_map_count;

#ifdef MINITIFF_USE_STB_IMAGE
    unsigned char *jpeg_tables;
    size_t jpeg_tables_size;
#endif
} TIFF_Page;


/* ------------------------------------------------------------------------- */
/* Safe size arithmetic                                                      */
/* ------------------------------------------------------------------------- */

static int tiff_mul_size(size_t a, size_t b, size_t *result)
{
    if (b != 0 && a > (size_t)-1 / b)
        return 0;

    *result = a * b;
    return 1;
}


static int tiff_add_size(size_t a, size_t b, size_t *result)
{
    if (a > (size_t)-1 - b)
        return 0;

    *result = a + b;
    return 1;
}


static int tiff_range_ok(const TIFF_Context *tiff,
                         unsigned long offset,
                         size_t length)
{
    size_t end;

    if (!tiff_add_size((size_t)offset, length, &end))
        return 0;

    return end <= tiff->size;
}


/* ------------------------------------------------------------------------- */
/* Endian helpers                                                            */
/* ------------------------------------------------------------------------- */

static unsigned short tiff_u16(const TIFF_Context *tiff,
                               const unsigned char *p)
{
    if (tiff->little_endian) {
        return (unsigned short)(
            (unsigned short)p[0] |
            ((unsigned short)p[1] << 8));
    }

    return (unsigned short)(
        ((unsigned short)p[0] << 8) |
        (unsigned short)p[1]);
}


static unsigned long tiff_u32(const TIFF_Context *tiff,
                              const unsigned char *p)
{
    if (tiff->little_endian) {
        return (unsigned long)p[0] |
               ((unsigned long)p[1] << 8) |
               ((unsigned long)p[2] << 16) |
               ((unsigned long)p[3] << 24);
    }

    return ((unsigned long)p[0] << 24) |
           ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) |
           (unsigned long)p[3];
}


/* ------------------------------------------------------------------------- */
/* TIFF field types                                                          */
/* ------------------------------------------------------------------------- */

static size_t tiff_type_size(unsigned short type)
{
    switch (type) {
    case 1:  /* BYTE */
    case 2:  /* ASCII */
    case 6:  /* SBYTE */
    case 7:  /* UNDEFINED */
        return 1;

    case 3:  /* SHORT */
    case 8:  /* SSHORT */
        return 2;

    case 4:  /* LONG */
    case 9:  /* SLONG */
    case 11: /* FLOAT */
        return 4;

    case 5:  /* RATIONAL */
    case 10: /* SRATIONAL */
    case 12: /* DOUBLE */
        return 8;

    default:
        return 0;
    }
}


/* ------------------------------------------------------------------------- */
/* TIFF entry access                                                         */
/* ------------------------------------------------------------------------- */

/*
    Get the raw value area belonging to an IFD entry.

    TIFF stores values of four bytes or less directly inside the entry.
    Larger values are referenced by an offset.
*/
static int tiff_entry_data(const TIFF_Context *tiff,
                           const TIFF_Entry *entry,
                           unsigned char raw[4],
                           const unsigned char **data,
                           size_t *size)
{
    size_t type_size;
    size_t total_size;
    unsigned long value;

    type_size = tiff_type_size(entry->type);
    if (type_size == 0)
        return 0;

    if (!tiff_mul_size(type_size,
                       (size_t)entry->count,
                       &total_size))
        return 0;

    *size = total_size;

    if (total_size <= 4) {
        value = entry->value;

        if (tiff->little_endian) {
            raw[0] = (unsigned char)(value);
            raw[1] = (unsigned char)(value >> 8);
            raw[2] = (unsigned char)(value >> 16);
            raw[3] = (unsigned char)(value >> 24);
        }
        else {
            raw[0] = (unsigned char)(value >> 24);
            raw[1] = (unsigned char)(value >> 16);
            raw[2] = (unsigned char)(value >> 8);
            raw[3] = (unsigned char)(value);
        }

        *data = raw;
        return 1;
    }

    if (!tiff_range_ok(tiff, entry->value, total_size))
        return 0;

    *data = tiff->data + entry->value;
    return 1;
}


/*
    Get a BYTE, SHORT or LONG entry value as an unsigned long.
*/
static int tiff_entry_get_u32(const TIFF_Context *tiff,
                              const TIFF_Entry *entry,
                              unsigned long index,
                              unsigned long *result)
{
    unsigned char raw[4];
    const unsigned char *data;
    size_t size;

    if (index >= entry->count)
        return 0;

    if (!tiff_entry_data(tiff, entry, raw, &data, &size))
        return 0;

    switch (entry->type) {
    case 1:
        *result = data[index];
        return 1;

    case 3:
        *result = tiff_u16(tiff, data + index * 2);
        return 1;

    case 4:
        *result = tiff_u32(tiff, data + index * 4);
        return 1;

    default:
        return 0;
    }
}


/* ------------------------------------------------------------------------- */
/* Dynamic arrays                                                            */
/* ------------------------------------------------------------------------- */

static int tiff_load_u32_array(const TIFF_Context *tiff,
                               const TIFF_Entry *entry,
                               unsigned long **result,
                               unsigned long *count)
{
    unsigned long *array;
    unsigned long i;
    unsigned long value;

    if (entry->type != 3 && entry->type != 4)
        return 0;

    if ((size_t)entry->count >
        (size_t)-1 / sizeof(unsigned long))
        return 0;

    array = (unsigned long *)malloc(
        (size_t)entry->count * sizeof(unsigned long));

    if (!array)
        return 0;

    for (i = 0; i < entry->count; ++i) {
        if (!tiff_entry_get_u32(tiff, entry, i, &value)) {
            free(array);
            return 0;
        }

        array[i] = value;
    }

    *result = array;
    *count = entry->count;
    return 1;
}


static int tiff_load_u16_array(const TIFF_Context *tiff,
                               const TIFF_Entry *entry,
                               unsigned short **result,
                               unsigned long *count)
{
    unsigned short *array;
    unsigned long i;
    unsigned long value;

    if (entry->type != 3)
        return 0;

    if ((size_t)entry->count >
        (size_t)-1 / sizeof(unsigned short))
        return 0;

    array = (unsigned short *)malloc(
        (size_t)entry->count * sizeof(unsigned short));

    if (!array)
        return 0;

    for (i = 0; i < entry->count; ++i) {
        if (!tiff_entry_get_u32(tiff, entry, i, &value) ||
            value > 65535UL) {
            free(array);
            return 0;
        }

        array[i] = (unsigned short)value;
    }

    *result = array;
    *count = entry->count;
    return 1;
}


#ifdef MINITIFF_USE_STB_IMAGE

/* ------------------------------------------------------------------------- */
/* Copy raw entry bytes                                                      */
/* ------------------------------------------------------------------------- */

static int tiff_copy_entry_bytes(const TIFF_Context *tiff,
                                 const TIFF_Entry *entry,
                                 unsigned char **result,
                                 size_t *result_size)
{
    size_t type_size;
    size_t size;
    unsigned char *buffer;

    type_size = tiff_type_size(entry->type);
    if (type_size == 0)
        return 0;

    if (!tiff_mul_size(type_size,
                       (size_t)entry->count,
                       &size))
        return 0;

    if (size == 0)
        return 0;

    buffer = (unsigned char *)malloc(size);
    if (!buffer)
        return 0;

    if (size <= 4) {
        unsigned char raw[4];
        unsigned long value;

        value = entry->value;

        if (tiff->little_endian) {
            raw[0] = (unsigned char)(value & 255UL);
            raw[1] = (unsigned char)((value >> 8) & 255UL);
            raw[2] = (unsigned char)((value >> 16) & 255UL);
            raw[3] = (unsigned char)((value >> 24) & 255UL);
        }
        else {
            raw[0] = (unsigned char)((value >> 24) & 255UL);
            raw[1] = (unsigned char)((value >> 16) & 255UL);
            raw[2] = (unsigned char)((value >> 8) & 255UL);
            raw[3] = (unsigned char)(value & 255UL);
        }

        memcpy(buffer, raw, size);
    }
    else {
        if (!tiff_range_ok(tiff,
                           entry->value,
                           size)) {
            free(buffer);
            return 0;
        }

        memcpy(buffer,
               tiff->data + entry->value,
               size);
    }

    *result = buffer;
    *result_size = size;
    return 1;
}


#endif /* MINITIFF_USE_STB_IMAGE */


/* ------------------------------------------------------------------------- */
/* Page cleanup                                                              */
/* ------------------------------------------------------------------------- */

static void tiff_page_free(TIFF_Page *page)
{
    free(page->strip_offsets);
    free(page->strip_byte_counts);
    free(page->color_map);
#ifdef MINITIFF_USE_STB_IMAGE
    free(page->jpeg_tables);
#endif

    memset(page, 0, sizeof(*page));
}


/* ------------------------------------------------------------------------- */
/* IFD parsing                                                               */
/* ------------------------------------------------------------------------- */

static int tiff_parse_ifd(const TIFF_Context *tiff,
                          unsigned long offset,
                          TIFF_Page *page)
{
    unsigned short entry_count;
    unsigned short i;

    memset(page, 0, sizeof(*page));

    /* Baseline TIFF defaults. */
    page->compression = 1;
    page->planar_config = 1;
    page->predictor = 1;

    if (!tiff_range_ok(tiff, offset, 2))
        return 0;

    entry_count = tiff_u16(tiff, tiff->data + offset);

    /* 2-byte count + entries + 4-byte next-IFD pointer. */
    {
        size_t entries_size;
        size_t total_size;

        if (!tiff_mul_size((size_t)entry_count,
                           12,
                           &entries_size))
            return 0;

        if (!tiff_add_size(entries_size, 6, &total_size))
            return 0;

        if (!tiff_range_ok(tiff, offset, total_size))
            return 0;
    }

    for (i = 0; i < entry_count; ++i) {
        TIFF_Entry entry;
        const unsigned char *p;
        unsigned long value;

        p = tiff->data + offset + 2 + (size_t)i * 12;

        entry.tag = tiff_u16(tiff, p + 0);
        entry.type = tiff_u16(tiff, p + 2);
        entry.count = tiff_u32(tiff, p + 4);
        entry.value = tiff_u32(tiff, p + 8);

        switch (entry.tag) {
        case 256: /* ImageWidth */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &page->width))
                return 0;
            break;

        case 257: /* ImageLength */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &page->height))
                return 0;
            break;

        case 258: /* BitsPerSample */
            if (entry.count > 4)
                return 0;

            page->bits_count =
                (unsigned short)entry.count;

            {
                unsigned long j;

                for (j = 0; j < entry.count; ++j) {
                    if (!tiff_entry_get_u32(tiff,
                                            &entry,
                                            j,
                                            &value) ||
                        value > 16UL)
                        return 0;

                    page->bits_per_sample[j] =
                        (unsigned short)value;
                }
            }
            break;

        case 259: /* Compression */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &value) ||
                value > 65535UL)
                return 0;

            page->compression =
                (unsigned short)value;
            break;

        case 262: /* PhotometricInterpretation */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &value) ||
                value > 65535UL)
                return 0;

            page->photometric =
                (unsigned short)value;
            break;

        case 273: /* StripOffsets */
            if (!tiff_load_u32_array(tiff,
                                     &entry,
                                     &page->strip_offsets,
                                     &page->strip_count))
                return 0;
            break;

        case 277: /* SamplesPerPixel */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &value) ||
                value > 65535UL)
                return 0;

            page->samples_per_pixel =
                (unsigned short)value;
            break;

        case 278: /* RowsPerStrip */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &page->rows_per_strip))
                return 0;
            break;

        case 279: /* StripByteCounts */
            {
                unsigned long count;

                if (!tiff_load_u32_array(
                        tiff,
                        &entry,
                        &page->strip_byte_counts,
                        &count))
                    return 0;

                if (page->strip_count != 0 &&
                    page->strip_count != count)
                    return 0;

                page->strip_count = count;
            }
            break;

        case 284: /* PlanarConfiguration */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &value) ||
                value > 65535UL)
                return 0;

            page->planar_config =
                (unsigned short)value;
            break;

        case 317: /* Predictor */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &value) ||
                value > 65535UL)
                return 0;

            page->predictor =
                (unsigned short)value;
            break;

        case 320: /* ColorMap */
            if (!tiff_load_u16_array(tiff,
                                     &entry,
                                     &page->color_map,
                                     &page->color_map_count))
                return 0;
            break;

        case 338: /* ExtraSamples */
            if (entry.count > 4)
                return 0;

            page->extra_count =
                (unsigned short)entry.count;

            {
                unsigned long j;

                for (j = 0; j < entry.count; ++j) {
                    if (!tiff_entry_get_u32(tiff,
                                            &entry,
                                            j,
                                            &value) ||
                        value > 65535UL)
                        return 0;

                    page->extra_samples[j] =
                        (unsigned short)value;
                }
            }
            break;

        case 347: /* JPEGTables */
#ifdef MINITIFF_USE_STB_IMAGE
            if (entry.type != 7) /* UNDEFINED */
                return 0;

            free(page->jpeg_tables);
            page->jpeg_tables = NULL;
            page->jpeg_tables_size = 0;

            if (!tiff_copy_entry_bytes(
                    tiff,
                    &entry,
                    &page->jpeg_tables,
                    &page->jpeg_tables_size))
                return 0;
#else
            /* JPEGTables is harmless when JPEG support is disabled. */
#endif
            break;

        default:
            /* Unknown tags are harmless; ignore them. */
            break;
        }
    }

    /* Baseline defaults. */
    if (page->samples_per_pixel == 0) {
        if (page->photometric == 2)
            page->samples_per_pixel = 3;
        else
            page->samples_per_pixel = 1;
    }

    if (page->bits_count == 0) {
        page->bits_count = page->samples_per_pixel;

        if (page->bits_count > 4)
            return 0;

        for (i = 0; i < page->bits_count; ++i)
            page->bits_per_sample[i] = 8;
    }

    if (page->width == 0 ||
        page->height == 0 ||
        page->rows_per_strip == 0 ||
        page->strip_count == 0 ||
        page->strip_offsets == NULL ||
        page->strip_byte_counts == NULL)
        return 0;

    if (page->samples_per_pixel == 0 ||
        page->samples_per_pixel > 4)
        return 0;

    if (page->planar_config != 1)
        return 0;

    if (page->compression != 1 &&
        page->compression != 5 &&
        page->compression != 6 &&
        page->compression != 7 &&
        page->compression != 8 &&
        page->compression != 32773 &&
        page->compression != 32946)
        return 0;

    if (page->predictor != 1 &&
        page->predictor != 2)
        return 0;

    if (page->photometric > 3)
        return 0;

    if (page->photometric == 2 &&
        page->samples_per_pixel < 3)
        return 0;

    if (page->photometric == 3 &&
        page->color_map == NULL)
        return 0;

    if (page->bits_count != 1 &&
        page->bits_count != page->samples_per_pixel)
        return 0;

    for (i = 0; i < page->bits_count; ++i) {
        unsigned short bits = page->bits_per_sample[i];

        if (bits != 1 && bits != 2 &&
            bits != 4 && bits != 8)
            return 0;

        if (page->photometric == 2 && bits != 8)
            return 0;
    }

    /* JPEG and Deflate are optional. */
    if (page->compression == 6 ||
        page->compression == 7) {
#ifndef MINITIFF_USE_STB_IMAGE
        return 0;
#endif
    }

    if (page->compression == 8 ||
        page->compression == 32946) {
#ifndef MINITIFF_USE_STB_ZLIB
        return 0;
#endif
    }

    return 1;
}


/* ------------------------------------------------------------------------- */
/* PackBits decoder                                                          */
/* ------------------------------------------------------------------------- */

static int tiff_packbits_decode(const unsigned char *src,
                                size_t src_size,
                                unsigned char *dst,
                                size_t dst_size)
{
    size_t src_pos;
    size_t dst_pos;

    src_pos = 0;
    dst_pos = 0;

    while (src_pos < src_size && dst_pos < dst_size) {
        int n;

        n = (int)(signed char)src[src_pos++];

        if (n >= 0) {
            size_t count;

            count = (size_t)n + 1;

            if (count > src_size - src_pos ||
                count > dst_size - dst_pos)
                return 0;

            memcpy(dst + dst_pos,
                   src + src_pos,
                   count);

            src_pos += count;
            dst_pos += count;
        }
        else if (n != -128) {
            size_t count;

            count = (size_t)(1 - n);

            if (src_pos >= src_size ||
                count > dst_size - dst_pos)
                return 0;

            memset(dst + dst_pos,
                   src[src_pos],
                   count);

            ++src_pos;
            dst_pos += count;
        }
    }

    return dst_pos == dst_size;
}


/* ------------------------------------------------------------------------- */
/* TIFF LZW decoder                                                          */
/* ------------------------------------------------------------------------- */

/*
    TIFF LZW is MSB-first.

    This is different from GIF LZW, which is commonly implemented using
    LSB-first code packing. TIFF also uses the historical "early change"
    code-width rule.
*/
typedef struct TIFF_LZW {
    int prefix[4096];
    unsigned char suffix[4096];
    unsigned char stack[4096];

    int code_size;
    int next_code;
    int old_code;

    int clear_code;
    int end_code;

    unsigned long bit_buffer;
    int bit_count;
} TIFF_LZW;


static int tiff_lzw_get_code(TIFF_LZW *lzw,
                             const unsigned char *src,
                             size_t src_size,
                             size_t *position)
{
    while (lzw->bit_count < lzw->code_size) {
        if (*position >= src_size)
            return -1;

        lzw->bit_buffer =
            (lzw->bit_buffer << 8) |
            (unsigned long)src[(*position)++];

        lzw->bit_count += 8;
    }

    {
        unsigned long mask;
        int shift;
        int code;

        mask = (1UL << lzw->code_size) - 1UL;
        shift = lzw->bit_count - lzw->code_size;

        code = (int)((lzw->bit_buffer >> shift) & mask);

        lzw->bit_count -= lzw->code_size;

        if (lzw->bit_count != 0)
            lzw->bit_buffer &=
                (1UL << lzw->bit_count) - 1UL;
        else
            lzw->bit_buffer = 0;

        return code;
    }
}


static int tiff_lzw_decode(const unsigned char *src,
                           size_t src_size,
                           unsigned char *dst,
                           size_t dst_size)
{
    TIFF_LZW lzw;
    size_t position;
    size_t output;
    int first_char;

    memset(&lzw, 0, sizeof(lzw));

    lzw.code_size = 9;
    lzw.next_code = 258;
    lzw.old_code = -1;
    lzw.clear_code = 256;
    lzw.end_code = 257;

    position = 0;
    output = 0;
    first_char = -1;

    while (output < dst_size) {
        int code;

        code = tiff_lzw_get_code(&lzw,
                                 src,
                                 src_size,
                                 &position);

        if (code < 0)
            return 0;

        if (code == lzw.clear_code) {
            lzw.code_size = 9;
            lzw.next_code = 258;
            lzw.old_code = -1;
            first_char = -1;
            continue;
        }

        if (code == lzw.end_code)
            break;

        /* First code after CLEAR. */
        if (lzw.old_code < 0) {
            if (code < 0 || code > 255)
                return 0;

            if (output >= dst_size)
                return 0;

            dst[output++] = (unsigned char)code;

            first_char = code;
            lzw.old_code = code;
            continue;
        }

        {
            int current;
            int stack_count;

            current = code;
            stack_count = 0;

            /*
                KwKwK case: the requested code is the next dictionary
                entry that has not quite been created yet.
            */
            if (code == lzw.next_code) {
                if (first_char < 0 ||
                    stack_count >= 4096)
                    return 0;

                lzw.stack[stack_count++] =
                    (unsigned char)first_char;

                current = lzw.old_code;
            }
            else if (code > lzw.next_code ||
                     code >= 4096) {
                return 0;
            }

            while (current >= 256) {
                if (current < 258 ||
                    current >= 4096 ||
                    stack_count >= 4096)
                    return 0;

                lzw.stack[stack_count++] =
                    lzw.suffix[current];

                current = lzw.prefix[current];
            }

            if (current < 0 ||
                current > 255 ||
                stack_count >= 4096)
                return 0;

            first_char = current;

            lzw.stack[stack_count++] =
                (unsigned char)current;

            while (stack_count != 0) {
                if (output >= dst_size)
                    return 0;

                dst[output++] =
                    lzw.stack[--stack_count];
            }

            /*
                Add old_string + first_char to the dictionary.
            */
            if (lzw.next_code < 4096) {
                lzw.prefix[lzw.next_code] =
                    lzw.old_code;

                lzw.suffix[lzw.next_code] =
                    (unsigned char)first_char;

                ++lzw.next_code;

                /* TIFF uses early-change. */
                if (lzw.next_code ==
                    ((1 << lzw.code_size) - 1) &&
                    lzw.code_size < 12)
                    ++lzw.code_size;
            }

            lzw.old_code = code;
        }
    }

    return output == dst_size;
}


/* ------------------------------------------------------------------------- */
/* Optional Deflate decoder                                                  */
/* ------------------------------------------------------------------------- */

#ifdef MINITIFF_USE_STB_ZLIB

/*
    stb_image keeps its zlib decoder private.  This wrapper is intended
    to be added to a copy of stb_image.h, after the internal zlib code.

    The exact stb_image internal function is not part of its public API,
    so this bridge may need adjustment when stb_image is upgraded.
*/
static int minitiff_stb_zlib_decode(const unsigned char *input,
                                    int input_length,
                                    unsigned char *output,
                                    int output_length)
{
    int result;

    result = stbi_zlib_decode_buffer((char *)output,
                                      output_length,
                                      (const char *)input,
                                      input_length);

    return result == output_length;
}

static int minitiff_zlib_decode(const unsigned char *src,
                                size_t src_size,
                                unsigned char *dst,
                                size_t dst_size)
{
    if (src_size > (size_t)INT_MAX ||
        dst_size > (size_t)INT_MAX)
        return 0;

    return minitiff_stb_zlib_decode(src,
                                    (int)src_size,
                                    dst,
                                    (int)dst_size);
}

#endif /* MINITIFF_USE_STB_ZLIB */


/* ------------------------------------------------------------------------- */
/* JPEG-in-TIFF decoder                                                      */
/* ------------------------------------------------------------------------- */

#ifdef MINITIFF_USE_STB_IMAGE

static int tiff_jpeg_decode(const unsigned char *src,
                            size_t src_size,
                            unsigned char *dst,
                            size_t dst_size,
                            unsigned long expected_width,
                            unsigned long expected_height,
                            unsigned short expected_samples,
                            const unsigned char *jpeg_tables,
                            size_t jpeg_tables_size)
{
    int width;
    int height;
    int channels;
    unsigned char *decoded;
    unsigned char *jpeg_data;
    size_t jpeg_size;
    size_t table_start;
    size_t table_end;
    size_t strip_start;
    size_t strip_end;
    size_t total_size;
    size_t decoded_size;
    size_t pixel_count;
    size_t i;

    jpeg_data = NULL;
    decoded = NULL;

    /*
        Compression 6/7 JPEG strips can use the JPEGTables tag.

        A JPEGTables value normally looks like:

            SOI + DQT/DHT/etc + EOI

        while the strip contains:

            SOI + SOF/SOS/data + EOI

        stbi_load_from_memory() wants one complete JPEG stream, so
        remove the wrapper markers and join the two parts.
    */
    table_start = 0;
    table_end = jpeg_tables_size;
    strip_start = 0;
    strip_end = src_size;

    if (jpeg_tables_size >= 2 &&
        jpeg_tables[0] == 0xff &&
        jpeg_tables[1] == 0xd8)
        table_start = 2;

    if (table_end >= table_start + 2 &&
        jpeg_tables[table_end - 2] == 0xff &&
        jpeg_tables[table_end - 1] == 0xd9)
        table_end -= 2;

    if (src_size >= 2 &&
        src[0] == 0xff &&
        src[1] == 0xd8)
        strip_start = 2;

    if (strip_end >= strip_start + 2 &&
        src[strip_end - 2] == 0xff &&
        src[strip_end - 1] == 0xd9)
        strip_end -= 2;

    if (!tiff_add_size(table_end - table_start,
                       strip_end - strip_start,
                       &total_size) ||
        !tiff_add_size(total_size, 4, &total_size))
        return 0;

    jpeg_data = (unsigned char *)malloc(total_size);
    if (!jpeg_data)
        return 0;

    jpeg_size = 0;

    /* SOI */
    jpeg_data[jpeg_size++] = 0xff;
    jpeg_data[jpeg_size++] = 0xd8;

    if (table_end > table_start) {
        memcpy(jpeg_data + jpeg_size,
               jpeg_tables + table_start,
               table_end - table_start);
        jpeg_size += table_end - table_start;
    }

    if (strip_end > strip_start) {
        memcpy(jpeg_data + jpeg_size,
               src + strip_start,
               strip_end - strip_start);
        jpeg_size += strip_end - strip_start;
    }

    /* EOI */
    jpeg_data[jpeg_size++] = 0xff;
    jpeg_data[jpeg_size++] = 0xd9;

    if (jpeg_size > (size_t)INT_MAX) {
        free(jpeg_data);
        return 0;
    }

    decoded = stbi_load_from_memory(
        jpeg_data,
        (int)jpeg_size,
        &width,
        &height,
        &channels,
        0);

    free(jpeg_data);

    if (!decoded)
        return 0;

    if ((unsigned long)width != expected_width ||
        (unsigned long)height != expected_height) {
        stbi_image_free(decoded);
        return 0;
    }

    if (channels != 1 && channels != 2 &&
        channels != 3 && channels != 4) {
        stbi_image_free(decoded);
        return 0;
    }

    if (!tiff_mul_size((size_t)width,
                       (size_t)height,
                       &pixel_count) ||
        !tiff_mul_size(pixel_count,
                       (size_t)expected_samples,
                       &decoded_size) ||
        decoded_size != dst_size) {
        stbi_image_free(decoded);
        return 0;
    }

    for (i = 0; i < pixel_count; ++i) {
        if (expected_samples == 1) {
            if (channels == 1 || channels == 2) {
                dst[i] = decoded[i * (size_t)channels];
            }
            else {
                unsigned int r;
                unsigned int g;
                unsigned int b;

                r = decoded[i * (size_t)channels + 0];
                g = decoded[i * (size_t)channels + 1];
                b = decoded[i * (size_t)channels + 2];

                dst[i] = (unsigned char)((r + g + b) / 3);
            }
        }
        else if (expected_samples == 3) {
            if (channels == 1 || channels == 2) {
                unsigned char v;

                v = decoded[i * (size_t)channels];

                dst[i * 3 + 0] = v;
                dst[i * 3 + 1] = v;
                dst[i * 3 + 2] = v;
            }
            else {
                dst[i * 3 + 0] = decoded[i * (size_t)channels + 0];
                dst[i * 3 + 1] = decoded[i * (size_t)channels + 1];
                dst[i * 3 + 2] = decoded[i * (size_t)channels + 2];
            }
        }
        else if (expected_samples == 4) {
            if (channels == 1 || channels == 2) {
                unsigned char v;

                v = decoded[i * (size_t)channels];

                dst[i * 4 + 0] = v;
                dst[i * 4 + 1] = v;
                dst[i * 4 + 2] = v;
                dst[i * 4 + 3] =
                    channels == 2 ? decoded[i * 2 + 1] : 255;
            }
            else {
                dst[i * 4 + 0] = decoded[i * (size_t)channels + 0];
                dst[i * 4 + 1] = decoded[i * (size_t)channels + 1];
                dst[i * 4 + 2] = decoded[i * (size_t)channels + 2];
                dst[i * 4 + 3] =
                    channels == 4 ? decoded[i * 4 + 3] : 255;
            }
        }
        else {
            stbi_image_free(decoded);
            return 0;
        }
    }

    stbi_image_free(decoded);
    return 1;
}

#endif /* MINITIFF_USE_STB_IMAGE */


/* ------------------------------------------------------------------------- */
/* Strip decoding                                                            */
/* ------------------------------------------------------------------------- */

static int tiff_decode_strip(const TIFF_Context *tiff,
                             const TIFF_Page *page,
                             unsigned long strip,
                             unsigned char *destination,
                             size_t destination_size,
                             unsigned long strip_width,
                             unsigned long strip_height)
{
    unsigned long offset;
    unsigned long byte_count;

#ifndef MINITIFF_USE_STB_IMAGE
    (void)strip_width;
    (void)strip_height;
#endif

    if (strip >= page->strip_count)
        return 0;

    offset = page->strip_offsets[strip];
    byte_count = page->strip_byte_counts[strip];

    if (!tiff_range_ok(tiff, offset, (size_t)byte_count))
        return 0;

    switch (page->compression) {
    case 1:
        if ((size_t)byte_count != destination_size)
            return 0;

        memcpy(destination,
               tiff->data + offset,
               destination_size);
        return 1;

    case 5:
        return tiff_lzw_decode(
            tiff->data + offset,
            (size_t)byte_count,
            destination,
            destination_size);

    case 32773:
        return tiff_packbits_decode(
            tiff->data + offset,
            (size_t)byte_count,
            destination,
            destination_size);

#ifdef MINITIFF_USE_STB_ZLIB
    case 8:
    case 32946:
        return minitiff_zlib_decode(
            tiff->data + offset,
            (size_t)byte_count,
            destination,
            destination_size);
#else
    case 8:
    case 32946:
        return 0;
#endif

#ifdef MINITIFF_USE_STB_IMAGE
    case 6:
    case 7:
        return tiff_jpeg_decode(
            tiff->data + offset,
            (size_t)byte_count,
            destination,
            destination_size,
            strip_width,
            strip_height,
            page->samples_per_pixel,
            page->jpeg_tables,
            page->jpeg_tables_size);
#else
    case 6:
    case 7:
        return 0;
#endif

    default:
        return 0;
    }
}


/* ------------------------------------------------------------------------- */
/* Predictor                                                                 */
/* ------------------------------------------------------------------------- */

static void tiff_predictor_horizontal(unsigned char *data,
                                      unsigned long width,
                                      unsigned long rows,
                                      unsigned long samples_per_pixel)
{
    unsigned long y;
    size_t stride;

    stride = (size_t)width *
             (size_t)samples_per_pixel;

    for (y = 0; y < rows; ++y) {
        unsigned char *row;
        size_t x;

        row = data + (size_t)y * stride;

        for (x = (size_t)samples_per_pixel;
             x < stride;
             ++x) {
            row[x] = (unsigned char)(
                row[x] + row[x - samples_per_pixel]);
        }
    }
}


/* ------------------------------------------------------------------------- */
/* Small-bit-depth sample extraction                                        */
/* ------------------------------------------------------------------------- */

static unsigned long tiff_get_sample(const unsigned char *row,
                                     size_t bit_position,
                                     unsigned short bits)
{
    unsigned char value;
    unsigned int shift;

    if (bits == 8)
        return row[bit_position >> 3];

    value = row[bit_position >> 3];

    if (bits == 4) {
        shift = (unsigned int)(4 -
            ((bit_position & 7) >> 1) * 4);
        return (unsigned long)((value >> shift) & 15);
    }

    if (bits == 2) {
        shift = (unsigned int)(6 -
            ((bit_position & 7) >> 1) * 2);
        return (unsigned long)((value >> shift) & 3);
    }

    /* bits == 1 */
    shift = (unsigned int)(7 - (bit_position & 7));
    return (unsigned long)((value >> shift) & 1);
}


static unsigned char tiff_scale_sample(unsigned long value,
                                        unsigned short bits)
{
    unsigned long maximum;
    unsigned long scaled;

    if (bits == 8)
        return (unsigned char)value;

    maximum = (1UL << bits) - 1UL;
    scaled = (value * 255UL + maximum / 2UL) / maximum;

    return (unsigned char)scaled;
}


/* ------------------------------------------------------------------------- */
/* Palette lookup                                                            */
/* ------------------------------------------------------------------------- */

static unsigned char tiff_palette_value(const TIFF_Page *page,
                                        unsigned long index,
                                        int channel)
{
    unsigned long entries;
    unsigned short value;

    if (!page->color_map)
        return 0;

    entries = page->color_map_count / 3UL;

    if (entries == 0 || index >= entries)
        return 0;

    value = page->color_map[
        (unsigned long)channel * entries + index];

    /* TIFF ColorMap entries are normally 16-bit 0..65535. */
    return (unsigned char)(value >> 8);
}


/* ------------------------------------------------------------------------- */
/* Convert raw samples to RGBA8                                              */
/* ------------------------------------------------------------------------- */

static int tiff_convert_pixels(const TIFF_Page *page,
                               const unsigned char *raw,
                               MiniTIFF_Image *image)
{
    unsigned long x;
    unsigned long y;
    unsigned short bits;
    size_t row_bits;
    size_t row_bytes;

    bits = page->bits_per_sample[0];

    if (!tiff_mul_size((size_t)page->width,
                       (size_t)page->samples_per_pixel,
                       &row_bits) ||
        !tiff_mul_size(row_bits,
                       (size_t)bits,
                       &row_bits))
        return 0;

    row_bytes = (row_bits + 7) / 8;

    for (y = 0; y < page->height; ++y) {
        const unsigned char *row;

        row = raw + (size_t)y * row_bytes;

        for (x = 0; x < page->width; ++x) {
            unsigned char r;
            unsigned char g;
            unsigned char b;
            unsigned char a;
            size_t base_bit;
            size_t output_offset;

            r = 0;
            g = 0;
            b = 0;
            a = 255;

            base_bit = (size_t)x *
                       (size_t)page->samples_per_pixel *
                       (size_t)bits;

            if (page->photometric == 0 ||
                page->photometric == 1) {
                unsigned long value;

                value = tiff_get_sample(row,
                                        base_bit,
                                        bits);

                if (page->photometric == 0)
                    value = ((1UL << bits) - 1UL) - value;

                r = tiff_scale_sample(value, bits);
                g = r;
                b = r;

                if (page->samples_per_pixel >= 2) {
                    value = tiff_get_sample(
                        row,
                        base_bit + bits,
                        bits);

                    a = tiff_scale_sample(value, bits);
                }
            }
            else if (page->photometric == 2) {
                /* RGB/RGBA is byte-oriented in this decoder. */
                const unsigned char *pixel;

                pixel = row + (size_t)x *
                        (size_t)page->samples_per_pixel;

                r = pixel[0];
                g = pixel[1];
                b = pixel[2];

                if (page->samples_per_pixel >= 4)
                    a = pixel[3];
            }
            else if (page->photometric == 3) {
                unsigned long index;

                index = tiff_get_sample(row,
                                        base_bit,
                                        bits);

                r = tiff_palette_value(page, index, 0);
                g = tiff_palette_value(page, index, 1);
                b = tiff_palette_value(page, index, 2);

                if (page->samples_per_pixel >= 2) {
                    index = tiff_get_sample(
                        row,
                        base_bit + bits,
                        bits);

                    a = tiff_scale_sample(index, bits);
                }
            }
            else {
                return 0;
            }

            output_offset =
                ((size_t)y * (size_t)image->width +
                 (size_t)x) * 4;

            image->pixels[output_offset + 0] = r;
            image->pixels[output_offset + 1] = g;
            image->pixels[output_offset + 2] = b;
            image->pixels[output_offset + 3] = a;
        }
    }

    return 1;
}


/* ------------------------------------------------------------------------- */
/* Decode one TIFF page                                                      */
/* ------------------------------------------------------------------------- */

static MiniTIFF_Image *tiff_decode_page(const TIFF_Context *tiff,
                                    const TIFF_Page *page)
{
    MiniTIFF_Image *image;
    unsigned char *raw;
    size_t raw_row_size;
    size_t raw_size;
    size_t pixel_count;
    size_t pixel_size;
    unsigned long y;
    unsigned long strip;

    /*
        Calculate the decoded bytes in one scanline.

        RGB is always 8-bit in this implementation.
        Grayscale/palette may be packed at 1/2/4/8 bits.
    */
    if (page->photometric == 2) {
        if (page->samples_per_pixel != 3 &&
            page->samples_per_pixel != 4)
            return NULL;

        if (!tiff_mul_size(
                (size_t)page->width,
                (size_t)page->samples_per_pixel,
                &raw_row_size))
            return NULL;
    }
    else {
        size_t row_bits;

        if (!tiff_mul_size(
                (size_t)page->width,
                (size_t)page->samples_per_pixel,
                &row_bits))
            return NULL;

        if (!tiff_mul_size(
                row_bits,
                (size_t)page->bits_per_sample[0],
                &row_bits))
            return NULL;

        raw_row_size = (row_bits + 7) / 8;
    }

    if (!tiff_mul_size(raw_row_size,
                       (size_t)page->height,
                       &raw_size) ||
        raw_size == 0)
        return NULL;

    raw = (unsigned char *)malloc(raw_size);
    if (!raw)
        return NULL;

    /* Decode strips in their normal top-to-bottom order. */
    y = 0;

    for (strip = 0;
         strip < page->strip_count &&
         y < page->height;
         ++strip) {
        unsigned long rows;
        size_t strip_size;
        unsigned char *destination;

        rows = page->height - y;
        if (rows > page->rows_per_strip)
            rows = page->rows_per_strip;

        if (!tiff_mul_size(raw_row_size,
                           (size_t)rows,
                           &strip_size)) {
            free(raw);
            return NULL;
        }

        destination = raw +
            (size_t)y * raw_row_size;

        if (!tiff_decode_strip(
                tiff,
                page,
                strip,
                destination,
                strip_size,
                page->width,
                rows)) {
            free(raw);
            return NULL;
        }

        if (page->predictor == 2) {
            /* Predictor 2 is implemented for byte-oriented samples. */
            if (page->bits_per_sample[0] != 8) {
                free(raw);
                return NULL;
            }

            tiff_predictor_horizontal(
                destination,
                page->width,
                rows,
                page->samples_per_pixel);
        }

        y += rows;
    }

    if (y != page->height) {
        free(raw);
        return NULL;
    }

    if (!tiff_mul_size((size_t)page->width,
                       (size_t)page->height,
                       &pixel_count) ||
        !tiff_mul_size(pixel_count,
                       4,
                       &pixel_size)) {
        free(raw);
        return NULL;
    }

    image = (MiniTIFF_Image *)calloc(1, sizeof(*image));
    if (!image) {
        free(raw);
        return NULL;
    }

    image->width = page->width;
    image->height = page->height;
    image->channels = 4;
    image->bits_per_channel = 8;

    image->pixels = (unsigned char *)malloc(pixel_size);
    if (!image->pixels) {
        free(raw);
        tiff_free(image);
        return NULL;
    }

    if (!tiff_convert_pixels(page, raw, image)) {
        free(raw);
        tiff_free(image);
        return NULL;
    }

    free(raw);
    return image;
}


/* ------------------------------------------------------------------------- */
/* Find an IFD/page                                                          */
/* ------------------------------------------------------------------------- */

static int tiff_find_ifd(const TIFF_Context *tiff,
                         unsigned page_index,
                         unsigned long *result)
{
    unsigned long offset;
    unsigned page;

    offset = tiff->first_ifd;

    for (page = 0; page < page_index; ++page) {
        unsigned short count;
        size_t entries_size;
        size_t total_size;

        if (!tiff_range_ok(tiff, offset, 2))
            return 0;

        count = tiff_u16(tiff,
                         tiff->data + offset);

        if (!tiff_mul_size((size_t)count,
                           12,
                           &entries_size))
            return 0;

        if (!tiff_add_size(entries_size,
                           6,
                           &total_size))
            return 0;

        if (!tiff_range_ok(tiff, offset, total_size))
            return 0;

        offset = tiff_u32(
            tiff,
            tiff->data + offset + 2 + entries_size);

        if (offset == 0)
            return 0;
    }

    *result = offset;
    return 1;
}


/* ------------------------------------------------------------------------- */
/* Public tiff_load                                                          */
/* ------------------------------------------------------------------------- */

MiniTIFF_Image *tiff_load(const void *data,
                      size_t size,
                      unsigned page_index)
{
    TIFF_Context tiff;
    TIFF_Page page;
    unsigned long ifd;
    MiniTIFF_Image *image;

    memset(&tiff, 0, sizeof(tiff));
    memset(&page, 0, sizeof(page));

    if (!data || size < 8)
        return NULL;

    tiff.data = (const unsigned char *)data;
    tiff.size = size;

    /* Byte order. */
    if (tiff.data[0] == 'I' &&
        tiff.data[1] == 'I') {
        tiff.little_endian = 1;
    }
    else if (tiff.data[0] == 'M' &&
             tiff.data[1] == 'M') {
        tiff.little_endian = 0;
    }
    else {
        return NULL;
    }

    /* Classic TIFF magic number. */
    if (tiff_u16(&tiff, tiff.data + 2) != 42)
        return NULL;

    tiff.first_ifd = tiff_u32(&tiff,
                              tiff.data + 4);

    if (tiff.first_ifd == 0)
        return NULL;

    if (!tiff_find_ifd(&tiff,
                       page_index,
                       &ifd))
        return NULL;

    if (!tiff_parse_ifd(&tiff,
                        ifd,
                        &page)) {
        tiff_page_free(&page);
        return NULL;
    }

    image = tiff_decode_page(&tiff, &page);

    tiff_page_free(&page);

    return image;
}


/* ------------------------------------------------------------------------- */
/* Public tiff_load_file                                                     */
/* ------------------------------------------------------------------------- */

MiniTIFF_Image *tiff_load_file(const char *filename,
                           unsigned page_index)
{
    FILE *file;
    long file_size_long;
    size_t file_size;
    unsigned char *data;
    MiniTIFF_Image *image;

    if (!filename)
        return NULL;

    file = fopen(filename, "rb");
    if (!file)
        return NULL;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    file_size_long = ftell(file);
    if (file_size_long < 0) {
        fclose(file);
        return NULL;
    }

    file_size = (size_t)file_size_long;

    if (file_size == 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    data = (unsigned char *)malloc(file_size);
    if (!data) {
        fclose(file);
        return NULL;
    }

    if (fread(data, 1, file_size, file) != file_size) {
        free(data);
        fclose(file);
        return NULL;
    }

    fclose(file);

    image = tiff_load(data,
                      file_size,
                      page_index);

    free(data);

    return image;
}


/* ------------------------------------------------------------------------- */
/* Public tiff_free                                                          */
/* ------------------------------------------------------------------------- */

void tiff_free(MiniTIFF_Image *image)
{
    if (!image)
        return;

    free(image->pixels);
    free(image);
}


/* ------------------------------------------------------------------------- */
/* Optional test program                                                     */
/* ------------------------------------------------------------------------- */

#ifdef TIFF_TEST

int main(int argc, char **argv)
{
    MiniTIFF_Image *image;
    unsigned page;

    if (argc < 2) {
        fprintf(stderr,
                "usage: %s file.tif [page]\n",
                argv[0]);
        return 2;
    }

    page = 0;
    if (argc >= 3)
        page = (unsigned)strtoul(argv[2], NULL, 10);

    image = tiff_load_file(argv[1], page);

    if (!image) {
        fprintf(stderr, "TIFF decode failed\n");
        return 1;
    }

    printf("%lu x %lu RGBA8\n",
           image->width,
           image->height);

    tiff_free(image);
    return 0;
}

#endif /* TIFF_TEST */

#endif /* MINITIFF_IMPLEMENTATION */

#endif /* _MINITFF_H */
