/*
    minitiff.h

    Small, human-readable TIFF decoder.

    C89-compatible core. No C99 types, declarations in for-loops, // comments,
    or C99-only language features are used.

    Output is always RGBA8.

    Supported by the core decoder:

      - Classic TIFF (version 42), not BigTIFF
      - Little-endian (II) and big-endian (MM)
      - Multiple IFDs/pages and page-count API
      - Strips and tiled images
      - RowsPerStrip
      - 1, 2, 4, 8 and 16 bit unsigned integer samples
      - FillOrder = 1 and 2 for packed samples
      - MaxSampleValue scaling for reduced-range samples
      - 1, 2, 4 and 8 bit palette images
      - 8 and 16 bit RGB and RGBA
      - PlanarConfiguration 1 and 2 (8/16-bit separate planes)
      - CMYK (PhotometricInterpretation = 5)
      - Orientation (tag 274), with pixels normalized to the displayed orientation
      - Generic UINT/STRING/raw tag access
      - Compression = 1   (none)
      - Compression = 3 and 4 (CCITT)
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

        Use stb_image's internal zlib decoder for Deflate
        (Compression = 8 and 32946). This is deliberately optional because
        stb_image's internal zlib routines are not a stable public API.

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

        cc -DMINITIFF_USE_STB_IMAGE -c minitiff.h

    The application must arrange for stb_image's implementation to be built
    exactly once, for example:

        #define STB_IMAGE_IMPLEMENTATION
        #include "stb_image.h"

    Optional stb_image build:

        #define STB_IMAGE_IMPLEMENTATION
        #define MINITIFF_USE_STB_IMAGE
        #define MINITIFF_USE_STB_ZLIB
        #include "minitiff.h"
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
            #include "minitiff.h"

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
    unsigned short orientation;
    unsigned char *pixels;
} MiniTIFF_Image;

MiniTIFF_Image *tiff_load(const void *data, size_t size, unsigned page_index);
MiniTIFF_Image *tiff_load_file(const char *filename, unsigned page_index);
void tiff_free(MiniTIFF_Image *image);

int tiff_get_page_count(const void *data, size_t size);
int tiff_get_page_count_file(const char *filename);
int tiff_is_valid(const void *data, size_t size);
int tiff_is_valid_file(const char *filename);

int tiff_get_tag_u32(const void *data, size_t size,
                     unsigned page_index, unsigned short tag,
                     unsigned long index, unsigned long *value);
int tiff_get_tag_string(const void *data, size_t size,
                        unsigned page_index, unsigned short tag,
                        char *buffer, size_t buffer_size);
int tiff_get_tag_data(const void *data, size_t size,
                      unsigned page_index, unsigned short tag,
                      unsigned char *buffer, size_t buffer_size,
                      size_t *tag_size);


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

    unsigned long tile_width;
    unsigned long tile_length;
    unsigned long *tile_offsets;
    unsigned long *tile_byte_counts;
    unsigned long tile_count;

    unsigned short compression;
    unsigned short photometric;
    unsigned short samples_per_pixel;
    unsigned short planar_config;
    unsigned short predictor;
    unsigned short orientation;
    unsigned short sample_format;
    unsigned short fill_order;
    unsigned long max_sample_value;
    unsigned long group3_options;
    unsigned long group4_options;

    unsigned short bits_per_sample[4];
    unsigned short bits_count;

    unsigned short extra_samples[4];
    unsigned short extra_count;

    unsigned short rgb555;

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
    free(page->tile_offsets);
    free(page->tile_byte_counts);
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
    page->orientation = 1;
    page->sample_format = 1;
    page->fill_order = 1;

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

        case 274: /* Orientation */
            if (!tiff_entry_get_u32(tiff, &entry, 0, &value) ||
                value > 65535UL)
                return 0;
            page->orientation = (unsigned short)value;
            break;

        case 317: /* Predictor */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &value) ||
                value > 65535UL)
                return 0;

            page->predictor =
                (unsigned short)value;
            break;

        case 266: /* FillOrder */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &value) ||
                value > 65535UL)
                return 0;

            page->fill_order =
                (unsigned short)value;
            break;

        case 281: /* MaxSampleValue */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &value) ||
                value > 65535UL)
                return 0;

            page->max_sample_value = value;
            break;

        case 292: /* Group3Options */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &page->group3_options))
                return 0;
            break;

        case 293: /* Group4Options */
            if (!tiff_entry_get_u32(tiff, &entry, 0,
                                    &page->group4_options))
                return 0;
            break;

        case 339: /* SampleFormat */
            if (!tiff_entry_get_u32(tiff, &entry, 0, &value) ||
                value > 65535UL)
                return 0;
            page->sample_format = (unsigned short)value;
            break;

        case 322: /* TileWidth */
            if (!tiff_entry_get_u32(tiff, &entry, 0, &page->tile_width))
                return 0;
            break;

        case 323: /* TileLength */
            if (!tiff_entry_get_u32(tiff, &entry, 0, &page->tile_length))
                return 0;
            break;

        case 324: /* TileOffsets */
            if (!tiff_load_u32_array(tiff, &entry, &page->tile_offsets,
                                     &page->tile_count))
                return 0;
            break;

        case 325: /* TileByteCounts */
            {
                unsigned long count;
                if (!tiff_load_u32_array(tiff, &entry, &page->tile_byte_counts, &count))
                    return 0;
                if (page->tile_count != 0 && page->tile_count != count)
                    return 0;
                page->tile_count = count;
            }
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

    if (page->max_sample_value == 0)
        page->max_sample_value =
            (1UL << page->bits_per_sample[0]) - 1UL;

    if (page->width == 0 ||
        page->height == 0)
        return 0;

    if (page->samples_per_pixel == 0 ||
        page->samples_per_pixel > 4)
        return 0;

    if (page->planar_config != 1 &&
        page->planar_config != 2)
        return 0;

    if (page->planar_config == 2 &&
        page->bits_per_sample[0] != 8 &&
        page->bits_per_sample[0] != 16)
        return 0;

    if (page->planar_config == 2 &&
        (page->compression == 6 || page->compression == 7))
        return 0;

    if (page->orientation < 1 || page->orientation > 8)
        return 0;

    if (page->sample_format != 1)
        return 0;

    if (page->max_sample_value == 0 ||
        page->max_sample_value >
        ((1UL << page->bits_per_sample[0]) - 1UL))
        return 0;

    if (page->compression != 1 &&
        page->compression != 3 &&
        page->compression != 4 &&
        page->compression != 5 &&
        page->compression != 6 &&
        page->compression != 7 &&
        page->compression != 8 &&
        page->compression != 32773 &&
        page->compression != 32946)
        return 0;

    if (page->fill_order != 1 &&
        page->fill_order != 2)
        return 0;

    if (page->predictor != 1 &&
        page->predictor != 2)
        return 0;

    if (page->photometric > 5)
        return 0;

    if ((page->compression == 3 || page->compression == 4) &&
        page->samples_per_pixel != 1)
        return 0;

    if (page->photometric == 2 &&
        !page->rgb555 &&
        page->samples_per_pixel < 3)
        return 0;

    if (page->photometric == 3 &&
        page->color_map == NULL)
        return 0;

    if (page->photometric == 5 &&
        page->samples_per_pixel != 4)
        return 0;

    if (page->tile_width != 0 || page->tile_length != 0 ||
        page->tile_offsets != NULL || page->tile_byte_counts != NULL) {
        if (page->tile_width == 0 || page->tile_length == 0 ||
            page->tile_count == 0 || page->tile_offsets == NULL ||
            page->tile_byte_counts == NULL)
            return 0;
    }

    if (page->tile_count == 0 &&
        (page->strip_count == 0 || page->strip_offsets == NULL ||
         page->strip_byte_counts == NULL || page->rows_per_strip == 0))
        return 0;

    if (page->tile_count != 0 && page->strip_count != 0)
        return 0;

    if (page->bits_count != 1 &&
        page->bits_count != page->samples_per_pixel)
        return 0;

    for (i = 0; i < page->bits_count; ++i) {
        unsigned short bits = page->bits_per_sample[i];

        if (bits < 1 || bits > 16)
            return 0;

        if (i != 0 && bits != page->bits_per_sample[0])
            return 0;

        if ((page->compression == 3 || page->compression == 4) &&
            bits != 1)
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

    /*
        Heuristic for antique TIFFs that store RGB555 data but tag it
        as 16-bit grayscale.  Detect: BitsPerSample=16, SamplesPerPixel=1,
        PhotometricInterpretation=0 or 1, MaxSampleValue=32767.
    */
    if (page->bits_per_sample[0] == 16 &&
        page->samples_per_pixel == 1 &&
        (page->photometric == 0 || page->photometric == 1) &&
        page->max_sample_value == 32767UL) {
        page->rgb555 = 1;
        page->photometric = 2;
    }

    return 1;
}


