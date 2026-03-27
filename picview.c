#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"
#include <windows.h>
#include <commdlg.h>

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void OpenPicFile(HWND hwnd);

HBITMAP hBitmap = NULL;
HPALETTE hPalette = NULL; // New: Palette handle
int imgWidth = 0, imgHeight = 0;
int scrollX = 0, scrollY = 0;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    HWND hwnd;
    MSG msg;
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "NT4_STB_Viewer";

    RegisterClass(&wc);

    hwnd = CreateWindowEx(WS_EX_ACCEPTFILES, "NT4_STB_Viewer", "NT4 Image Viewer (stb_image) - Press 'O' to open file",
        WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

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

    GetClientRect(hwnd, &clientRect);
    cw = clientRect.right;
    ch = clientRect.bottom;

    // Horizontal
    si.nMin = 0;
    si.nMax = imgWidth;
    si.nPage = cw;
    SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);

    // Vertical
    si.nMin = 0;
    si.nMax = imgHeight;
    si.nPage = ch;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

    // Ensure scroll position doesn't hang out in "dead space" after shrink
    scrollX = GetScrollPos(hwnd, SB_HORZ);
    scrollY = GetScrollPos(hwnd, SB_VERT);
}

void LoadImageFromPath(HWND hwnd, char* filePath) {
    int channels;
    // stb_image loads pixels as RGBA/RGB
    unsigned char *data = stbi_load(filePath, &imgWidth, &imgHeight, &channels, 4);

    if (data) {
        BITMAPINFO bmi = {0};
        void *pBits;
        HDC hdc;
        RECT r;
        int i;
        unsigned char *pDest;

        if (hBitmap) DeleteObject(hBitmap);
        if (hPalette) DeleteObject(hPalette);

        // 1. Create a Halftone Palette for 8-bit displays
        hdc = GetDC(hwnd);
        hPalette = CreateHalftonePalette(hdc);

        // 2. Prepare Bitmap Info
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = imgWidth;
        bmi.bmiHeader.biHeight = -imgHeight;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        // Swizzle RGBA -> BGRA
        for (i = 0; i < imgWidth * imgHeight * 4; i += 4) {
            unsigned char r = data[i];
            data[i] = data[i + 2];
            data[i + 2] = r;
        }

        // --- METHOD 1: CreateDIBSection (Modern/Efficient) ---
        hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);

        if (hBitmap && pBits) {
            // Success: Copy swizzled data to the DIB pointer
            memcpy(pBits, data, imgWidth * imgHeight * 4);
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
                // IMPORTANT: Select and Realize Palette in the Paint DC
                if (hPalette) {
                    SelectPalette(hdc, hPalette, FALSE);
                    RealizePalette(hdc);
                }

                hdcMem = CreateCompatibleDC(hdc);
                SelectObject(hdcMem, hBitmap);
                
                // Use SetStretchBltMode for better quality in 8-bit
                SetStretchBltMode(hdc, COLORONCOLOR);

                BitBlt(hdc, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom, 
                       hdcMem, scrollX, scrollY, SRCCOPY);
                
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
            GetScrollInfo(hwnd, bar, &si);

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
            SetScrollInfo(hwnd, bar, &si, TRUE);
            GetScrollInfo(hwnd, bar, &si); // Get clamped position

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
