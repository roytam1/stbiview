#define MAINWIN_CLASS "NT4_STB_Viewer"
#define MAINWIN_TITLE "Simple Image Viewer"
#define MAINWIN_TITLE_SUFFIX "Press 'O' to open file"

#include <windows.h>
#include <commdlg.h>

#include "resource.h"
#include "deffix.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"

#define SIMPLEWEBP_IMPLEMENTATION
#include "simplewebp.h"

#define DR_PCX_IMPLEMENTATION
#include "dr_pcx.h"

/* MyVerInfo from GreenPad */
/* MYVERINFO defination */
#pragma pack(push,1)
typedef struct _MYVERINFO {
	union {
		DWORD dwVersionAndBuild;
		struct {
			WORD wBuild;
			union {
				WORD wVer;
				struct {
					BYTE cMinor;
					BYTE cMajor;
				} u;
			} ver;
		} vb;
	} verbuild;
	WORD wPlatform;
	WORD wType;
} MYVERINFO;

typedef struct _MYVERINFO_PV {
	WORD wBuild;
	union {
		DWORD dwPlatformAndVersion;
		struct {
			WORD wVer;
			WORD wPlatform;
		} pv;
	} platver;
	WORD wType;
} MYVERINFO_PV;
#pragma pack(pop)

#define MVI_KERNELEX	0x8000

#define MVI_MAJOR	verbuild.vb.ver.u.cMajor
#define MVI_MINOR	verbuild.vb.ver.u.cMinor
#define MVI_VER		verbuild.vb.ver.wVer
#define MVI_BUILD	verbuild.vb.wBuild
#define MVI_VBN		verbuild.dwVersionAndBuild

#define MPV_PV		platver.dwPlatformAndVersion

// generate WORD of MMmm (for example Win7(NT 6.1) = 0601)
#define MKVER(M, m) ( (WORD)( (BYTE)(M)<<8 | (BYTE)(m) ) )
// generate DWORD of MMmmbbbb (for example Win7(NT 6.1.7601) = 06011db1)
#define MKVBN(M, m, b) ( (DWORD)( (BYTE)(M)<<24 | (BYTE)(m)<<16 | (WORD)(b) ) )
// generate DWORD of PPPPMMmm (for example Win7(NT 6.1.7601) = 00020601)
#define MKPV(P, M, m) ( (DWORD)( (WORD)(P)<<16 | (BYTE)(M)<<8 | (BYTE)(m) ) )

/* MYVERINFO declaration */
MYVERINFO		g_mvi;
BOOL		g_mvi_init = 0;

/* MYVERINFO (helper) functions */
BOOL GetNtVersionNumbers(DWORD* dwMajorVer, DWORD* dwMinorVer,DWORD* dwBuildNumber)
{
	// call NTDLL.RtlGetNtVersionNumbers for real version numbers
	// it is firstly existed as `RtlGetNtVersionInfo` in XP Beta Build 2495 and quickly renamed to retail name in Build 2517
	BOOL bRet = FALSE;
	HMODULE hModNtdll = NULL;
	if (hModNtdll = LoadLibrary("ntdll.dll"))
	{
		typedef void (WINAPI *pfRTLGETNTVERSIONNUMBERS)(DWORD*, DWORD*, DWORD*);
		pfRTLGETNTVERSIONNUMBERS pfRtlGetNtVersionNumbers;
		pfRtlGetNtVersionNumbers = (pfRTLGETNTVERSIONNUMBERS)GetProcAddress(hModNtdll, "RtlGetNtVersionNumbers");

		if (pfRtlGetNtVersionNumbers)
		{
			pfRtlGetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber);
			*dwBuildNumber &= 0x0ffff;
			bRet = TRUE;
		}

		FreeLibrary(hModNtdll);
		hModNtdll = NULL;
	}
	return bRet;
}