/* ------------------------------------------------------------------------- */
/* CCITT Group 3/4 fax decoder                                              */
/* ------------------------------------------------------------------------- */

typedef struct TIFF_CCITT_Code {
    unsigned char bits;
    unsigned long code;
    unsigned short run;
} TIFF_CCITT_Code;

typedef struct TIFF_CCITT_Mode {
    unsigned char bits;
    unsigned long code;
    int value;
} TIFF_CCITT_Mode;

static const TIFF_CCITT_Code tiff_ccitt_white[] = {
    {8, 53UL, 0}, /* 00110101 */
    {6, 7UL, 1}, /* 000111 */
    {4, 7UL, 2}, /* 0111 */
    {4, 8UL, 3}, /* 1000 */
    {4, 11UL, 4}, /* 1011 */
    {4, 12UL, 5}, /* 1100 */
    {4, 14UL, 6}, /* 1110 */
    {4, 15UL, 7}, /* 1111 */
    {5, 19UL, 8}, /* 10011 */
    {5, 20UL, 9}, /* 10100 */
    {5, 7UL, 10}, /* 00111 */
    {5, 8UL, 11}, /* 01000 */
    {6, 8UL, 12}, /* 001000 */
    {6, 3UL, 13}, /* 000011 */
    {6, 52UL, 14}, /* 110100 */
    {6, 53UL, 15}, /* 110101 */
    {6, 42UL, 16}, /* 101010 */
    {6, 43UL, 17}, /* 101011 */
    {7, 39UL, 18}, /* 0100111 */
    {7, 12UL, 19}, /* 0001100 */
    {7, 8UL, 20}, /* 0001000 */
    {7, 23UL, 21}, /* 0010111 */
    {7, 3UL, 22}, /* 0000011 */
    {7, 4UL, 23}, /* 0000100 */
    {7, 40UL, 24}, /* 0101000 */
    {7, 43UL, 25}, /* 0101011 */
    {7, 19UL, 26}, /* 0010011 */
    {7, 36UL, 27}, /* 0100100 */
    {7, 24UL, 28}, /* 0011000 */
    {8, 2UL, 29}, /* 00000010 */
    {8, 3UL, 30}, /* 00000011 */
    {8, 26UL, 31}, /* 00011010 */
    {8, 27UL, 32}, /* 00011011 */
    {8, 18UL, 33}, /* 00010010 */
    {8, 19UL, 34}, /* 00010011 */
    {8, 20UL, 35}, /* 00010100 */
    {8, 21UL, 36}, /* 00010101 */
    {8, 22UL, 37}, /* 00010110 */
    {8, 23UL, 38}, /* 00010111 */
    {8, 40UL, 39}, /* 00101000 */
    {8, 41UL, 40}, /* 00101001 */
    {8, 42UL, 41}, /* 00101010 */
    {8, 43UL, 42}, /* 00101011 */
    {8, 44UL, 43}, /* 00101100 */
    {8, 45UL, 44}, /* 00101101 */
    {8, 4UL, 45}, /* 00000100 */
    {8, 5UL, 46}, /* 00000101 */
    {8, 10UL, 47}, /* 00001010 */
    {8, 11UL, 48}, /* 00001011 */
    {8, 82UL, 49}, /* 01010010 */
    {8, 83UL, 50}, /* 01010011 */
    {8, 84UL, 51}, /* 01010100 */
    {8, 85UL, 52}, /* 01010101 */
    {8, 36UL, 53}, /* 00100100 */
    {8, 37UL, 54}, /* 00100101 */
    {8, 88UL, 55}, /* 01011000 */
    {8, 89UL, 56}, /* 01011001 */
    {8, 90UL, 57}, /* 01011010 */
    {8, 91UL, 58}, /* 01011011 */
    {8, 74UL, 59}, /* 01001010 */
    {8, 75UL, 60}, /* 01001011 */
    {8, 50UL, 61}, /* 00110010 */
    {8, 51UL, 62}, /* 00110011 */
    {8, 52UL, 63}, /* 00110100 */
    {5, 27UL, 64}, /* 11011 */
    {5, 18UL, 128}, /* 10010 */
    {6, 23UL, 192}, /* 010111 */
    {7, 55UL, 256}, /* 0110111 */
    {8, 54UL, 320}, /* 00110110 */
    {8, 55UL, 384}, /* 00110111 */
    {8, 100UL, 448}, /* 01100100 */
    {8, 101UL, 512}, /* 01100101 */
    {8, 104UL, 576}, /* 01101000 */
    {8, 103UL, 640}, /* 01100111 */
    {9, 204UL, 704}, /* 011001100 */
    {9, 205UL, 768}, /* 011001101 */
    {9, 210UL, 832}, /* 011010010 */
    {9, 211UL, 896}, /* 011010011 */
    {9, 212UL, 960}, /* 011010100 */
    {9, 213UL, 1024}, /* 011010101 */
    {9, 214UL, 1088}, /* 011010110 */
    {9, 215UL, 1152}, /* 011010111 */
    {9, 216UL, 1216}, /* 011011000 */
    {9, 217UL, 1280}, /* 011011001 */
    {9, 218UL, 1344}, /* 011011010 */
    {9, 219UL, 1408}, /* 011011011 */
    {9, 152UL, 1472}, /* 010011000 */
    {9, 153UL, 1536}, /* 010011001 */
    {9, 154UL, 1600}, /* 010011010 */
    {6, 24UL, 1664}, /* 011000 */
    {9, 155UL, 1728}, /* 010011011 */
    {11, 8UL, 1792}, /* 00000001000 */
    {11, 12UL, 1856}, /* 00000001100 */
    {11, 13UL, 1920}, /* 00000001101 */
    {12, 18UL, 1984}, /* 000000010010 */
    {12, 19UL, 2048}, /* 000000010011 */
    {12, 20UL, 2112}, /* 000000010100 */
    {12, 21UL, 2176}, /* 000000010101 */
    {12, 22UL, 2240}, /* 000000010110 */
    {12, 23UL, 2304}, /* 000000010111 */
    {12, 28UL, 2368}, /* 000000011100 */
    {12, 29UL, 2432}, /* 000000011101 */
    {12, 30UL, 2496}, /* 000000011110 */
    {12, 31UL, 2560}, /* 000000011111 */
};

