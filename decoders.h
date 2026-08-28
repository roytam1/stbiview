/* Internal Decoder: XBM format */

unsigned char* LoadXBM(const char* szPath, int* w, int* h) {
    char line[256];
    int width = 0, height = 0, bytesPerRow;
    unsigned char* pRGB;
    int x = 0, y = 0, b;
    unsigned int val;
    FILE* f = fopen(szPath, "r");
    if (!f) return NULL;

    // 1. Simple parser to find #define name_width and #define name_height
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "_width")) sscanf(line, "%*s %*s %d", &width);
        if (strstr(line, "_height")) sscanf(line, "%*s %*s %d", &height);
        if (strstr(line, "static char")) break;
    }

    if (width <= 0 || height <= 0) { fclose(f); return NULL; }

    // 2. Allocate RGB buffer (Viewer expects 24-bit)
    pRGB = (unsigned char*)malloc(width * height * 3);
    bytesPerRow = (width + 7) / 8;

    // Scan for hex values (e.g., 0xAB)
    while (fscanf(f, " 0x%x ,", &val) == 1) {
        for (b = 0; b < 8 && x < width; b++) {
            // XBM is LSB first
            int idx;
            BYTE color = (val & (1 << b)) ? 0 : 255; // 1 = Black, 0 = White
            idx = (y * width + x) * 3;
            pRGB[idx] = pRGB[idx+1] = pRGB[idx+2] = color;
            x++;
        }
        if (x >= width) { x = 0; y++; }
        if (y >= height) break;
    }

    fclose(f);
    *w = width; *h = height;
    return pRGB;
}

/* Internal Decoder: XPM format */

unsigned char* LoadXPM(const char* szPath, int* w, int* h) {
    typedef struct { char key[4]; BYTE r, g, b; } XPM_COLOR;
    int width, height, ncolors, cpp, i, y, x, k;
    char line[1024];
    unsigned char* pRGB;
    XPM_COLOR* colors;
    FILE* f = fopen(szPath, "r");
    if (!f) return NULL;

    // Skip to the values line (usually the first line in quotes)
    while (fgets(line, sizeof(line), f) && !strchr(line, '"'));

    sscanf(strchr(line, '"') + 1, "%d %d %d %d", &width, &height, &ncolors, &cpp);

    // Simplest approach: A fixed array for the color map (XPM2/3 style)
    // Note: For a 486SX, a simple linear search of 'ncolors' is fast enough
    colors = malloc(ncolors * sizeof(XPM_COLOR));

    for (i = 0; i < ncolors; i++) {
        char *p, *hex;
TryNextColor:
        if(!fgets(line, sizeof(line), f)) break;
        p = strchr(line, '"');
        if(!p) goto TryNextColor;
        ++p;
        strncpy(colors[i].key, p, cpp);
        colors[i].key[cpp] = '\0';

        hex = strrchr(p, '#');
        if (hex) {
            unsigned int r, g, b;
            sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b);
            colors[i].r = (BYTE)r; colors[i].g = (BYTE)g; colors[i].b = (BYTE)b;
        }
    }

    pRGB = (unsigned char*)malloc(width * height * 3);
    for (y = 0; y < height; y++) {
        char* p;
TryNextLine:
        if(!fgets(line, sizeof(line), f)) break;
        p = strchr(line, '"');
        if(!p) goto TryNextLine;
        ++p;
        for (x = 0; x < width; x++) {
            // Find key in color map
            for (k = 0; k < ncolors; k++) {
                if (strncmp(p + (x * cpp), colors[k].key, cpp) == 0) {
                    int idx = (y * width + x) * 3;
                    pRGB[idx]   = colors[k].r;
                    pRGB[idx+1] = colors[k].g;
                    pRGB[idx+2] = colors[k].b;
                    break;
                }
            }
        }
    }

    free(colors);
    fclose(f);
    *w = width; *h = height;
    return pRGB;
}

/* Internal Decoder: QOI format */