void init_mvi()
{
	OSVERSIONINFOA osvi;

	if(g_mvi_init) return;
	// zero-filling structs for safety
	memset(&g_mvi, 0, sizeof(MYVERINFO));
	memset(&osvi, 0, sizeof(OSVERSIONINFOA));

	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);

	if(GetNtVersionNumbers(&osvi.dwMajorVersion, &osvi.dwMinorVersion,&osvi.dwBuildNumber)) {
		g_mvi.MVI_MINOR = (BYTE)osvi.dwMinorVersion;
		g_mvi.MVI_MAJOR = (BYTE)osvi.dwMajorVersion;
		g_mvi.MVI_BUILD = (WORD)osvi.dwBuildNumber;
		g_mvi.wPlatform = VER_PLATFORM_WIN32_NT;
		g_mvi.wType = 2;
	} else {
		typedef BOOL (WINAPI *PGVEXA)(OSVERSIONINFOA*);
		PGVEXA pGVEXA;
		pGVEXA = (PGVEXA) GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "GetVersionExA");
		if(pGVEXA) {
			pGVEXA(&osvi);

			g_mvi.MVI_MINOR = (BYTE)osvi.dwMinorVersion;
			g_mvi.MVI_MAJOR = (BYTE)osvi.dwMajorVersion;
			g_mvi.MVI_BUILD = (WORD)osvi.dwBuildNumber;
			g_mvi.wPlatform = (WORD)osvi.dwPlatformId;
			g_mvi.wType = 1;
		} else {
			osvi.dwBuildNumber = GetVersion();

			g_mvi.MVI_MAJOR = (LOBYTE(LOWORD(osvi.dwBuildNumber)));
			g_mvi.MVI_MINOR = (HIBYTE(LOWORD(osvi.dwBuildNumber)));

			if (osvi.dwBuildNumber < 0x80000000) {
				g_mvi.MVI_BUILD = (HIWORD(osvi.dwBuildNumber));
				g_mvi.wPlatform = VER_PLATFORM_WIN32_NT;
			}

			if (g_mvi.wPlatform != VER_PLATFORM_WIN32_NT) {
				if (g_mvi.MVI_MAJOR == 3) g_mvi.wPlatform = VER_PLATFORM_WIN32s;
				else g_mvi.wPlatform = VER_PLATFORM_WIN32_WINDOWS;

				//g_mvi.MVI_BUILD = (DWORD)(HIWORD(osvi.dwBuildNumber & 0x7FFFFFFF)); // when dwPlatformId == VER_PLATFORM_WIN32_WINDOWS, HIWORD(dwVersion) is reserved
			}

		}
	}
	// check system32 if verion is > 3.10
	if(g_mvi.wPlatform == VER_PLATFORM_WIN32_NT && g_mvi.MVI_BUILD > MKVER(3, 10)) {
		char cSysDir[256];
		int iSysDirLen = 0;
		iSysDirLen = GetSystemDirectoryA(cSysDir, 256);
		if(iSysDirLen && cSysDir[iSysDirLen-1] != '2') {
			g_mvi.wType |= MVI_KERNELEX;
		}
	}
	g_mvi_init = 1;
}

BOOL mvi_isNT()
{
	if(!g_mvi_init) init_mvi();
	return g_mvi.wPlatform==VER_PLATFORM_WIN32_NT;
}

WORD mvi_getOSBuild()
{
	if(!g_mvi_init) init_mvi();
	return g_mvi.MVI_BUILD;
}

BOOL mvi_isBuildGreater(DWORD dwTarget)
{
	if(!g_mvi_init) init_mvi();
	return g_mvi.MVI_VBN >= dwTarget;
}

BOOL mvi_isWin32s()
{
	if(!g_mvi_init) init_mvi();
	return g_mvi.wPlatform==VER_PLATFORM_WIN32s;
}
/* end of MyVerInfo */

/* [G|S]etScrollInfo wrappers from GreenPad */

// Win32s scroll range is mimited to 32767
#define MAX_SCROLL_WIN32S 32500
#define MAX_MULT_WIN32S (32768-MAX_SCROLL)
// Windows NT: scrollrange is limited to 65535
#define MAX_SCROLL_NT 65400
#define MAX_MULT_NT (65536-MAX_SCROLL)

/* [G|S]etScrollInfo wrappers globals */
int MAX_SCROLL = 0, MAX_MULT = 0;
int V_PAGE = 0, H_PAGE = 0;

typedef int (WINAPI *SSCRINF)(HWND, int, LPSCROLLINFO, BOOL);
int WINAPI MySetScrollInfo_fallback(HWND hwnd, int nBar, LPSCROLLINFO lpsi, BOOL redraw)
{
	// Smart Fallback...
	// We must use SetScrollRange but it is mimited to 65535.
	// So we can avoid oveflow by dividing range and position values
	// In GreenPad we can assume that only nMax can go beyond range.
	int MULT=1;
	int nMax = lpsi->nMax;
	// If we go beyond 65400 then we use a divider
	if (nMax > MAX_SCROLL)
	{   // 65501 - 65535 = 35 values MULT from 2-136
		// Like this the new range is around 8912896 instead of 65535
		MULT = min( (nMax / MAX_SCROLL) + 1,  MAX_MULT );
		// We store the divider in the last 135 values
		// of the max scroll range.
		nMax = MAX_SCROLL + MULT - 1 ; // 65401 => MULT = 2
	}
	if(nBar == SB_HORZ) H_PAGE = lpsi->nPage;
	else /* nBar == SB_VERT */ V_PAGE = lpsi->nPage;

	if (lpsi->fMask|SIF_RANGE)
		SetScrollRange( hwnd, nBar, lpsi->nMin, nMax, FALSE );

	return SetScrollPos( hwnd, nBar, lpsi->nPos/MULT, redraw );
}
int WINAPI MySetScrollInfo_init(HWND hwnd, int nBar, LPSCROLLINFO lpsi, BOOL redraw);
static SSCRINF MySetScrollInfo = MySetScrollInfo_init;