static const TIFF_CCITT_Code tiff_ccitt_black[] = {
    {10, 55UL, 0}, /* 0000110111 */
    {3, 2UL, 1}, /* 010 */
    {2, 3UL, 2}, /* 11 */
    {2, 2UL, 3}, /* 10 */
    {3, 3UL, 4}, /* 011 */
    {4, 3UL, 5}, /* 0011 */
    {4, 2UL, 6}, /* 0010 */
    {5, 3UL, 7}, /* 00011 */
    {6, 5UL, 8}, /* 000101 */
    {6, 4UL, 9}, /* 000100 */
    {7, 4UL, 10}, /* 0000100 */
    {7, 5UL, 11}, /* 0000101 */
    {7, 7UL, 12}, /* 0000111 */
    {8, 4UL, 13}, /* 00000100 */
    {8, 7UL, 14}, /* 00000111 */
    {9, 24UL, 15}, /* 000011000 */
    {10, 23UL, 16}, /* 0000010111 */
    {10, 24UL, 17}, /* 0000011000 */
    {10, 8UL, 18}, /* 0000001000 */
    {11, 103UL, 19}, /* 00001100111 */
    {11, 104UL, 20}, /* 00001101000 */
    {11, 108UL, 21}, /* 00001101100 */
    {11, 55UL, 22}, /* 00000110111 */
    {11, 40UL, 23}, /* 00000101000 */
    {11, 23UL, 24}, /* 00000010111 */
    {11, 24UL, 25}, /* 00000011000 */
    {12, 202UL, 26}, /* 000011001010 */
    {12, 203UL, 27}, /* 000011001011 */
    {12, 204UL, 28}, /* 000011001100 */
    {12, 205UL, 29}, /* 000011001101 */
    {12, 104UL, 30}, /* 000001101000 */
    {12, 105UL, 31}, /* 000001101001 */
    {12, 106UL, 32}, /* 000001101010 */
    {12, 107UL, 33}, /* 000001101011 */
    {12, 210UL, 34}, /* 000011010010 */
    {12, 211UL, 35}, /* 000011010011 */
    {12, 212UL, 36}, /* 000011010100 */
    {12, 213UL, 37}, /* 000011010101 */
    {12, 214UL, 38}, /* 000011010110 */
    {12, 215UL, 39}, /* 000011010111 */
    {12, 108UL, 40}, /* 000001101100 */
    {12, 109UL, 41}, /* 000001101101 */
    {12, 218UL, 42}, /* 000011011010 */
    {12, 219UL, 43}, /* 000011011011 */
    {12, 84UL, 44}, /* 000001010100 */
    {12, 85UL, 45}, /* 000001010101 */
    {12, 86UL, 46}, /* 000001010110 */
    {12, 87UL, 47}, /* 000001010111 */
    {12, 100UL, 48}, /* 000001100100 */
    {12, 101UL, 49}, /* 000001100101 */
    {12, 82UL, 50}, /* 000001010010 */
    {12, 83UL, 51}, /* 000001010011 */
    {12, 36UL, 52}, /* 000000100100 */
    {12, 55UL, 53}, /* 000000110111 */
    {12, 56UL, 54}, /* 000000111000 */
    {12, 39UL, 55}, /* 000000100111 */
    {12, 40UL, 56}, /* 000000101000 */
    {12, 88UL, 57}, /* 000001011000 */
    {12, 89UL, 58}, /* 000001011001 */
    {12, 43UL, 59}, /* 000000101011 */
    {12, 44UL, 60}, /* 000000101100 */
    {12, 90UL, 61}, /* 000001011010 */
    {12, 102UL, 62}, /* 000001100110 */
    {12, 103UL, 63}, /* 000001100111 */
    {10, 15UL, 64}, /* 0000001111 */
    {12, 200UL, 128}, /* 000011001000 */
    {12, 201UL, 192}, /* 000011001001 */
    {12, 91UL, 256}, /* 000001011011 */
    {12, 51UL, 320}, /* 000000110011 */
    {12, 52UL, 384}, /* 000000110100 */
    {12, 53UL, 448}, /* 000000110101 */
    {13, 108UL, 512}, /* 0000001101100 */
    {13, 109UL, 576}, /* 0000001101101 */
    {13, 74UL, 640}, /* 0000001001010 */
    {13, 75UL, 704}, /* 0000001001011 */
    {13, 76UL, 768}, /* 0000001001100 */
    {13, 77UL, 832}, /* 0000001001101 */
    {13, 114UL, 896}, /* 0000001110010 */
    {13, 115UL, 960}, /* 0000001110011 */
    {13, 116UL, 1024}, /* 0000001110100 */
    {13, 117UL, 1088}, /* 0000001110101 */
    {13, 118UL, 1152}, /* 0000001110110 */
    {13, 119UL, 1216}, /* 0000001110111 */
    {13, 82UL, 1280}, /* 0000001010010 */
    {13, 83UL, 1344}, /* 0000001010011 */
    {13, 84UL, 1408}, /* 0000001010100 */
    {13, 85UL, 1472}, /* 0000001010101 */
    {13, 90UL, 1536}, /* 0000001011010 */
    {13, 91UL, 1600}, /* 0000001011011 */
    {13, 100UL, 1664}, /* 0000001100100 */
    {13, 101UL, 1728}, /* 0000001100101 */
    {11, 8UL, 1792}, /* 00000001000 */
    {11, 12UL, 1856}, /* 00000001100 */
    {11, 13UL, 1920}, /* 00000001101 */
    {12, 18UL, 1984}, /* 000000010010 */
    {12, 19UL, 2048}, /* 000000010011 */
    {12, 20UL, 2112}, /* 000000010100 */
    {12, 21UL, 2176}, /* 000000010101 */
    {12, 22UL, 2240}, /* 000000010110 */
    {12, 23UL, 2304}, /* 000000010111 */
    {12, 28UL, 2368}, /* 000000011100 */
    {12, 29UL, 2432}, /* 000000011101 */
    {12, 30UL, 2496}, /* 000000011110 */
    {12, 31UL, 2560}, /* 000000011111 */
};


static const TIFF_CCITT_Mode tiff_ccitt_mode[] = {
    {1, 1UL, 0},  /* vertical 0 */
    {3, 3UL, 3},  /* vertical +1 */
    {3, 2UL, 4},  /* vertical -1 */
    {3, 1UL, 1},  /* horizontal */
    {4, 1UL, 2},  /* pass */
    {6, 3UL, 5},  /* vertical +2 */
    {6, 2UL, 6},  /* vertical -2 */
    {7, 3UL, 7},  /* vertical +3 */
    {7, 2UL, 8},  /* vertical -3 */
};

static int tiff_ccitt_get_bit(const unsigned char *src,
                              size_t src_size,
                              size_t *bit_pos,
                              unsigned short fill_order,
                              int *bit)
{
    size_t byte_pos;
    unsigned int bit_in_byte;
    unsigned char mask;

    if (*bit_pos >= src_size * 8)
        return 0;

    byte_pos = *bit_pos >> 3;
    bit_in_byte = (unsigned int)(*bit_pos & 7);

    if (fill_order == 2)
        mask = (unsigned char)(1U << bit_in_byte);
    else
        mask = (unsigned char)(0x80U >> bit_in_byte);

    *bit = (src[byte_pos] & mask) != 0;
    ++*bit_pos;
    return 1;
}

static int tiff_ccitt_find_code(const unsigned char *src,
                                size_t src_size,
                                size_t *bit_pos,
                                unsigned short fill_order,
                                const TIFF_CCITT_Code *table,
                                size_t table_count,
                                unsigned long *value)
{
    unsigned long code;
    unsigned int bits;
    size_t i;
    int bit;

    code = 0;

    for (bits = 1; bits <= 13; ++bits) {
        if (!tiff_ccitt_get_bit(src, src_size, bit_pos,
                                fill_order, &bit))
            return 0;

        code = (code << 1) | (unsigned long)bit;

        for (i = 0; i < table_count; ++i) {
            if (table[i].bits == bits &&
                table[i].code == code) {
                *value = table[i].run;
                return 1;
            }
        }
    }

    return 0;
}

static int tiff_ccitt_read_run(const unsigned char *src,
                               size_t src_size,
                               size_t *bit_pos,
                               unsigned short fill_order,
                               int white,
                               unsigned long *run)
{
    const TIFF_CCITT_Code *table;
    size_t table_count;
    unsigned long value;
    unsigned long total;

    if (white) {
        table = tiff_ccitt_white;
        table_count = sizeof(tiff_ccitt_white) /
                      sizeof(tiff_ccitt_white[0]);
    }
    else {
        table = tiff_ccitt_black;
        table_count = sizeof(tiff_ccitt_black) /
                      sizeof(tiff_ccitt_black[0]);
    }

    total = 0;

    do {
        if (!tiff_ccitt_find_code(src, src_size, bit_pos,
                                  fill_order, table,
                                  table_count, &value))
            return 0;

        if (value == (unsigned long)-1)
            return 0;

        total += value;

        /* A terminating code is less than 64. */
        if (value < 64UL)
            break;
    } while (total <= 2623UL);

    if (total > 2623UL)
        return 0;

    *run = total;
    return 1;
}