unsigned char* LoadQOI(const char* szPath, int* w, int* h) {
    unsigned int magic;
    unsigned int width, height, i;
    unsigned char channels, colorspace;
    unsigned char head[10];
    unsigned char index[64][4];
    unsigned char* pRGB;
    unsigned char r = 0, g = 0, b = 0, a = 255;
    int px_len, run = 0;
    FILE* f = fopen(szPath, "rb");
    if (!f) return NULL;

    // Read Header (14 bytes)
    fread(&magic, 4, 1, f);
    if (magic != 0x66696f71) { fclose(f); return NULL; } // "qoif" in little-endian

    // Swap endianness for Win32 (QOI is Big Endian)
    fread(head, 1, 10, f);
    width = (head[0] << 24) | (head[1] << 16) | (head[2] << 8) | head[3];
    height = (head[4] << 24) | (head[5] << 16) | (head[6] << 8) | head[7];
    channels = head[8]; 

    pRGB = (unsigned char*)malloc(width * height * 3);
    memset(index, 0, 64 * 4);

    px_len = width * height * 3;

    for (i = 0; i < px_len; i += 3) {
        if (run > 0) {
            run--;
        } else {
            int index_pos, b1 = fgetc(f);
            if (b1 == EOF) break;

            if (b1 == 0xff) { // QOI_OP_RGBA
                r = fgetc(f); g = fgetc(f); b = fgetc(f); a = fgetc(f);
            } else if (b1 == 0xfe) { // QOI_OP_RGB
                r = fgetc(f); g = fgetc(f); b = fgetc(f);
            } else if ((b1 & 0xc0) == 0x00) { // QOI_OP_INDEX
                int idx = b1 & 0x3f;
                r = index[idx][0]; g = index[idx][1]; b = index[idx][2]; a = index[idx][3];
            } else if ((b1 & 0xc0) == 0x40) { // QOI_OP_DIFF
                r += ((b1 >> 4) & 0x03) - 2;
                g += ((b1 >> 2) & 0x03) - 2;
                b += ( b1       & 0x03) - 2;
            } else if ((b1 & 0xc0) == 0x80) { // QOI_OP_LUMA
                int b2 = fgetc(f);
                int vg = (b1 & 0x3f) - 32;
                r += vg - 8 + ((b2 >> 4) & 0x0f);
                g += vg;
                b += vg - 8 + ( b2       & 0x0f);
            } else if ((b1 & 0xc0) == 0xc0) { // QOI_OP_RUN
                run = b1 & 0x3f;
            }

            index_pos = ((r << 1) + r + (g << 2) + g + (b << 3) - b + (a << 3) + a + a + a) & 63;
            index[index_pos][0] = r; index[index_pos][1] = g;
            index[index_pos][2] = b; index[index_pos][3] = a;
        }

        pRGB[i] = r; pRGB[i+1] = g; pRGB[i+2] = b;
    }

    fclose(f);
    *w = (int)width; *h = (int)height;
    return pRGB;
}

/* Internal Decoder: MSP format */

typedef struct {
    WORD Key1;        // Magic number: 0x6144 ("aD") or 0x694c ("iL") depending on version
    WORD Key2;        // Magic number: 0x4d6e ("Mn") or 0x536e ("Sn")
    WORD Width;       // Image width in pixels
    WORD Height;      // Image height in pixels
    WORD XAspectRatio;// Display aspect ratio (important for 1980s CGA/EGA screens)
    WORD YAspectRatio;
    WORD XPrinterRes; // Intended printer resolution
    WORD YPrinterRes;
    WORD PrinterWidth;  /* Width of the printer in pixels   */
    WORD PrinterHeight; /* Height of the printer in pixels   */
    WORD XAspectCorr;   /* X aspect correction (unused)     */
    WORD YAspectCorr;   /* Y aspect correction (unused)     */
    WORD Checksum;      /* Checksum of previous 24 bytes   */
    WORD Padding[3];    /* Unused padding    */
} MSP_HEADER;

