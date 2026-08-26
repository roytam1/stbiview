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
