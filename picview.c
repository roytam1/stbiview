#include <windows.h>
#include <commdlg.h>

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void OpenPicFile(HWND hwnd);

HBITMAP hBitmap = NULL;
int bmpWidth = 0, bmpHeight = 0;
int scrollX = 0, scrollY = 0;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    HWND hwnd;
    MSG msg;
    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "NT4_BMP_Viewer";

    RegisterClass(&wc);

    hwnd = CreateWindow("NT4_BMP_Viewer", "NT4 BMP Viewer - Press 'O' to Open",
        WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}

void OpenPicFile(HWND hwnd) {
    OPENFILENAME ofn;
    char szFile[260] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Bitmaps (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        if (hBitmap) DeleteObject(hBitmap);
        
        hBitmap = (HBITMAP)LoadImage(NULL, szFile, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
        if (hBitmap) {
            RECT rect;
            BITMAP bmp;
            GetObject(hBitmap, sizeof(BITMAP), &bmp);
            bmpWidth = bmp.bmWidth;
            bmpHeight = bmp.bmHeight;
            scrollX = 0; scrollY = 0;
            
            // Refresh scrollbars and window
            GetClientRect(hwnd, &rect);
            SendMessage(hwnd, WM_SIZE, 0, MAKELPARAM(rect.right, rect.bottom));
            InvalidateRect(hwnd, NULL, TRUE);
        }
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_KEYDOWN:
            if (wParam == 'O') OpenPicFile(hwnd);
            return 0;

        case WM_SIZE: {
            int cx = LOWORD(lParam);
            int cy = HIWORD(lParam);
            SCROLLINFO si = { sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS };
            
            si.nMax = bmpWidth; si.nPage = cx; si.nPos = scrollX;
            SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
            
            si.nMax = bmpHeight; si.nPage = cy; si.nPos = scrollY;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            return 0;
        }

        case WM_HSCROLL:
        case WM_VSCROLL: {
            int bar = (uMsg == WM_HSCROLL) ? SB_HORZ : SB_VERT;
            int* pos = (uMsg == WM_HSCROLL) ? &scrollX : &scrollY;
            
            SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };
            GetScrollInfo(hwnd, bar, &si);

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
            GetScrollInfo(hwnd, bar, &si); // Get clamped position
            *pos = si.nPos;

            InvalidateRect(hwnd, NULL, TRUE);
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