unsigned char* LoadMSP(const char* szPath, int* w, int* h) {
    MSP_HEADER header;
    int width, height, bytesPerLine, x, y, i;
    BOOL isVersion2 = FALSE;
    unsigned char* pRGB;
    FILE* f = fopen(szPath, "rb");
    if (!f) return NULL;

    if (fread(&header, 1, 32, f) != 32) { fclose(f); return NULL; }

    // Validate Windows Paint MSP Magic numbers ("Dan McCabe", the original author's name)
    // v1: 0x6144 0x4d6e ("aD", "Mn" due to little-endian)
    // v2: 0x694c 0x536e ("iL", "Sn")
    if (header.Key1 == 0x6144 && header.Key2 == 0x4d6e) {
        isVersion2 = FALSE;
    } else if (header.Key1 == 0x694c && header.Key2 == 0x536e) {
        isVersion2 = TRUE;
    } else {
        // Not a valid Paint MSP file
        fclose(f);
        return NULL;
    }

    width = header.Width;
    height = header.Height;

    // Allocate 24-bit RGB target buffer for the viewer
    pRGB = (unsigned char*)malloc(width * height * 3);
    if (!pRGB) { fclose(f); return NULL; }

    bytesPerLine = (width + 7) / 8;

    if (!isVersion2) {
        // --- VERSION 1 DECODER: Uncompressed Raw Bits ---
        unsigned char* lineBuf = (unsigned char*)malloc(bytesPerLine);

        for (y = 0; y < height; y++) {
            fread(lineBuf, 1, bytesPerLine, f);
            for (x = 0; x < width; x++) {
                // MSP reads bits from Most Significant Bit (MSB) to LSB
                int byteIdx = x / 8;
                int bitIdx = 7 - (x % 8);
                BYTE val = (lineBuf[byteIdx] & (1 << bitIdx)) ? 255 : 0; // 0 = Black, 1 = White

                int rgbIdx = (y * width + x) * 3;
                pRGB[rgbIdx] = pRGB[rgbIdx+1] = pRGB[rgbIdx+2] = val;
            }
        }
        free(lineBuf);
    }
    else {
        // --- VERSION 2 DECODER: PackBits-style RLE ---
        // Skip the Scan-line map (height * 2 bytes) - used for random access, we don't need it
        unsigned char* lineBuf;
        fseek(f, height * 2, SEEK_CUR);

        lineBuf = (unsigned char*)malloc(bytesPerLine);

        for (y = 0; y < height; y++) {
            int outBytes = 0;
            memset(lineBuf, 255, bytesPerLine);
            // Unpack one row
            while (outBytes < bytesPerLine) {
                int control = fgetc(f);
                if (control == EOF) break;

                if (control == 0) {
                    // Single-byte RLE run
                    int count = fgetc(f);
                    int pattern = fgetc(f);
                    for (i = 0; i < count && outBytes < bytesPerLine; i++) {
                        lineBuf[outBytes++] = (unsigned char)pattern;
                    }
                }
                else {
                    int count = control;
                    for (i = 0; i < count && outBytes < bytesPerLine; i++) {
                        lineBuf[outBytes++] = fgetc(f);
                    }
                }
            }

            // Convert unpacked line buffer bits to our 24-bit output image
            for (x = 0; x < width; x++) {
                int byteIdx = x / 8;
                int bitIdx = 7 - (x % 8);
                BYTE val = (lineBuf[byteIdx] & (1 << bitIdx)) ? 255 : 0;

                int rgbIdx = (y * width + x) * 3;
                pRGB[rgbIdx] = pRGB[rgbIdx+1] = pRGB[rgbIdx+2] = val;
            }
        }
        free(lineBuf);
    }

    fclose(f);
    *w = width;
    *h = height;
    return pRGB;
}

/* Internal Decoder: MAG format */

#define MAG_LE16(p) ((unsigned int)(p)[0] | ((unsigned int)(p)[1] << 8))
#define MAG_LE32(p) ((unsigned long)(p)[0] | ((unsigned long)(p)[1] << 8) | \
                     ((unsigned long)(p)[2] << 16) | ((unsigned long)(p)[3] << 24))

typedef unsigned short mag_pixel;