int WINAPI MySetScrollInfo_init(HWND hwnd, int nBar, LPSCROLLINFO lpsi, BOOL redraw)
{
	if( MySetScrollInfo == MySetScrollInfo_init ) {
		if(!MAX_SCROLL) {
			if(mvi_isWin32s()) {
				MAX_SCROLL = MAX_SCROLL_WIN32S;
				MAX_MULT = MAX_MULT_WIN32S;
			} else {
				MAX_SCROLL = MAX_SCROLL_NT;
				MAX_MULT = MAX_MULT_NT;
			}
		}
		MySetScrollInfo = (SSCRINF)GetProcAddress(GetModuleHandleA("USER32.DLL"), "SetScrollInfo");

		// Should be supported since Windows NT 3.51...
		if( !(MySetScrollInfo && ((!mvi_isNT() && mvi_getOSBuild()>=275) || (mvi_isNT() && mvi_isBuildGreater(MKVBN(3,51,944))))) ) {
			MySetScrollInfo = MySetScrollInfo_fallback;
		}
	}

	return MySetScrollInfo( hwnd, nBar, lpsi, redraw );
}

typedef int (WINAPI *GSCRINF)(HWND, int, LPSCROLLINFO);
int WINAPI MyGetScrollInfo_fallback(HWND hwnd, int nBar, LPSCROLLINFO lpsi)
{
	// Smart Fallback...
	if( lpsi->fMask|SIF_RANGE )
		GetScrollRange( hwnd, nBar, &lpsi->nMin, &lpsi->nMax);

	lpsi->nPos = GetScrollPos( hwnd, nBar );
	// Scroll range indicates a multiplier was used...
	if( lpsi->nMax > MAX_SCROLL )
	{ // Apply multipler
		int MULT = lpsi->nMax - MAX_SCROLL + 1; // 65401 => MULT = 2
		lpsi->nMax      *= MULT;
		lpsi->nPos      *= MULT;
		lpsi->nTrackPos *= MULT; // This has to be set by the user.
	}
	if(nBar == SB_HORZ) lpsi->nPage = H_PAGE;
	else /* nBar == SB_VERT */ lpsi->nPage = V_PAGE;
	return 1; // sucess!
}

int WINAPI MyGetScrollInfo_init(HWND hwnd, int nBar, LPSCROLLINFO lpsi);
static GSCRINF MyGetScrollInfo = MyGetScrollInfo_init;

int WINAPI MyGetScrollInfo_init(HWND hwnd, int nBar, LPSCROLLINFO lpsi)
{
	if( MyGetScrollInfo == MyGetScrollInfo_init ) {
		if(!MAX_SCROLL) {
			if(mvi_isWin32s()) {
				MAX_SCROLL = MAX_SCROLL_WIN32S;
				MAX_MULT = MAX_MULT_WIN32S;
			} else {
				MAX_SCROLL = MAX_SCROLL_NT;
				MAX_MULT = MAX_MULT_NT;
			}
		}

		MyGetScrollInfo = (GSCRINF)GetProcAddress(GetModuleHandleA("USER32.DLL"), "GetScrollInfo");

		// Should be supported since Windows NT 3.51...
		if( !(MyGetScrollInfo && ((!mvi_isNT() && mvi_getOSBuild()>=275) || (mvi_isNT() && mvi_isBuildGreater(MKVBN(3,51,944))))) ) {
			MyGetScrollInfo = MyGetScrollInfo_fallback;
		}
	}

	return MyGetScrollInfo( hwnd, nBar, lpsi );
}
/* end of [G|S]etScrollInfo */

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void LoadImageFromPath(HWND hwnd, char* filePath);
void OpenPicFile(HWND hwnd);

/* Global State */
HPALETTE hPalette = NULL; // New: Palette handle
unsigned char* pRawData = NULL; // Raw RGB pixel data
char szFile[260] = {0};
int imgWidth = 0, imgHeight = 0;
int scrollX = 0, scrollY = 0;
int FSdither = 1;
BITMAPINFO bmi;
int LUT_inited = 0;

typedef struct { BYTE r, g, b; } RGB_TRIPLE;
typedef unsigned char LUT_TYPE[32][32];

// Standard 16-color VGA Palette
RGB_TRIPLE vga16[16] = {
    {0,0,0}, {128,0,0}, {0,128,0}, {128,128,0}, {0,0,128}, {128,0,128}, {0,128,128}, {192,192,192},
    {128,128,128}, {255,0,0}, {0,255,0}, {255,255,0}, {0,0,255}, {255,0,255}, {0,255,255}, {255,255,255}
};
RGB_TRIPLE web216[216]; // The "6x6x6" color cube
unsigned char lut16[32][32][32];
unsigned char lut256[32][32][32];