static void tiff_ccitt_set_bit(unsigned char *row,
                               unsigned long x,
                               int value)
{
    unsigned char mask;

    mask = (unsigned char)(0x80U >> (x & 7));

    if (value)
        row[x >> 3] |= mask;
    else
        row[x >> 3] &= (unsigned char)~mask;
}

static int tiff_ccitt_read_eol(const unsigned char *src,
                               size_t src_size,
                               size_t *bit_pos,
                               unsigned short fill_order)
{
    unsigned long window;
    unsigned int count;
    int bit;

    window = 0;
    count = 0;

    while (*bit_pos < src_size * 8) {
        if (!tiff_ccitt_get_bit(src, src_size, bit_pos,
                                fill_order, &bit))
            return 0;

        window = ((window << 1) | (unsigned long)bit) & 0xFFFUL;

        if (count < 12)
            ++count;

        if (count == 12 && window == 1UL)
            return 1;
    }

    return 0;
}

static void tiff_ccitt_fill_run(unsigned char *row,
                                unsigned long start,
                                unsigned long length,
                                int white,
                                unsigned long width)
{
    unsigned long end;
    unsigned long x;

    end = start + length;
    if (end > width)
        end = width;

    for (x = start; x < end; ++x)
        tiff_ccitt_set_bit(row, x, white ? 1 : 0);
}

static int tiff_ccitt_decode_1d_line(const unsigned char *src,
                                     size_t src_size,
                                     size_t *bit_pos,
                                     unsigned short fill_order,
                                     unsigned char *row,
                                     unsigned long width)
{
    unsigned long x;
    unsigned long run;
    int white;

    memset(row, 0, (size_t)((width + 7UL) / 8UL));

    x = 0;
    white = 1;

    while (x < width) {
        if (!tiff_ccitt_read_run(src, src_size, bit_pos,
                                 fill_order, white, &run))
            return 0;

        if (run > width - x)
            return 0;

        tiff_ccitt_fill_run(row, x, run, white, width);
        x += run;
        white = !white;
    }

    return 1;
}

static unsigned long tiff_ccitt_find_b1(const unsigned char *ref,
                                        unsigned long width,
                                        unsigned long a0,
                                        int white)
{
    unsigned long x;

    x = a0 + 1UL;

    while (x < width) {
        int left;
        int current;

        left = (x == 0) ? white :
               ((ref[(x - 1UL) >> 3] &
                 (0x80U >> ((x - 1UL) & 7))) != 0);

        current = ((ref[x >> 3] &
                    (0x80U >> (x & 7))) != 0);

        if (left == white && current != white)
            break;

        ++x;
    }

    return x;
}

static unsigned long tiff_ccitt_find_b2(const unsigned char *ref,
                                        unsigned long width,
                                        unsigned long b1,
                                        int white)
{
    unsigned long x;

    x = b1 + 1UL;

    while (x < width) {
        int left;
        int current;

        left = ((ref[(x - 1UL) >> 3] &
                 (0x80U >> ((x - 1UL) & 7))) != 0);
        current = ((ref[x >> 3] &
                    (0x80U >> (x & 7))) != 0);

        if (left != white && current == white)
            break;

        ++x;
    }

    return x;
}

static int tiff_ccitt_read_mode(const unsigned char *src,
                                size_t src_size,
                                size_t *bit_pos,
                                unsigned short fill_order,
                                int *mode)
{
    unsigned long code;
    unsigned int bits;
    size_t i;

    code = 0;

    for (bits = 1; bits <= 7; ++bits) {
        int bit;

        if (!tiff_ccitt_get_bit(src, src_size, bit_pos,
                                fill_order, &bit))
            return 0;

        code = (code << 1) | (unsigned long)bit;

        for (i = 0; i < sizeof(tiff_ccitt_mode) /
                        sizeof(tiff_ccitt_mode[0]); ++i) {
            if (tiff_ccitt_mode[i].bits == bits &&
                tiff_ccitt_mode[i].code == code) {
                *mode = (int)tiff_ccitt_mode[i].value;
                return 1;
            }
        }
    }

    return 0;
}

static int tiff_ccitt_decode_2d_line(const unsigned char *src,
                                     size_t src_size,
                                     size_t *bit_pos,
                                     unsigned short fill_order,
                                     const unsigned char *reference,
                                     unsigned char *row,
                                     unsigned long width)
{
    unsigned long a0;
    unsigned long b1;
    unsigned long b2;
    unsigned long n1;
    unsigned long n2;
    unsigned long x;
    int white;
    int mode;

    memset(row, 0, (size_t)((width + 7UL) / 8UL));

    a0 = 0;
    white = 1;

    while (a0 < width) {
        if (!tiff_ccitt_read_mode(src, src_size, bit_pos,
                                  fill_order, &mode))
            return 0;

        if (mode == 0) { /* Vertical 0 */
            b1 = tiff_ccitt_find_b1(reference, width, a0, white);

            if (b1 < a0)
                return 0;

            tiff_ccitt_fill_run(row, a0, b1 - a0,
                                white, width);
            a0 = b1;
            white = !white;
        }
        else if (mode >= 3 && mode <= 8) {
            b1 = tiff_ccitt_find_b1(reference, width, a0, white);

            if (mode == 3)
                b1 += 1UL;
            else if (mode == 4 && b1 > 0)
                b1 -= 1UL;
            else if (mode == 5)
                b1 += 2UL;
            else if (mode == 6 && b1 >= 2UL)
                b1 -= 2UL;
            else if (mode == 7)
                b1 += 3UL;
            else if (mode == 8 && b1 >= 3UL)
                b1 -= 3UL;

            if (b1 > width)
                return 0;

            tiff_ccitt_fill_run(row, a0, b1 - a0,
                                white, width);
            a0 = b1;
            white = !white;
        }
        else if (mode == 1) { /* Horizontal */
            if (!tiff_ccitt_read_run(src, src_size, bit_pos,
                                     fill_order, white, &n1))
                return 0;

            if (n1 > width - a0)
                return 0;

            tiff_ccitt_fill_run(row, a0, n1,
                                white, width);
            a0 += n1;
            white = !white;

            if (!tiff_ccitt_read_run(src, src_size, bit_pos,
                                     fill_order, white, &n2))
                return 0;

            if (n2 > width - a0)
                return 0;

            tiff_ccitt_fill_run(row, a0, n2,
                                white, width);
            a0 += n2;
            white = !white;
        }
        else if (mode == 2) { /* Pass */
            b1 = tiff_ccitt_find_b1(reference, width, a0, white);
            b2 = tiff_ccitt_find_b2(reference, width, b1, white);

            if (b2 < a0)
                return 0;

            tiff_ccitt_fill_run(row, a0, b2 - a0,
                                white, width);
            a0 = b2;
        }
        else {
            return 0;
        }
    }

    /* Clear unused bits in the final byte. */
    x = width & 7UL;
    if (x != 0)
        row[width >> 3] &= (unsigned char)(0xFFU << (8U - x));

    return 1;
}