static const int mag_dx[16] = {
    0, 1, 2, 4, 0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0
};
static const int mag_dy[16] = {
    0, 0, 0, 0, 1, 1, 2, 2, 2, 4, 4, 4, 8, 8, 8, 16
};

unsigned char* LoadMAG(const char *filePath, int *w, int *h) {
    FILE *fp;
    unsigned char id[8];
    unsigned char hdr[32];
    int m_256, raw_x1, raw_y1, raw_x2, raw_y2;
    int left_pad, right_pad, out_w, out_h, p_width, p_height;
    long h_off;
    unsigned long a_off, a_size, b_off, b_size, p_off, p_size;
    unsigned char *a_data = NULL, *b_data = NULL, *p_data = NULL;
    unsigned char palette[256][3];
    unsigned char *flag = NULL;
    mag_pixel *pixel0 = NULL;
    unsigned char *pic = NULL;
    unsigned char *rgb = NULL;
    int i, py, fi, px, ai, bi, pi;
    unsigned char mask;

    if (!filePath || !w || !h) return NULL;
    fp = fopen(filePath, "rb");
    if (!fp) return NULL;

    if (fread(id, 1, 8, fp) != 8 || memcmp(id, "MAKI02", 6) != 0) {
        fclose(fp);
        return NULL;
    }

    { int ch; while ((ch = fgetc(fp)) != 0x1A && ch != EOF); }
    if (feof(fp)) { fclose(fp); return NULL; }
    h_off = ftell(fp);

    if (fread(hdr, 1, 32, fp) != 32) { fclose(fp); return NULL; }

    m_256  = hdr[3] & 0x80;
    raw_x1 = MAG_LE16(hdr + 4);
    raw_y1 = MAG_LE16(hdr + 6);
    raw_x2 = MAG_LE16(hdr + 8);
    raw_y2 = MAG_LE16(hdr + 10);

    a_off  = MAG_LE32(hdr + 12);
    b_off  = MAG_LE32(hdr + 16);
    b_size = MAG_LE32(hdr + 20);
    p_off  = MAG_LE32(hdr + 24);
    p_size = MAG_LE32(hdr + 28);
    a_size = b_off - a_off;

    out_w = raw_x2 - raw_x1 + 1;
    out_h = raw_y2 - raw_y1 + 1;
    if (out_w <= 0 || out_h <= 0) { fclose(fp); return NULL; }

    left_pad  = raw_x1 & 7;
    right_pad = 7 - (raw_x2 & 7);
    p_width   = (out_w + left_pad + right_pad) / (m_256 ? 2 : 4);
    p_height  = out_h;

    memset(palette, 0, sizeof(palette));
    {
        int nc = m_256 ? 256 : 16;
        fseek(fp, h_off + 32, SEEK_SET);
        for (i = 0; i < nc; i++) {
            unsigned char g = (unsigned char)fgetc(fp);
            unsigned char r = (unsigned char)fgetc(fp);
            unsigned char b = (unsigned char)fgetc(fp);
            palette[i][0] = r;
            palette[i][1] = g;
            palette[i][2] = b;
        }
    }

    a_data = (unsigned char*)malloc(a_size > 0 ? a_size : 1);
    if (!a_data) goto fail;
    if (a_size > 0) {
        fseek(fp, h_off + (long)a_off, SEEK_SET);
        if (fread(a_data, 1, a_size, fp) != a_size) goto fail;
    }

    b_data = (unsigned char*)malloc(b_size > 0 ? b_size : 1);
    if (!b_data) goto fail;
    if (b_size > 0) {
        fseek(fp, h_off + (long)b_off, SEEK_SET);
        if (fread(b_data, 1, b_size, fp) != b_size) goto fail;
    }

    p_data = (unsigned char*)malloc(p_size > 0 ? p_size : 1);
    if (!p_data) goto fail;
    if (p_size > 0) {
        fseek(fp, h_off + (long)p_off, SEEK_SET);
        if (fread(p_data, 1, p_size, fp) != p_size) goto fail;
    }
    fclose(fp);
    fp = NULL;

    flag    = (unsigned char*)calloc(p_width / 2, 1);
    pixel0  = (mag_pixel*)calloc((size_t)p_width * 17, sizeof(mag_pixel));
    pic     = (unsigned char*)calloc((size_t)out_w * out_h, 1);
    rgb     = (unsigned char*)malloc((size_t)out_w * out_h * 3);
    if (!flag || !pixel0 || !pic || !rgb) goto fail;

    ai = bi = pi = 0;
    mask = 0x80;

    for (py = 0; py < p_height; py++) {
        for (fi = 0; fi < p_width / 2; fi++) {
            int bit = (ai < a_size) ? (a_data[ai] & mask) : 0;
            if (py == 0) {
                flag[fi] = (bit && bi < b_size) ? b_data[bi++] : 0;
            } else {
                if (bit && bi < b_size) flag[fi] ^= b_data[bi++];
            }
            if ((mask >>= 1) == 0) { mask = 0x80; ai++; }
        }

        px = 0;
        for (fi = 0; fi < p_width / 2; fi++) {
            int f, sx, sy;

            f = (flag[fi] >> 4) & 0x0F;
            if (f == 0) {
                if (pi + 1 < (int)p_size)
                    pixel0[(py % 17) * p_width + px] =
                        (mag_pixel)p_data[pi] | ((mag_pixel)p_data[pi + 1] << 8);
                pi += 2;
            } else {
                sx = px - mag_dx[f]; sy = py - mag_dy[f];
                pixel0[(py % 17) * p_width + px] =
                    (sx >= 0 && sy >= 0 && sx < p_width)
                    ? pixel0[(sy % 17) * p_width + sx] : 0;
            }
            px++;

            f = flag[fi] & 0x0F;
            if (f == 0) {
                if (pi + 1 < (int)p_size)
                    pixel0[(py % 17) * p_width + px] =
                        (mag_pixel)p_data[pi] | ((mag_pixel)p_data[pi + 1] << 8);
                pi += 2;
            } else {
                sx = px - mag_dx[f]; sy = py - mag_dy[f];
                pixel0[(py % 17) * p_width + px] =
                    (sx >= 0 && sy >= 0 && sx < p_width)
                    ? pixel0[(sy % 17) * p_width + sx] : 0;
            }
            px++;
        }

        {
            int ox = -left_pad;
            for (px = 0; px < p_width; px++) {
                mag_pixel p = pixel0[(py % 17) * p_width + px];
                if (m_256) {
                    if ((unsigned)ox < (unsigned)out_w)
                        pic[py * out_w + ox] = (unsigned char)(p & 0xFF);
                    ox++;
                    if ((unsigned)ox < (unsigned)out_w)
                        pic[py * out_w + ox] = (unsigned char)((p >> 8) & 0xFF);
                    ox++;
                } else {
                    if ((unsigned)ox < (unsigned)out_w)
                        pic[py * out_w + ox] = (unsigned char)((p >> 4) & 0x0F);
                    ox++;
                    if ((unsigned)ox < (unsigned)out_w)
                        pic[py * out_w + ox] = (unsigned char)(p & 0x0F);
                    ox++;
                    if ((unsigned)ox < (unsigned)out_w)
                        pic[py * out_w + ox] = (unsigned char)((p >> 12) & 0x0F);
                    ox++;
                    if ((unsigned)ox < (unsigned)out_w)
                        pic[py * out_w + ox] = (unsigned char)((p >> 8) & 0x0F);
                    ox++;
                }
            }
        }
    }

    for (i = 0; i < out_w * out_h; i++) {
        unsigned char idx = pic[i];
        rgb[i * 3 + 0] = palette[idx][0];
        rgb[i * 3 + 1] = palette[idx][1];
        rgb[i * 3 + 2] = palette[idx][2];
    }

    free(a_data); free(b_data); free(p_data);
    free(flag); free(pixel0); free(pic);
    *w = out_w;
    *h = out_h;
    return rgb;

fail:
    if (fp) fclose(fp);
    free(a_data); free(b_data); free(p_data);
    free(flag); free(pixel0); free(pic); free(rgb);
    return NULL;
}