HPALETTE CreateWebSafePalette() {
    int palSize, r, g, b, i = 0;
    LOGPALETTE* plp;
    HPALETTE hPal;

    // 216 colors for the 6x6x6 cube + 20 system colors = 236
    // We'll just create the 216 logical entries
    palSize = sizeof(LOGPALETTE) + (216 * sizeof(PALETTEENTRY));
    plp = (LOGPALETTE*)malloc(palSize);
    
    plp->palVersion = 0x300; // Windows 3.0+
    plp->palNumEntries = 216;
    for (r = 0; r < 6; r++)
        for (g = 0; g < 6; g++)
            for (b = 0; b < 6; b++) {
                plp->palPalEntry[i].peRed   = r * 51;
                plp->palPalEntry[i].peGreen = g * 51;
                plp->palPalEntry[i].peBlue  = b * 51;
                plp->palPalEntry[i].peFlags = 0;
                web216[i].r = r * 51;
                web216[i].g = g * 51;
                web216[i].b = b * 51;
                i++;
            }

    hPal = CreatePalette(plp);
    free(plp);
    return hPal;
}

// Helper: Find closest color in a palette
int FindClosestColor(RGB_TRIPLE color, RGB_TRIPLE* palette, int count) {
    int i, closest = 0;
    long minDistance = 2000000; // Large value
    for (i = 0; i < count; i++) {
        long dr, dg, db, distance;
        dr = color.r - palette[i].r;
        dg = color.g - palette[i].g;
        db = color.b - palette[i].b;
        distance = dr*dr + dg*dg + db*db;
        if (distance < minDistance) {
            minDistance = distance;
            closest = i;
        }
    }
    return closest;
}

// Run this ONCE at app startup
void InitAllLUTs() {
    // Precompute for VGA 16 and web-safe 216
    int r, g, b;
    if(LUT_inited) return;
    for (r = 0; r < 32; r++) {
        for (g = 0; g < 32; g++) {
            for (b = 0; b < 32; b++) {
                RGB_TRIPLE color = { (BYTE)(r << 3), (BYTE)(g << 3), (BYTE)(b << 3) };
                lut16[r][g][b] = (unsigned char)FindClosestColor(color, vga16, 16);
                lut256[r][g][b] = (unsigned char)FindClosestColor(color, web216, 216);
            }
        }
    }
    LUT_inited = 1;
}

// Floyd-Steinberg Dither Function
void ApplyDithering(unsigned char* pixels, int width, int height, int colorCount) {
    // Error buffer stores RGB errors shifted by 4 bits for precision.
    // Signed int is necessary to handle negative error values.
    int i, palCount, x, y;
    LUT_TYPE *activeLUT;
    RGB_TRIPLE *activePal;
    int* errorBuf = (int*)malloc(width * height * 3 * sizeof(int));
    if (!errorBuf) return;

    // Load initial pixels into error buffer (Scale up by 16)
    for (i = 0; i < width * height * 3; i++) {
        errorBuf[i] = (int)pixels[i] << 4;
    }

    activeLUT = (colorCount == 16) ? lut16 : lut256;
    activePal = (colorCount == 16) ? vga16 : web216;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int pIdx, idx, r, g, b;
            int errR, errG, errB;
            BYTE clpR, clpG, clpB;
            RGB_TRIPLE newP;

            idx = (y * width + x) * 3;

            // 1. Clamping and Quantization
            // Shift back down to 0-255 range for LUT lookup
            r = errorBuf[idx] >> 4;
            g = errorBuf[idx+1] >> 4;
            b = errorBuf[idx+2] >> 4;

            clpR = (r < 0) ? 0 : (r > 255) ? 255 : (BYTE)r;
            clpG = (g < 0) ? 0 : (g > 255) ? 255 : (BYTE)g;
            clpB = (b < 0) ? 0 : (b > 255) ? 255 : (BYTE)b;

            // 2. Fast LUT Lookup (No distance math!)
            pIdx = activeLUT[clpR >> 3][clpG >> 3][clpB >> 3];
            newP = activePal[pIdx];

            // 3. Store Final Pixel
            pixels[idx]   = newP.r;
            pixels[idx+1] = newP.g;
            pixels[idx+2] = newP.b;

            // 4. Calculate Error (Residual)
            // Error is in the "Shifted" domain
            errR = errorBuf[idx]   - (newP.r << 4);
            errG = errorBuf[idx+1] - (newP.g << 4);
            errB = errorBuf[idx+2] - (newP.b << 4);

            // 5. Error Diffusion (Floyd-Steinberg Weights)
            // We use integer weights: 7/16, 3/16, 5/16, 1/16

            // Pixel to the Right (7/16)
            if (x + 1 < width) {
                errorBuf[idx + 3] += (errR * 7) >> 4;
                errorBuf[idx + 4] += (errG * 7) >> 4;
                errorBuf[idx + 5] += (errB * 7) >> 4;
            }

            if (y + 1 < height) {
                int rowNext = idx + (width * 3);

                // Down-Left (3/16)
                if (x > 0) {
                    errorBuf[rowNext - 3] += (errR * 3) >> 4;
                    errorBuf[rowNext - 2] += (errG * 3) >> 4;
                    errorBuf[rowNext - 1] += (errB * 3) >> 4;
                }
                // Down (5/16)
                errorBuf[rowNext]     += (errR * 5) >> 4;
                errorBuf[rowNext + 1] += (errG * 5) >> 4;
                errorBuf[rowNext + 2] += (errB * 5) >> 4;

                // Down-Right (1/16)
                if (x + 1 < width) {
                    errorBuf[rowNext + 3] += errR >> 4;
                    errorBuf[rowNext + 4] += errG >> 4;
                    errorBuf[rowNext + 5] += errB >> 4;
                }
            }
        }
    }
    free(errorBuf);
}

