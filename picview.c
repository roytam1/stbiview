#define MAINWIN_CLASS "NT4_STB_Viewer"
#define MAINWIN_TITLE "Simple Image Viewer"

#include <windows.h>
#include <commdlg.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"

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

HBITMAP hBitmap = NULL;
HPALETTE hPalette = NULL; // New: Palette handle
int imgWidth = 0, imgHeight = 0;
int scrollX = 0, scrollY = 0;

typedef struct { BYTE r, g, b; } RGB_TRIPLE;

// Standard 16-color VGA Palette
RGB_TRIPLE vga16[16] = {
    {0,0,0}, {128,0,0}, {0,128,0}, {128,128,0}, {0,0,128}, {128,0,128}, {0,128,128}, {192,192,192},
    {128,128,128}, {255,0,0}, {0,255,0}, {255,255,0}, {0,0,255}, {255,0,255}, {0,255,255}, {255,255,255}
};

RGB_TRIPLE web216[216]; // The "6x6x6" color cube

void InitWebSafePalette() {
    int r, g, b, i = 0;
    for (r = 0; r < 6; r++)
        for (g = 0; g < 6; g++)
            for (b = 0; b < 6; b++) {
                web216[i].r = r * 51;
                web216[i].g = g * 51;
                web216[i].b = b * 51;
                i++;
            }
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

// Floyd-Steinberg Dither Function
void ApplyDithering(unsigned char* pixels, int width, int height, int colorCount) {
    // Error buffer: current row and next row to save memory
    // Layout: [red, green, blue] per pixel
    int i, palCount, x, y;
    RGB_TRIPLE *pal;
    int* errorBuf = (int*)calloc(width * height * 3, sizeof(int));
    if (!errorBuf) return;

    for (i = 0; i < width * height * 3; i++) errorBuf[i] = pixels[i] << 8;

    pal = (colorCount == 16) ? vga16 : web216;
    palCount = (colorCount == 16) ? 16 : 216;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            RGB_TRIPLE oldP, newP;
            int errR, errG, errB, i;
            int dx[] = {1, -1, 0, 1}, dy[] = {0, 1, 1, 1}, w[] = {7, 3, 5, 1};
            int pIdx, idx = (y * width + x) * 3;

            oldP.r = (BYTE)max(0, min(255, errorBuf[idx] >> 8));
            oldP.g = (BYTE)max(0, min(255, errorBuf[idx+1] >> 8));
            oldP.b = (BYTE)max(0, min(255, errorBuf[idx+2] >> 8));

            pIdx = FindClosestColor(oldP, pal, palCount);
            newP = pal[pIdx];

            pixels[idx]   = newP.r;
            pixels[idx+1] = newP.g;
            pixels[idx+2] = newP.b;

            errR = errorBuf[idx]   - (newP.r << 8);
            errG = errorBuf[idx+1] - (newP.g << 8);
            errB = errorBuf[idx+2] - (newP.b << 8);

            // Diffusion: 7/16 right, 3/16 down-left, 5/16 down, 1/16 down-right
            // We use bit shifts for speed: (err * 7) >> 4 is approx err * (7/16)
            for (i = 0; i < 4; i++) {
                int nx = x + dx[i], ny = y + dy[i];
                if (nx >= 0 && nx < width && ny < height) {
                    int nIdx = (ny * width + nx) * 3;
                    errorBuf[nIdx]   += (errR * w[i]) >> 4;
                    errorBuf[nIdx+1] += (errG * w[i]) >> 4;
                    errorBuf[nIdx+2] += (errB * w[i]) >> 4;
                }
            }
        }
    }
    free(errorBuf);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    HWND hwnd;
    MSG msg;
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = MAINWIN_CLASS;

    RegisterClass(&wc);

    hwnd = CreateWindowEx(WS_EX_ACCEPTFILES, MAINWIN_CLASS, MAINWIN_TITLE " - Press 'O' to open file",
        WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    // --- COMMAND LINE PROCESSING ---
    // __argc and __argv are globals defined in stdlib.h / TCHAR.h
    // index 0 is the EXE path, index 1 is the first argument (the file)
    if (__argc > 1) {
        LoadImageFromPath(hwnd, __argv[1]);
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
    int channels;
    // stb_image loads pixels as RGBA/RGB
    unsigned char *data = stbi_load(filePath, &imgWidth, &imgHeight, &channels, 3);

    if (!data) {
        char errBuf[MAX_PATH + 50];
        wsprintf(errBuf, "Failed to load:\n%s", filePath);
        MessageBox(hwnd, errBuf, "Error", MB_ICONERROR);
        return;
    } else {
        BITMAPINFO bmi = {0};
        void *pBits;
        HDC hdc, screenHdc;
        RECT r;
        int bpp, i;

        // Detect if we should dither (e.g., if bit depth is low)
        screenHdc = GetDC(NULL);
        bpp = GetDeviceCaps(screenHdc, BITSPIXEL);
        ReleaseDC(NULL, screenHdc);

        if (hBitmap) DeleteObject(hBitmap);
        if (hPalette) DeleteObject(hPalette);

        if (bpp <= 8) {
            InitWebSafePalette();
            ApplyDithering(data, imgWidth, imgHeight, (bpp <= 4) ? 16 : 256);
        }

        // 1. Create a Halftone Palette for 8-bit displays
        hdc = GetDC(hwnd);
        hPalette = CreateHalftonePalette(hdc);

        // 2. Prepare Bitmap Info
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = imgWidth;
        bmi.bmiHeader.biHeight = -imgHeight;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        // Swizzle RGBA -> BGRA
        for (i = 0; i < imgWidth * imgHeight * 3; i += 3) {
            unsigned char r = data[i];
            data[i] = data[i + 2];
            data[i + 2] = r;
        }

        // --- METHOD 1: CreateDIBSection (Modern/Efficient) ---
        hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);

        if (hBitmap && pBits) {
            // Success: Copy swizzled data to the DIB pointer
            memcpy(pBits, data, imgWidth * imgHeight * 3);
        }
        else {
            // --- METHOD 2: Fallback (DDB + SetDIBits) ---
            // Create a bitmap compatible with the current display
            hBitmap = CreateCompatibleBitmap(hdc, imgWidth, imgHeight);
            if (hBitmap) {
                // Manually push the pixel data into the bitmap handle
                SetDIBits(hdc, hBitmap, 0, imgHeight, data, &bmi, DIB_RGB_COLORS);
            }
        }

        ReleaseDC(hwnd, hdc);
        stbi_image_free(data);

        // Reset view
        scrollX = 0; scrollY = 0;
        GetClientRect(hwnd, &r);
        SendMessage(hwnd, WM_SIZE, 0, MAKELPARAM(r.right, r.bottom));
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

void OpenPicFile(HWND hwnd) {
    OPENFILENAME ofn;
    char szFile[260] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Images\0*.jpg;*.png;*.bmp;*.tga\0All Files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        LoadImageFromPath(hwnd, szFile);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_QUERYNEWPALETTE:
            if (hPalette) {
                UINT updated;
                HDC hdc = GetDC(hwnd);
                SelectPalette(hdc, hPalette, FALSE);
                updated = RealizePalette(hdc);
                ReleaseDC(hwnd, hdc);
                if (updated > 0) InvalidateRect(hwnd, NULL, TRUE);
                return TRUE;
            }
            return FALSE;

        case WM_PALETTECHANGED:
            if (hPalette && (HWND)wParam != hwnd) {
                HDC hdc = GetDC(hwnd);
                SelectPalette(hdc, hPalette, FALSE);
                RealizePalette(hdc);
                ReleaseDC(hwnd, hdc);
                UpdateWindow(hwnd);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hBitmap) {
                HDC hdcMem;

                hdcMem = CreateCompatibleDC(hdc);
                SelectObject(hdcMem, hBitmap);

                // IMPORTANT: Select and Realize Palette in the Paint DC
                if (hPalette) {
                    SelectPalette(hdc, hPalette, FALSE);
                    RealizePalette(hdc);
                    SelectPalette(hdcMem, hPalette, FALSE);
                    RealizePalette(hdcMem);
                }

#if 1
                // Set scaling mode for better quality
                SetStretchBltMode(hdc, HALFTONE);
                // Requirement for HALFTONE: Set brush origin
                SetBrushOrgEx(hdc, 0, 0, NULL);
#else
                // Use SetStretchBltMode for better quality in 8-bit
                SetStretchBltMode(hdc, COLORONCOLOR);
#endif

                StretchBlt(hdc, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom, 
                       hdcMem, scrollX, scrollY, ps.rcPaint.right, ps.rcPaint.bottom, SRCCOPY);
                
                DeleteDC(hdcMem);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
            UpdateScrollbars(hwnd);
            return 0;

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            char szFile[MAX_PATH];

            // Get the count of dropped files (we only care about the first one)
            if (DragQueryFile(hDrop, 0, szFile, MAX_PATH)) {
                LoadImageFromPath(hwnd, szFile);
            }

            // Must release the handle allocated by the shell
            DragFinish(hDrop);
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
                    break;
                case VK_ESCAPE:
                    PostQuitMessage(0);
                    break;
            }
            return 0;

        case WM_HSCROLL:
        case WM_VSCROLL: {
            int oldPos;
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

            if (uMsg == WM_HSCROLL) scrollX = si.nPos;
            else scrollY = si.nPos;

            // Only redraw if the position actually changed
            if (oldPos != si.nPos) {
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }

        case WM_DESTROY:
            if (hBitmap) DeleteObject(hBitmap);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
