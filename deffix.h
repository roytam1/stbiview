#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif
#ifndef WHEEL_DELTA
#define WHEEL_DELTA 120
#endif

#ifndef VER_PLATFORM_WIN32_WINDOWS
/* very old WinSDK, putting more defines inside this guard */
#define VER_PLATFORM_WIN32_WINDOWS 1

#define MB_ICONERROR MB_ICONHAND

#define SIF_RANGE           0x0001
#define SIF_PAGE            0x0002
#define SIF_POS             0x0004
#define SIF_DISABLENOSCROLL 0x0008
#define SIF_TRACKPOS        0x0010
#define SIF_ALL             (SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS)
typedef struct tagSCROLLINFO
{
    UINT    cbSize;
    UINT    fMask;
    int     nMin;
    int     nMax;
    UINT    nPage;
    int     nPos;
    int     nTrackPos;
}   SCROLLINFO, FAR *LPSCROLLINFO;
typedef SCROLLINFO CONST FAR *LPCSCROLLINFO;

int WINAPI _imp__MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType) {
    return MessageBoxExA(hWnd, lpText, lpCaption, uType, 0);
}

#define MessageBox(a,b,c,d) MessageBoxExA(a,b,c,d,0);
#endif
