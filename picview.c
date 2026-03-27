#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"
#include <windows.h>
#include <commdlg.h>

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void OpenPicFile(HWND hwnd);

HBITMAP hBitmap = NULL;
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

    hwnd = CreateWindow("NT4_STB_Viewer", "NT4 Image Viewer (stb_image) - Press 'O'",
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
        int channels;
        // stb_image loads pixels as RGBA/RGB
        unsigned char *data = stbi_load(szFile, &imgWidth, &imgHeight, &channels, 4);
        
        if (data) {
            BITMAPINFO bmi = {0};
            void *pBits;
            HDC hdc;
            RECT r;
            int i;
            unsigned char *pDest;

            if (hBitmap) DeleteObject(hBitmap);

            // Create a DIB Section so GDI can use the raw pixel data
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = imgWidth;
            bmi.bmiHeader.biHeight = -imgHeight; // Negative for top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            hdc = GetDC(hwnd);

            // Perform Byte Swizzle (RGBA -> BGRA)
            for (i = 0; i < imgWidth * imgHeight * 4; i += 4) {
                unsigned char r = data[i];
                data[i] = data[i + 2]; // Blue
                data[i + 2] = r;       // Red
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
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_KEYDOWN:
            if (wParam == 'O') OpenPicFile(hwnd);
            return 0;

        case WM_SIZE:
            UpdateScrollbars(hwnd);
            return 0;

        case WM_HSCROLL:
        case WM_VSCROLL: {
            int oldPos;
            int bar = (uMsg == WM_HSCROLL) ? SB_HORZ : SB_VERT;
            SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };
            GetScrollInfo(hwnd, bar, &si);

            oldPos = si.nPos;
            switch (LOWORD(wParam)) {
                case SB_TOP:           si.nPos = si.nMin; break;
                case SB_BOTTOM:        si.nPos = si.nMax; break;
                case SB_LINEUP:        si.nPos -= 20; break;
                case SB_LINEDOWN:      si.nPos += 20; break;
                case SB_PAGEUP:        si.nPos -= si.nPage; break;
                case SB_PAGEDOWN:      si.nPos += si.nPage; break;
                case SB_THUMBTRACK:    si.nPos = si.nTrackPos; break;
            }

            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, bar, &si, TRUE);
            GetScrollInfo(hwnd, bar, &si); // Get clamped pos

            if (uMsg == WM_HSCROLL) scrollX = si.nPos;
            else scrollY = si.nPos;

            if (oldPos != si.nPos) InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hBitmap) {
                HDC hdcMem = CreateCompatibleDC(hdc);
                SelectObject(hdcMem, hBitmap);
                BitBlt(hdc, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom, 
                       hdcMem, scrollX + ps.rcPaint.left, scrollY + ps.rcPaint.top, SRCCOPY);
                DeleteDC(hdcMem);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            if (hBitmap) DeleteObject(hBitmap);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