BOOL SaveRawBufferToBMP(const char* szFileName) {
    // 1. Prepare Headers
    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    int stride, y;
    DWORD dwDataSize, dwWritten;
    HANDLE hFile;

    if (!pRawData) return FALSE;

    // Calculate the stride (how many bytes per row in our GDI-compatible buffer)
    stride = ((imgWidth * 24 + 31) / 32) * 4;
    dwDataSize = (DWORD)stride * imgHeight;

    memset(&bfh, 0, sizeof(bfh));
    bfh.bfType = 0x4D42; // "BM"
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + dwDataSize;

    memset(&bih, 0, sizeof(bih));
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = imgWidth;
    bih.biHeight = imgHeight; // Use positive for standard bottom-up BMP file
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;

    // 2. Open File
    hFile = CreateFile(szFileName, GENERIC_WRITE, 0, NULL, 
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    // 3. Write Headers
    WriteFile(hFile, &bfh, sizeof(bfh), &dwWritten, NULL);
    WriteFile(hFile, &bih, sizeof(bih), &dwWritten, NULL);

    // 4. Write Pixel Data
    // New, simplified version for Bottom-Up buffer
    for (y = 0; y < imgHeight; y++) {
        unsigned char* pRow = pRawData + (y * stride);
        WriteFile(hFile, pRow, stride, &dwWritten, NULL);
    }

    CloseHandle(hFile);
    return TRUE;
}

void UpdateWindowTitle(HWND hwnd, char* filePath) {
    char newTitle[500];
    wsprintf(newTitle, "%s (D=%d) - %s", MAINWIN_TITLE, FSdither, *filePath ? filePath : MAINWIN_TITLE_SUFFIX);
    SetWindowText(hwnd, newTitle);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    HWND hwnd;
    MSG msg;
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCEA(IDI_APPICON));;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = MAINWIN_CLASS;

    RegisterClass(&wc);

    hwnd = CreateWindowEx(WS_EX_ACCEPTFILES, MAINWIN_CLASS, MAINWIN_TITLE " - " MAINWIN_TITLE_SUFFIX,
        WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    // --- COMMAND LINE PROCESSING ---
    // __argc and __argv are globals defined in stdlib.h / TCHAR.h
    // index 0 is the EXE path, index 1 is the first argument (the file)
    if (__argc > 1) {
        LoadImageFromPath(hwnd, __argv[1]);
        UpdateWindowTitle(hwnd, szFile);
    }

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}

// Recalculates scrollbar ranges based on current window size
void UpdateScrollbars(HWND hwnd) {
    int cw, ch;
    RECT clientRect;
    SCROLLINFO si = { sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL };
    MyGetScrollInfo(hwnd, SB_HORZ, &si);

    GetClientRect(hwnd, &clientRect);
    cw = clientRect.right;
    ch = clientRect.bottom;

    // Horizontal
    si.nMin = 0;
    si.nMax = imgWidth;
    if(MyGetScrollInfo == MyGetScrollInfo_fallback) si.nMax -= cw;
    si.nPage = cw;
    MySetScrollInfo(hwnd, SB_HORZ, &si, TRUE);

    // Vertical
    si.nMin = 0;
    si.nMax = imgHeight;
    if(MyGetScrollInfo == MyGetScrollInfo_fallback) si.nMax -= ch;
    si.nPage = ch;
    MySetScrollInfo(hwnd, SB_VERT, &si, TRUE);

    // Ensure scroll position doesn't hang out in "dead space" after shrink
    scrollX = GetScrollPos(hwnd, SB_HORZ);
    scrollY = GetScrollPos(hwnd, SB_VERT);
}

void LoadImageFromPath(HWND hwnd, char* filePath) {
    int imgW = 0, imgH = 0, channels, bpp, stride, x, y;
    unsigned char *pSrc, *pDest;
    char *fileExt;
    HDC hdcScreen;
    simplewebp *swebp;
    int isWebp = 0, isPCX = 0, swRet;
    char errBuf[MAX_PATH + 50];

    UpdateWindowTitle(hwnd, "Loading...");

    fileExt = strrchr(filePath, '.');

    if(fileExt &&
        (stricmp(fileExt,".webp") == 0 ||
         stricmp(fileExt,".wbp") == 0 || // proposed 3-letter extension for webp
         stricmp(fileExt,".web") == 0)) { // truncated 3-letter extension for webp
        // Try to load with simplewebp
        swRet = simplewebp_load_from_filename(filePath, NULL, &swebp);
        if (swRet == SIMPLEWEBP_NO_ERROR) {
            isWebp = 1;
            // Get image dimensions from webp
            simplewebp_get_dimensions(swebp, &imgW, &imgH);

            // Allocate memory for simplewebp output buffer (in RGBA form)
            pSrc = malloc(imgW * imgH * 4);
            if (!pSrc) {
                if(swebp) simplewebp_unload(swebp);
                MessageBox(hwnd, "Out of memory (webp)", "Error", MB_ICONERROR);
                return;
            }

            // decode webp with simplewebp
            swRet = simplewebp_decode(swebp, pSrc, NULL);
            if (swRet == SIMPLEWEBP_NO_ERROR) {
                /* transform rgba to rgb in-place */
                for(x=0; x < imgW*imgH; x++) {
                    pSrc[x * 3 + 0] = pSrc[x * 4 + 0];
                    pSrc[x * 3 + 1] = pSrc[x * 4 + 1];
                    pSrc[x * 3 + 2] = pSrc[x * 4 + 2];
                }
            } else {
                free(pSrc);
                pSrc = NULL;
            }
        }
        if(swebp) simplewebp_unload(swebp);
        if(swRet == SIMPLEWEBP_NOT_WEBP_ERROR) goto TrySTB;
    }
    else
    if(fileExt && stricmp(fileExt,".pcx") == 0) {
        isPCX = 1;
        pSrc = drpcx_load_file(filePath, DRPCX_FALSE, &imgW, &imgH, &channels, 3);
    }
    else
    {
TrySTB:
        // 1. Load raw packed RGB data from stb_image
        pSrc = stbi_load(filePath, &imgW, &imgH, &channels, 3);
    }

    if (!pSrc || !imgW || !imgH) {
        wsprintf(errBuf, "Failed to load:\n%s", filePath);
        MessageBox(hwnd, errBuf, "Error", MB_ICONERROR);
        return;
    }
    // 2. Calculate GDI Stride (DWORD Aligned)
    // Formula: ((Width * BitsPerPixel + 31) / 32) * 4
    stride = ((imgW * 24 + 31) / 32) * 4;
    
    // Allocate the destination buffer for GDI
    pDest = (unsigned char*)malloc(stride * imgH);
    if (!pDest) {
        if(!isWebp && !isPCX) stbi_image_free(pSrc);
        MessageBox(hwnd, "Out of memory", "Error", MB_ICONERROR);
        return;
    }

    // 3. Optional: Apply Dithering on the source buffer first
    // (Use the ApplyDithering function from our previous steps here)

    // Detect if we should dither (e.g., if bit depth is low)
    hdcScreen = GetDC(NULL);
    bpp = GetDeviceCaps(hdcScreen, BITSPIXEL);
    ReleaseDC(NULL, hdcScreen);

    if (FSdither == 1) { // auto FS dither when target surface bpp <= 8
        if (bpp <= 8) {
            UpdateWindowTitle(hwnd, "Dithering...");
            if (!hPalette)
                hPalette = CreateWebSafePalette();
            InitAllLUTs();
            ApplyDithering(pSrc, imgW, imgH, (bpp <= 4) ? 16 : 216);
        }
    } else if (FSdither > 1) { // force FS dither, to 16 colors when FSdither=2, to web-safe 256 colors otherwise
        UpdateWindowTitle(hwnd, "Dithering...");
        if (!hPalette)
            hPalette = CreateWebSafePalette();
        InitAllLUTs();
        ApplyDithering(pSrc, imgW, imgH, (FSdither == 2) ? 16 : 216);
    } // no FS dithering when FSdither=0

    // 4. SINGLE-PASS: Swizzle + Padded + Flip to Bottom-Up
    UpdateWindowTitle(hwnd, "Rendering...");
    for (y = 0; y < imgH; y++) {
        unsigned char *pSrcRow, *pDestRow;
        pSrcRow = &pSrc[y * imgW * 3];
        
        // Target the rows in reverse order: 
        // When y=0 (top of image), we write to the very last row of pDest.
        pDestRow = &pDest[(imgH - 1 - y) * stride];

        for (x = 0; x < imgW; x++) {
            pDestRow[x * 3 + 0] = pSrcRow[x * 3 + 2]; // Blue
            pDestRow[x * 3 + 1] = pSrcRow[x * 3 + 1]; // Green
            pDestRow[x * 3 + 2] = pSrcRow[x * 3 + 0]; // Red
        }

        // Note: The "extra" bytes at the end of pDestRow (the padding) 
        // don't need to be initialized; GDI simply ignores them.
    }

    // 5. Update Global State for Rendering
    if (pRawData) free(pRawData); // Free previous image buffer
    pRawData = pDest;             // Point to our new GDI-compatible buffer
    imgWidth = imgW;
    imgHeight = imgH;

    // 6. Update BITMAPINFO for StretchDIBits
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = imgW;
    bmi.bmiHeader.biHeight      = imgH; // POSITIVE value = Bottom-Up (Win32s friendly)
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    if(isWebp) free(pSrc);
    else if(isPCX) drpcx_free(pSrc);
    else stbi_image_free(pSrc); // Free the original stb_image buffer

    // Copy filename to global
    if(filePath != szFile) strcpy(szFile, filePath);

    // 7. Refresh UI
    scrollX = 0; scrollY = 0;
    UpdateScrollbars(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
}

void SaveFile(HWND hwnd) {
    OPENFILENAME ofn = {0};
    char szSaveFile[260] = "output.bmp";

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szSaveFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Bitmap Files (*.bmp)\0*.bmp\0";
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (GetSaveFileName(&ofn)) {
        if (SaveRawBufferToBMP(szSaveFile)) {
            MessageBox(hwnd, "File saved successfully!", "Success", MB_OK);
        } else {
            MessageBox(hwnd, "Failed to save file.", "Error", MB_ICONERROR);
        }
    }
}

void OpenPicFile(HWND hwnd) {
    OPENFILENAME ofn;
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Images\0*.jpg;*.png;*.gif;*.bmp;*.tga;*.pnm;*.ppm;*.pgm;*.webp;*.web;*.wbp;*.pcx\0All Files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        LoadImageFromPath(hwnd, szFile);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_ERASEBKGND:
            // Return 1 to tell Windows we will erase the background ourselves.
            // This is the most important step to stop flickering.
            return 1;

        case WM_QUERYNEWPALETTE:
            if (hPalette) {
                UINT u;
                HDC hdc = GetDC(hwnd);
                // Select our palette into the DC
                SelectPalette(hdc, hPalette, FALSE);
                // Map the logical palette to the physical system palette
                u = RealizePalette(hdc);
                ReleaseDC(hwnd, hdc);
                
                if (u > 0) {
                    // If any colors changed, redraw the whole window
                    InvalidateRect(hwnd, NULL, TRUE);
                    return TRUE; // We handled it
                }
            }
            return FALSE;

        case WM_PALETTECHANGED:
            // If another window changed the palette, and it wasn't us...
            if (hPalette && (HWND)wParam != hwnd) {
                HDC hdc = GetDC(hwnd);
                SelectPalette(hdc, hPalette, FALSE);
                RealizePalette(hdc);
                ReleaseDC(hwnd, hdc);
                // Update immediately to show background-friendly colors
                UpdateWindow(hwnd);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            RECT rc;
            HBRUSH hBrush;
            int srcH, srcY;
            HDC hdc = BeginPaint(hwnd, &ps);

            GetClientRect(hwnd, &rc);
            hBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));

            if (pRawData) {

                // IMPORTANT: You must select/realize before every draw call
                if (hPalette) {
                    SelectPalette(hdc, hPalette, FALSE);
                    RealizePalette(hdc);
                }

                if (mvi_isWin32s()) {
                    // --- WIN32S PATH: Direct to Screen (Most Stable) ---

                    // Fill background manually since we trapped WM_ERASEBKGND
                    FillRect(hdc, &rc, hBrush);
                    DeleteObject(hBrush);

                    // Win32s handles SetDIBitsToDevice much better than Memory DCs
                    srcH = rc.bottom;
                    srcY = imgHeight - scrollY - srcH;

                    StretchDIBits(hdc, 
                        0, 0, rc.right, rc.bottom,
                        scrollX, srcY, 
                        rc.right, srcH,
                        pRawData, &bmi, DIB_RGB_COLORS, SRCCOPY);
                } 
                else {
                    // --- NT4/9x PATH: Double Buffered (Flicker-Free) ---
                    HBITMAP hMemBmp, hOldBmp;
                    HDC hMemDC = CreateCompatibleDC(hdc);
                    hMemBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
                    hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

                    if (hPalette) {
                        SelectPalette(hMemDC, hPalette, FALSE);
                        RealizePalette(hMemDC);
                    }

                    // Fill background in the memory DC
                    FillRect(hMemDC, &rc, hBrush);
                    DeleteObject(hBrush);

                    // On NT4, we can even use StretchDIBits in the memory DC
                    srcH = rc.bottom;
                    srcY = imgHeight - scrollY - srcH;

                    StretchDIBits(hMemDC, 
                        0, 0, rc.right, rc.bottom,
                        scrollX, srcY, 
                        rc.right, srcH,
                        pRawData, &bmi, DIB_RGB_COLORS, SRCCOPY);

                    // "Flip" the buffer: Copy the memory DC to the real screen DC
                    BitBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, 0, 0, SRCCOPY);

                    SelectObject(hMemDC, hOldBmp);
                    DeleteObject(hMemBmp);
                    DeleteDC(hMemDC);
                }
            } else {
                // Fill background manually since we trapped WM_ERASEBKGND
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
            UpdateScrollbars(hwnd);
            return 0;

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            char szDropFile[MAX_PATH];

            // Get the count of dropped files (we only care about the first one)
            if (DragQueryFile(hDrop, 0, szDropFile, MAX_PATH)) {
                LoadImageFromPath(hwnd, szDropFile);
                UpdateWindowTitle(hwnd, szFile);
            }

            // Must release the handle allocated by the shell
            DragFinish(hDrop);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            // GET_WHEEL_DELTA_WPARAM(wParam) - Extracting the delta manually for old headers
            short zDelta = (short)HIWORD(wParam);

            // Delta is usually 120 (WHEEL_DELTA), so we scroll 3 'lines' per click
            int i, steps = zDelta / WHEEL_DELTA;
            for (i = 0; i < abs(steps); i++) {
                SendMessage(hwnd, GetKeyState(VK_CONTROL) & 0x8000 ? WM_HSCROLL : WM_VSCROLL, (zDelta > 0) ? SB_LINEUP : SB_LINEDOWN, 0);
            }
            return 0;
        }

        case WM_KEYDOWN:
            switch (wParam) {
                // Vertical Navigation
                case VK_UP:
                    SendMessage(hwnd, WM_VSCROLL, SB_LINEUP, 0);
                    break;
                case VK_DOWN:
                    SendMessage(hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
                    break;
                case VK_PRIOR: // Page Up
                    SendMessage(hwnd, WM_VSCROLL, SB_PAGEUP, 0);
                    break;
                case VK_NEXT:  // Page Down
                    SendMessage(hwnd, WM_VSCROLL, SB_PAGEDOWN, 0);
                    break;
                case VK_HOME:
                    SendMessage(hwnd, WM_VSCROLL, SB_TOP, 0);
                    break;
                case VK_END:
                    SendMessage(hwnd, WM_VSCROLL, SB_BOTTOM, 0);
                    break;

                // Horizontal Navigation
                case VK_LEFT:
                    SendMessage(hwnd, WM_HSCROLL, SB_LINEUP, 0); // SB_LINEUP is used for Left
                    break;
                case VK_RIGHT:
                    SendMessage(hwnd, WM_HSCROLL, SB_LINEDOWN, 0); // SB_LINEDOWN is used for Right
                    break;

                // File Operations
                case 'O':
                    OpenPicFile(hwnd);
                    UpdateWindowTitle(hwnd, szFile);
                    break;
                case 'D':
                    FSdither = FSdither == 3 ? 0 : FSdither+1;
                    UpdateWindowTitle(hwnd, szFile);
                    break;
                case 'S':
                    SaveFile(hwnd);
                    break;
                case VK_ESCAPE:
                    PostQuitMessage(0);
                    break;
            }
            return 0;

        case WM_HSCROLL:
        case WM_VSCROLL: {
            int oldPos = 0, dx = 0, dy = 0;
            int bar = (uMsg == WM_HSCROLL) ? SB_HORZ : SB_VERT;
            SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };
            si.nTrackPos = HIWORD(wParam);
            MyGetScrollInfo(hwnd, bar, &si);

            oldPos = si.nPos;
            switch (LOWORD(wParam)) {
                case SB_TOP:        si.nPos = si.nMin; break;
                case SB_BOTTOM:     si.nPos = si.nMax; break;
                case SB_LINEUP:     si.nPos -= 20; break;
                case SB_LINEDOWN:   si.nPos += 20; break;
                case SB_PAGEUP:     si.nPos -= si.nPage; break;
                case SB_PAGEDOWN:   si.nPos += si.nPage; break;
                case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
            }

            si.fMask = SIF_POS;
            MySetScrollInfo(hwnd, bar, &si, TRUE);
            MyGetScrollInfo(hwnd, bar, &si); // Get clamped position

            if (uMsg == WM_HSCROLL) {
                scrollX = si.nPos;
                dx = oldPos - scrollX;
            } else {
                scrollY = si.nPos;
                dy = oldPos - scrollY;
            }

            // Scroll existing pixels and only invalidate the "new" area
            ScrollWindowEx(hwnd, dx, dy, NULL, NULL, NULL, NULL, SW_INVALIDATE | SW_ERASE);
            UpdateWindow(hwnd); // Forces immediate repaint
            return 0;
        }

        case WM_DESTROY:
            if (hPalette) DeleteObject(hPalette);
            if (pRawData) free(pRawData);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