static int tiff_ccitt_decode(const unsigned char *src,
                             size_t src_size,
                             unsigned char *dst,
                             size_t dst_size,
                             unsigned long width,
                             unsigned long rows,
                             unsigned short compression,
                             unsigned long group3_options,
                             unsigned long group4_options,
                             unsigned short fill_order,
                             unsigned short photometric)
{
    size_t row_size;
    size_t bit_pos;
    unsigned long y;
    unsigned char *reference;
    int use_2d;
    int byte_align;

    (void)group4_options;

    row_size = (size_t)((width + 7UL) / 8UL);

    {
        size_t expected_size;

        if (!tiff_mul_size(row_size, (size_t)rows,
                           &expected_size))
            return 0;

        if (dst_size != expected_size)
            return 0;
    }

    reference = (unsigned char *)malloc(row_size);
    if (!reference)
        return 0;

    memset(reference, 0, row_size);
    bit_pos = 0;
    use_2d = (compression == 3 &&
              (group3_options & 1UL) != 0);
    byte_align = (compression == 3 &&
                  (group3_options & 4UL) != 0);

    for (y = 0; y < rows; ++y) {
        unsigned char *row;
        int ok;

        row = dst + (size_t)y * row_size;

        if (compression == 3) {
            if (!tiff_ccitt_read_eol(src, src_size, &bit_pos,
                                     fill_order)) {
                free(reference);
                return 0;
            }

            if (byte_align)
                bit_pos = (bit_pos + 7U) & ~(size_t)7U;
        }

        if (compression == 3 && use_2d) {
            int two_d_line;
            int tag_bit;

            if (!tiff_ccitt_get_bit(src, src_size, &bit_pos,
                                    fill_order, &tag_bit)) {
                free(reference);
                return 0;
            }

            /* Group 3 mixed mode: 0 = 2-D, 1 = 1-D. */
            two_d_line = (tag_bit == 0);

            if (two_d_line) {
                ok = tiff_ccitt_decode_2d_line(
                    src, src_size, &bit_pos, fill_order,
                    reference, row, width);
            }
            else {
                ok = tiff_ccitt_decode_1d_line(
                    src, src_size, &bit_pos, fill_order,
                    row, width);
            }
        }
        else if (compression == 4) {
            ok = tiff_ccitt_decode_2d_line(
                src, src_size, &bit_pos, fill_order,
                reference, row, width);
        }
        else {
            ok = tiff_ccitt_decode_1d_line(
                src, src_size, &bit_pos, fill_order,
                row, width);
        }

        if (!ok) {
            free(reference);
            return 0;
        }

        memcpy(reference, row, row_size);
    }

    if (photometric == 0) {
        size_t i;

        for (i = 0; i < dst_size; ++i)
            dst[i] = (unsigned char)~dst[i];
    }

    free(reference);
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
    TIFF_LZW *lzw;
    size_t position;
    size_t output;
    int first_char;

    lzw = (TIFF_LZW *)calloc(1, sizeof(*lzw));
    if (!lzw)
        return 0;

    lzw->code_size = 9;
    lzw->next_code = 258;
    lzw->old_code = -1;
    lzw->clear_code = 256;
    lzw->end_code = 257;

    position = 0;
    output = 0;
    first_char = -1;

    while (output < dst_size) {
        int code;

        code = tiff_lzw_get_code(lzw,
                                 src,
                                 src_size,
                                 &position);

        if (code < 0) {
            free(lzw);
            return 0;
        }

        if (code == lzw->clear_code) {
            lzw->code_size = 9;
            lzw->next_code = 258;
            lzw->old_code = -1;
            first_char = -1;
            continue;
        }

        if (code == lzw->end_code)
            break;

        /* First code after CLEAR. */
        if (lzw->old_code < 0) {
            if (code < 0 || code > 255) {
                free(lzw);
                return 0;
            }

            if (output >= dst_size) {
                free(lzw);
                return 0;
            }

            dst[output++] = (unsigned char)code;

            first_char = code;
            lzw->old_code = code;
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
            if (code == lzw->next_code) {
                if (first_char < 0 ||
                    stack_count >= 4096) {
                    free(lzw);
                    return 0;
                }

                lzw->stack[stack_count++] =
                    (unsigned char)first_char;

                current = lzw->old_code;
            }
            else if (code > lzw->next_code ||
                     code >= 4096) {
                free(lzw);
                return 0;
            }

            while (current >= 256) {
                if (current < 258 ||
                    current >= 4096 ||
                    stack_count >= 4096) {
                    free(lzw);
                    return 0;
                }

                lzw->stack[stack_count++] =
                    lzw->suffix[current];

                current = lzw->prefix[current];
            }

            if (current < 0 ||
                current > 255 ||
                stack_count >= 4096) {
                free(lzw);
                return 0;
            }

            first_char = current;

            lzw->stack[stack_count++] =
                (unsigned char)current;

            while (stack_count != 0) {
                if (output >= dst_size) {
                    free(lzw);
                    return 0;
                }

                dst[output++] =
                    lzw->stack[--stack_count];
            }

            /*
                Add old_string + first_char to the dictionary.
            */
            if (lzw->next_code < 4096) {
                lzw->prefix[lzw->next_code] =
                    lzw->old_code;

                lzw->suffix[lzw->next_code] =
                    (unsigned char)first_char;

                ++lzw->next_code;

                /* TIFF uses early-change. */
                if (lzw->next_code ==
                    ((1 << lzw->code_size) - 1) &&
                    lzw->code_size < 12)
                    ++lzw->code_size;
            }

            lzw->old_code = code;
        }
    }

    {
        int success;
        success = output == dst_size;
        free(lzw);
        return success;
    }
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
                                     unsigned short bits,
                                     unsigned short fill_order)
{
    unsigned int byte_offset;
    unsigned int bit_offset;
    unsigned int bytes_needed;
    unsigned long val;
    unsigned int shift;
    unsigned int i;

    if (bits == 8)
        return row[bit_position >> 3];

    byte_offset = (unsigned int)(bit_position >> 3);
    bit_offset = (unsigned int)(bit_position & 7);

    bytes_needed = (bit_offset + bits + 7U) / 8U;
    if (bytes_needed > 2U)
        bytes_needed = 2U;

    val = 0;
    for (i = 0; i < bytes_needed; ++i)
        val = (val << 8) | (unsigned long)row[byte_offset + i];

    /*
        FillOrder = 2 with byte-aligned samples (bits divides 8):
        swap the order of samples within each byte without changing
        their values.  For example, 4-bit: swap nibble order so
        pixel 0 reads the low nibble instead of the high nibble.
    */
    if (fill_order == 2 && bytes_needed == 1 && (8U % bits) == 0U) {
        unsigned int samples_per_byte = 8U / bits;
        unsigned int sample_index = bit_offset / bits;
        shift = bits * sample_index;
    }
    else {
        shift = (unsigned int)(bytes_needed * 8U - bit_offset - bits);
    }

    return (val >> shift) & ((1UL << bits) - 1UL);
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
/* 16-bit sample helpers                                                    */

static unsigned short tiff_get_u16_sample(const TIFF_Context *tiff,
                                          const unsigned char *data)
{
    return tiff_u16(tiff, data);
}

static void tiff_put_u16_sample(const TIFF_Context *tiff,
                                unsigned char *data,
                                unsigned short value)
{
    if (tiff->little_endian) {
        data[0] = (unsigned char)(value & 255U);
        data[1] = (unsigned char)(value >> 8);
    }
    else {
        data[0] = (unsigned char)(value >> 8);
        data[1] = (unsigned char)(value & 255U);
    }
}

static unsigned char tiff_scale_u16(unsigned short value,
                                     unsigned long maximum)
{
    unsigned long scaled;

    if (maximum == 0)
        maximum = 65535UL;

    if (maximum == 65535UL)
        return (unsigned char)(value >> 8);

    scaled = ((unsigned long)value * 255UL +
              maximum / 2UL) / maximum;

    if (scaled > 255UL)
        scaled = 255UL;

    return (unsigned char)scaled;
}

static void tiff_predictor_horizontal_16(const TIFF_Context *tiff,
                                         unsigned char *data,
                                         unsigned long width,
                                         unsigned long rows,
                                         unsigned long samples)
{
    unsigned long y;
    unsigned long x;
    size_t stride;

    stride = (size_t)width * (size_t)samples * 2;

    for (y = 0; y < rows; ++y) {
        unsigned char *row;
        row = data + (size_t)y * stride;

        for (x = samples; x < width * samples; ++x) {
            unsigned short current;
            unsigned short previous;
            unsigned short value;

            current = tiff_get_u16_sample(tiff, row + x * 2);
            previous = tiff_get_u16_sample(tiff, row + (x - samples) * 2);
            value = (unsigned short)(current + previous);
            tiff_put_u16_sample(tiff, row + x * 2, value);
        }
    }
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

static int tiff_convert_pixels(const TIFF_Context *tiff,
                               const TIFF_Page *page,
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

            if (page->photometric == 0 || page->photometric == 1) {
                if (bits == 16) {
                    const unsigned char *p;
                    unsigned short value;
                    p = row + (size_t)x * page->samples_per_pixel * 2;
                    value = tiff_get_u16_sample(tiff, p);
                    if (page->photometric == 0)
                        value = (unsigned short)(page->max_sample_value - value);
                    r = tiff_scale_u16(value, page->max_sample_value);
                    g = r;
                    b = r;
                    if (page->samples_per_pixel >= 2)
                        a = tiff_scale_u16(tiff_get_u16_sample(tiff, p + 2), page->max_sample_value);
                }
                else {
                    unsigned long value;
                    value = tiff_get_sample(row, base_bit, bits, page->fill_order);
                    if (page->photometric == 0)
                        value = ((1UL << bits) - 1UL) - value;
                    r = tiff_scale_sample(value, bits);
                    g = r;
                    b = r;
                    if (page->samples_per_pixel >= 2)
                        a = tiff_scale_sample(tiff_get_sample(row, base_bit + bits, bits, page->fill_order), bits);
                }
            }
            else if (page->photometric == 2) {
                const unsigned char *pixel;
                if (page->rgb555) {
                    unsigned long v;
                    pixel = row + (size_t)x * 2;
                    v = tiff_get_u16_sample(tiff, pixel);
                    g = (unsigned char)(((v >> 10) & 0x1FU) * 255 / 31);
                    r = (unsigned char)(((v >> 5) & 0x1FU) * 255 / 31);
                    b = (unsigned char)((v & 0x1FU) * 255 / 31);
                }
                else if (bits == 16) {
                    pixel = row + (size_t)x * page->samples_per_pixel * 2;
                    r = tiff_scale_u16(tiff_get_u16_sample(tiff, pixel), page->max_sample_value);
                    g = tiff_scale_u16(tiff_get_u16_sample(tiff, pixel + 2), page->max_sample_value);
                    b = tiff_scale_u16(tiff_get_u16_sample(tiff, pixel + 4), page->max_sample_value);
                    if (page->samples_per_pixel >= 4)
                        a = tiff_scale_u16(tiff_get_u16_sample(tiff, pixel + 6), page->max_sample_value);
                }
                else if (bits == 8) {
                    pixel = row + (size_t)x * page->samples_per_pixel;
                    r = pixel[0];
                    g = pixel[1];
                    b = pixel[2];
                    if (page->samples_per_pixel >= 4)
                        a = pixel[3];
                }
                else {
                    r = tiff_scale_sample(
                        tiff_get_sample(row, base_bit, bits, page->fill_order), bits);
                    g = tiff_scale_sample(
                        tiff_get_sample(row, base_bit + (size_t)bits, bits, page->fill_order), bits);
                    b = tiff_scale_sample(
                        tiff_get_sample(row, base_bit + (size_t)bits * 2, bits, page->fill_order), bits);
                    if (page->samples_per_pixel >= 4)
                        a = tiff_scale_sample(
                            tiff_get_sample(row, base_bit + (size_t)bits * 3, bits, page->fill_order), bits);
                }
            }
            else if (page->photometric == 3) {
                unsigned long index;
                index = tiff_get_sample(row, base_bit, bits, page->fill_order);
                r = tiff_palette_value(page, index, 0);
                g = tiff_palette_value(page, index, 1);
                b = tiff_palette_value(page, index, 2);
                if (page->samples_per_pixel >= 2)
                    a = tiff_scale_sample(tiff_get_sample(row, base_bit + bits, bits, page->fill_order), bits);
            }
            else if (page->photometric == 5) {
                const unsigned char *pixel;
                unsigned long c, m, yy, k;
                if (bits == 16) {
                    pixel = row + (size_t)x * 8;
                    c = tiff_get_u16_sample(tiff, pixel);
                    m = tiff_get_u16_sample(tiff, pixel + 2);
                    yy = tiff_get_u16_sample(tiff, pixel + 4);
                    k = tiff_get_u16_sample(tiff, pixel + 6);
                    if (page->max_sample_value != 65535UL) {
                        c = (c * 65535UL + page->max_sample_value / 2UL) /
                            page->max_sample_value;
                        m = (m * 65535UL + page->max_sample_value / 2UL) /
                            page->max_sample_value;
                        yy = (yy * 65535UL + page->max_sample_value / 2UL) /
                             page->max_sample_value;
                        k = (k * 65535UL + page->max_sample_value / 2UL) /
                            page->max_sample_value;
                    }
                    r = (unsigned char)(((65535UL - c) * (65535UL - k)) / 16842495UL);
                    g = (unsigned char)(((65535UL - m) * (65535UL - k)) / 16842495UL);
                    b = (unsigned char)(((65535UL - yy) * (65535UL - k)) / 16842495UL);
                }
                else {
                    pixel = row + (size_t)x * 4;
                    c = pixel[0]; m = pixel[1]; yy = pixel[2]; k = pixel[3];
                    r = (unsigned char)(((255UL - c) * (255UL - k)) / 255UL);
                    g = (unsigned char)(((255UL - m) * (255UL - k)) / 255UL);
                    b = (unsigned char)(((255UL - yy) * (255UL - k)) / 255UL);
                }
            }
            else {
                return 0;
            }

            output_offset = ((size_t)y * (size_t)image->width + (size_t)x) * 4;
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

static int tiff_decode_block(const TIFF_Context *tiff,
                             const TIFF_Page *page,
                             unsigned long offset,
                             unsigned long byte_count,
                             unsigned char *destination,
                             size_t destination_size,
                             unsigned long block_width,
                             unsigned long block_height)
{
    (void)block_width;
    (void)block_height;

    if (!tiff_range_ok(tiff, offset, (size_t)byte_count))
        return 0;

    switch (page->compression) {
    case 1:
        if ((size_t)byte_count != destination_size)
            return 0;
        memcpy(destination, tiff->data + offset, destination_size);
        return 1;

    case 3:
    case 4:
        return tiff_ccitt_decode(
            tiff->data + offset,
            (size_t)byte_count,
            destination,
            destination_size,
            block_width,
            block_height,
            page->compression,
            page->group3_options,
            page->group4_options,
            page->fill_order,
            page->photometric);

    case 5:
        return tiff_lzw_decode(tiff->data + offset, (size_t)byte_count, destination, destination_size);
    case 32773:
        return tiff_packbits_decode(tiff->data + offset, (size_t)byte_count, destination, destination_size);
#ifdef MINITIFF_USE_STB_ZLIB
    case 8:
    case 32946:
        return minitiff_zlib_decode(tiff->data + offset, (size_t)byte_count, destination, destination_size);
#else
    case 8:
    case 32946:
        return 0;
#endif
#ifdef MINITIFF_USE_STB_IMAGE
    case 6:
    case 7:
        return tiff_jpeg_decode(tiff->data + offset, (size_t)byte_count, destination, destination_size,
                                block_width, block_height, page->samples_per_pixel,
                                page->jpeg_tables, page->jpeg_tables_size);
#else
    case 6:
    case 7:
        return 0;
#endif
    default:
        return 0;
    }
}

static int tiff_row_bytes(unsigned long width, unsigned short samples,
                          unsigned short bits, size_t *result)
{
    size_t bits_total;
    if (!tiff_mul_size((size_t)width, (size_t)samples, &bits_total) ||
        !tiff_mul_size(bits_total, (size_t)bits, &bits_total))
        return 0;
    *result = (bits_total + 7) / 8;
    return 1;
}

static int tiff_copy_block_to_image(const TIFF_Page *page,
                                    const unsigned char *block,
                                    unsigned long block_width,
                                    unsigned long block_height,
                                    unsigned long dst_x,
                                    unsigned long dst_y,
                                    unsigned long plane,
                                    unsigned char *raw,
                                    size_t raw_row_bytes)
{
    unsigned long x, y;
    size_t block_row_bytes;
    unsigned short bits;
    bits = page->bits_per_sample[0];
    if (!tiff_row_bytes(block_width, page->planar_config == 2 ? 1 : page->samples_per_pixel,
                        bits, &block_row_bytes))
        return 0;

    for (y = 0; y < block_height && dst_y + y < page->height; ++y) {
        unsigned long copy_width = page->width - dst_x;
        unsigned char *dst_row;
        const unsigned char *src_row;
        if (copy_width > block_width) copy_width = block_width;
        dst_row = raw + (size_t)(dst_y + y) * raw_row_bytes;
        src_row = block + (size_t)y * block_row_bytes;

        if (page->planar_config == 1) {
            size_t bytes;
            if (bits == 8 || bits == 16) {
                bytes = (size_t)copy_width * page->samples_per_pixel * (bits / 8);
                memcpy(dst_row + (size_t)dst_x * page->samples_per_pixel * (bits / 8), src_row, bytes);
            } else {
                /* Packed samples: only whole-byte-aligned blocks can be copied directly. */
                if ((dst_x * page->samples_per_pixel * bits) % 8 != 0 ||
                    (copy_width * page->samples_per_pixel * bits) % 8 != 0)
                    return 0;
                memcpy(dst_row + (dst_x * page->samples_per_pixel * bits) / 8,
                       src_row, (copy_width * page->samples_per_pixel * bits) / 8);
            }
        } else {
            for (x = 0; x < copy_width; ++x) {
                size_t src_off;
                size_t dst_off;
                src_off = (size_t)x * (bits / 8);
                dst_off = ((size_t)(dst_x + x) * page->samples_per_pixel + plane) * (bits / 8);
                memcpy(dst_row + dst_off, src_row + src_off, bits / 8);
            }
        }
    }
    return 1;
}

static MiniTIFF_Image *tiff_decode_page(const TIFF_Context *tiff,
                                        const TIFF_Page *page)
{
    MiniTIFF_Image *image;
    unsigned char *raw;
    unsigned char *block;
    size_t raw_row_size, raw_size, pixel_count, pixel_size, block_size;
    unsigned long y, strip, plane;
    unsigned long output_width, output_height;
    unsigned short bits;

    bits = page->bits_per_sample[0];
    if (!tiff_row_bytes(page->width, page->samples_per_pixel, bits, &raw_row_size))
        return NULL;
    if (!tiff_mul_size(raw_row_size, (size_t)page->height, &raw_size) || raw_size == 0)
        return NULL;
    raw = (unsigned char *)calloc(1, raw_size);
    if (!raw) return NULL;
    block = NULL;
    block_size = 0;

    if (page->tile_count != 0) {
        unsigned long tiles_x;
        unsigned long tiles_y;
        unsigned long tiles_per_plane;
        unsigned long total_planes;
        unsigned long t;
        size_t tile_row_bytes;
        if (page->width > (unsigned long)-1 - page->tile_width + 1 ||
            page->height > (unsigned long)-1 - page->tile_length + 1) {
            free(raw); return NULL;
        }
        tiles_x = (page->width + page->tile_width - 1) / page->tile_width;
        tiles_y = (page->height + page->tile_length - 1) / page->tile_length;
        if (tiles_y != 0 && tiles_x > (unsigned long)-1 / tiles_y) {
            free(raw); return NULL;
        }
        tiles_per_plane = tiles_x * tiles_y;
        total_planes = page->planar_config == 2 ? page->samples_per_pixel : 1;
        if (!tiff_row_bytes(page->tile_width, page->planar_config == 2 ? 1 : page->samples_per_pixel, bits, &tile_row_bytes) ||
            !tiff_mul_size(tile_row_bytes, (size_t)page->tile_length, &block_size)) {
            free(raw); return NULL;
        }
        block = (unsigned char *)malloc(block_size);
        if (!block) { free(raw); return NULL; }
        for (plane = 0; plane < total_planes; ++plane) {
            for (t = 0; t < tiles_per_plane; ++t) {
                unsigned long tx = t % tiles_x;
                unsigned long ty = t / tiles_x;
                unsigned long tile_index = plane * tiles_per_plane + t;
                if (tile_index >= page->tile_count) { free(block); free(raw); return NULL; }
                if (!tiff_decode_block(tiff, page, page->tile_offsets[tile_index], page->tile_byte_counts[tile_index],
                                       block, block_size, page->tile_width, page->tile_length)) {
                    free(block); free(raw); return NULL;
                }
                if (page->predictor == 2) {
                    if (bits == 8) tiff_predictor_horizontal(block, page->tile_width, page->tile_length,
                                                              page->planar_config == 2 ? 1 : page->samples_per_pixel);
                    else if (bits == 16) tiff_predictor_horizontal_16(tiff, block, page->tile_width, page->tile_length,
                                                                        page->planar_config == 2 ? 1 : page->samples_per_pixel);
                    else { free(block); free(raw); return NULL; }
                }
                if (!tiff_copy_block_to_image(page, block, page->tile_width, page->tile_length,
                                              tx * page->tile_width, ty * page->tile_length,
                                              plane, raw, raw_row_size)) {
                    free(block); free(raw); return NULL;
                }
            }
        }
    } else {
        unsigned long total_planes = page->planar_config == 2 ? page->samples_per_pixel : 1;
        unsigned long strips_per_plane;
        size_t plane_row_bytes;
        if (!tiff_row_bytes(page->width, page->planar_config == 2 ? 1 : page->samples_per_pixel,
                            bits, &plane_row_bytes)) { free(raw); return NULL; }
        if (page->strip_count % total_planes != 0) { free(raw); return NULL; }
        strips_per_plane = page->strip_count / total_planes;
        for (plane = 0; plane < total_planes; ++plane) {
            y = 0;
            for (strip = 0; strip < strips_per_plane && y < page->height; ++strip) {
                unsigned long index = plane * strips_per_plane + strip;
                unsigned long rows = page->height - y;
                size_t strip_size;
                if (rows > page->rows_per_strip) rows = page->rows_per_strip;
                if (!tiff_mul_size(plane_row_bytes, (size_t)rows, &strip_size)) { free(raw); return NULL; }
                if (block_size < strip_size) {
                    free(block);
                    block = (unsigned char *)malloc(strip_size);
                    if (!block) { free(raw); return NULL; }
                    block_size = strip_size;
                }
                if (index >= page->strip_count ||
                    !tiff_decode_block(tiff, page, page->strip_offsets[index], page->strip_byte_counts[index],
                                       block, strip_size, page->width, rows)) {
                    free(block); free(raw); return NULL;
                }
                if (page->predictor == 2) {
                    if (bits == 8) tiff_predictor_horizontal(block, page->width, rows,
                                                              page->planar_config == 2 ? 1 : page->samples_per_pixel);
                    else if (bits == 16) tiff_predictor_horizontal_16(tiff, block, page->width, rows,
                                                                        page->planar_config == 2 ? 1 : page->samples_per_pixel);
                    else { free(block); free(raw); return NULL; }
                }
                if (!tiff_copy_block_to_image(page, block, page->width, rows, 0, y, plane, raw, raw_row_size)) {
                    free(block); free(raw); return NULL;
                }
                y += rows;
            }
            if (y != page->height) { free(block); free(raw); return NULL; }
        }
    }
    free(block);

    output_width = page->width;
    output_height = page->height;
    if (page->orientation >= 5 && page->orientation <= 8) {
        output_width = page->height;
        output_height = page->width;
    }
    if (!tiff_mul_size((size_t)output_width, (size_t)output_height, &pixel_count) ||
        !tiff_mul_size(pixel_count, 4, &pixel_size)) { free(raw); return NULL; }
    image = (MiniTIFF_Image *)calloc(1, sizeof(*image));
    if (!image) { free(raw); return NULL; }
    image->width = output_width;
    image->height = output_height;
    image->channels = 4;
    image->bits_per_channel = 8;
    image->orientation = page->orientation;
    image->pixels = (unsigned char *)malloc(pixel_size);
    if (!image->pixels) { free(raw); tiff_free(image); return NULL; }

    {
        MiniTIFF_Image source;
        size_t source_size;
        source.width = page->width;
        source.height = page->height;
        source.channels = 4;
        source.bits_per_channel = 8;
        source.orientation = page->orientation;
        if (!tiff_mul_size((size_t)page->width, (size_t)page->height, &source_size) ||
            !tiff_mul_size(source_size, 4, &source_size)) { free(raw); tiff_free(image); return NULL; }
        source.pixels = (unsigned char *)malloc(source_size);
        if (!source.pixels) { free(raw); tiff_free(image); return NULL; }
        if (!tiff_convert_pixels(tiff, page, raw, &source)) {
            free(source.pixels); free(raw); tiff_free(image); return NULL;
        }
        if (page->orientation == 1) {
            memcpy(image->pixels, source.pixels, pixel_size);
        } else {
            unsigned long sx, sy, dx, dy;
            for (sy = 0; sy < source.height; ++sy) {
                for (sx = 0; sx < source.width; ++sx) {
                    switch (page->orientation) {
                    case 2: dx = source.width - 1 - sx; dy = sy; break;
                    case 3: dx = source.width - 1 - sx; dy = source.height - 1 - sy; break;
                    case 4: dx = sx; dy = source.height - 1 - sy; break;
                    case 5: dx = sy; dy = sx; break;
                    case 6: dx = source.height - 1 - sy; dy = sx; break;
                    case 7: dx = source.height - 1 - sy; dy = source.width - 1 - sx; break;
                    case 8: dx = sy; dy = source.width - 1 - sx; break;
                    default: dx = sx; dy = sy; break;
                    }
                    memcpy(image->pixels + ((size_t)dy * image->width + dx) * 4,
                           source.pixels + ((size_t)sy * source.width + sx) * 4, 4);
                }
            }
        }
        free(source.pixels);
    }
    free(raw);
    return image;
}

/* ------------------------------------------------------------------------- */
/* TIFF context initialization                                               */

static int tiff_init_context(const void *data, size_t size, TIFF_Context *tiff)
{
    memset(tiff, 0, sizeof(*tiff));
    if (!data || size < 8)
        return 0;
    tiff->data = (const unsigned char *)data;
    tiff->size = size;
    if (tiff->data[0] == 'I' && tiff->data[1] == 'I')
        tiff->little_endian = 1;
    else if (tiff->data[0] == 'M' && tiff->data[1] == 'M')
        tiff->little_endian = 0;
    else
        return 0;
    if (tiff_u16(tiff, tiff->data + 2) != 42)
        return 0;
    tiff->first_ifd = tiff_u32(tiff, tiff->data + 4);
    return tiff->first_ifd != 0;
}

static int tiff_count_ifds(const TIFF_Context *tiff)
{
    unsigned long offset;
    int count;
    offset = tiff->first_ifd;
    count = 0;
    while (offset != 0) {
        unsigned short entries;
        size_t entries_size;
        size_t total_size;
        if (!tiff_range_ok(tiff, offset, 2)) return 0;
        entries = tiff_u16(tiff, tiff->data + offset);
        if (!tiff_mul_size((size_t)entries, 12, &entries_size) ||
            !tiff_add_size(entries_size, 6, &total_size) ||
            !tiff_range_ok(tiff, offset, total_size)) return 0;
        if (count == INT_MAX) return 0;
        ++count;
        if ((size_t)count > tiff->size / 6 + 1) return 0;
        offset = tiff_u32(tiff, tiff->data + offset + 2 + entries_size);
    }
    return count;
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
/* Public page count and validation APIs                                     */

int tiff_get_page_count(const void *data, size_t size)
{
    TIFF_Context tiff;
    if (!tiff_init_context(data, size, &tiff))
        return 0;
    return tiff_count_ifds(&tiff);
}

int tiff_get_page_count_file(const char *filename)
{
    FILE *file;
    long file_size_long;
    size_t file_size;
    unsigned char *data;
    int count;
    if (!filename) return 0;
    file = fopen(filename, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return 0; }
    file_size_long = ftell(file);
    if (file_size_long <= 0) { fclose(file); return 0; }
    file_size = (size_t)file_size_long;
    if (fseek(file, 0, SEEK_SET) != 0) { fclose(file); return 0; }
    data = (unsigned char *)malloc(file_size);
    if (!data) { fclose(file); return 0; }
    if (fread(data, 1, file_size, file) != file_size) { free(data); fclose(file); return 0; }
    fclose(file);
    count = tiff_get_page_count(data, file_size);
    free(data);
    return count;
}

int tiff_is_valid(const void *data, size_t size)
{
    return tiff_get_page_count(data, size) > 0;
}

int tiff_is_valid_file(const char *filename)
{
    return tiff_get_page_count_file(filename) > 0;
}

static int tiff_get_ifd_entry(const TIFF_Context *tiff,
                              unsigned long offset,
                              unsigned short wanted_tag,
                              TIFF_Entry *result)
{
    unsigned short count;
    unsigned short i;
    if (!tiff_range_ok(tiff, offset, 2)) return 0;
    count = tiff_u16(tiff, tiff->data + offset);
    if (!tiff_range_ok(tiff, offset, 2 + (size_t)count * 12 + 4)) return 0;
    for (i = 0; i < count; ++i) {
        const unsigned char *p = tiff->data + offset + 2 + (size_t)i * 12;
        result->tag = tiff_u16(tiff, p);
        result->type = tiff_u16(tiff, p + 2);
        result->count = tiff_u32(tiff, p + 4);
        result->value = tiff_u32(tiff, p + 8);
        if (result->tag == wanted_tag) return 1;
    }
    return 0;
}

static int tiff_get_page_entry(const void *data, size_t size,
                               unsigned page_index, unsigned short tag,
                               TIFF_Context *tiff, TIFF_Entry *entry)
{
    unsigned long ifd;
    if (!tiff_init_context(data, size, tiff)) return 0;
    if (!tiff_find_ifd(tiff, page_index, &ifd)) return 0;
    return tiff_get_ifd_entry(tiff, ifd, tag, entry);
}

int tiff_get_tag_u32(const void *data, size_t size,
                     unsigned page_index, unsigned short tag,
                     unsigned long index, unsigned long *value)
{
    TIFF_Context tiff;
    TIFF_Entry entry;
    if (!value) return 0;
    if (!tiff_get_page_entry(data, size, page_index, tag, &tiff, &entry)) return 0;
    return tiff_entry_get_u32(&tiff, &entry, index, value);
}

int tiff_get_tag_data(const void *data, size_t size,
                      unsigned page_index, unsigned short tag,
                      unsigned char *buffer, size_t buffer_size,
                      size_t *tag_size)
{
    TIFF_Context tiff;
    TIFF_Entry entry;
    unsigned char raw[4];
    const unsigned char *ptr;
    size_t total;
    if (!buffer || !tag_size) return 0;
    if (!tiff_get_page_entry(data, size, page_index, tag, &tiff, &entry)) return 0;
    if (!tiff_entry_data(&tiff, &entry, raw, &ptr, &total)) return 0;
    if (buffer_size < total) {
        *tag_size = total;
        return 0;
    }
    memcpy(buffer, ptr, total);
    *tag_size = total;
    return 1;
}

int tiff_get_tag_string(const void *data, size_t size,
                        unsigned page_index, unsigned short tag,
                        char *buffer, size_t buffer_size)
{
    TIFF_Context tiff;
    TIFF_Entry entry;
    unsigned char raw[4];
    const unsigned char *ptr;
    size_t length;
    size_t copy_length;
    if (!buffer || buffer_size == 0) return 0;
    if (!tiff_get_page_entry(data, size, page_index, tag, &tiff, &entry)) return 0;
    if (entry.type != 2) return 0;
    if (!tiff_entry_data(&tiff, &entry, raw, &ptr, &length)) return 0;
    copy_length = length;
    if (copy_length >= buffer_size) copy_length = buffer_size - 1;
    memcpy(buffer, ptr, copy_length);
    buffer[copy_length] = '\0';
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

    memset(&page, 0, sizeof(page));

    if (!tiff_init_context(data, size, &tiff))
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
