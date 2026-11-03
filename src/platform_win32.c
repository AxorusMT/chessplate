/* Win32 backend: plain user32/gdi32, no toolkit. */
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

static HWND g_hwnd;
static HICON g_icon;
static int g_width, g_height;
static int g_min_width, g_min_height;
static InputState *g_input;
static bool g_quit;
static bool g_tracking_leave;
static const uint32_t *g_last_pixels;
static int g_last_w, g_last_h;

static void blit(HDC dc, const uint32_t *pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0) return;
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0, 0, (DWORD)w, (DWORD)h, 0, 0, 0, (UINT)h, pixels, &bmi, DIB_RGB_COLORS);
}

static void mouse_button(int button, bool press, LPARAM lp) {
    if (!g_input) return;
    g_input->mouse_x = (float)GET_X_LPARAM(lp);
    g_input->mouse_y = (float)GET_Y_LPARAM(lp);
    g_input->mouse_inside = true;
    if (press) {
        g_input->pressed[button] = true;
        g_input->down[button] = true;
        SetCapture(g_hwnd);
    } else {
        g_input->released[button] = true;
        g_input->down[button] = false;
        if (!g_input->down[0] && !g_input->down[1] && !g_input->down[2]) ReleaseCapture();
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY: g_quit = true; return 0;
    case WM_SIZE:
        g_width = LOWORD(lp);
        g_height = HIWORD(lp);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        RECT r = {0, 0, g_min_width, g_min_height};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_input) {
            g_input->mouse_x = (float)GET_X_LPARAM(lp);
            g_input->mouse_y = (float)GET_Y_LPARAM(lp);
            g_input->mouse_inside = true;
        }
        if (!g_tracking_leave) {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            g_tracking_leave = true;
        }
        return 0;
    case WM_MOUSELEAVE:
        g_tracking_leave = false;
        if (g_input && !(g_input->down[0] || g_input->down[1] || g_input->down[2])) g_input->mouse_inside = false;
        return 0;
    case WM_LBUTTONDOWN: mouse_button(MOUSE_LEFT, true, lp); return 0;
    case WM_LBUTTONUP: mouse_button(MOUSE_LEFT, false, lp); return 0;
    case WM_RBUTTONDOWN: mouse_button(MOUSE_RIGHT, true, lp); return 0;
    case WM_RBUTTONUP: mouse_button(MOUSE_RIGHT, false, lp); return 0;
    case WM_MBUTTONDOWN: mouse_button(MOUSE_MIDDLE, true, lp); return 0;
    case WM_MBUTTONUP: mouse_button(MOUSE_MIDDLE, false, lp); return 0;
    case WM_MOUSEWHEEL:
        if (g_input) g_input->wheel += (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
        return 0;
    case WM_KEYDOWN:
        if (g_input && wp == VK_ESCAPE) g_input->key_escape = true;
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        blit(dc, g_last_pixels, g_last_w, g_last_h);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HICON make_icon(const Image *icon) {
    if (!icon || !icon->pixels) return NULL;
    BITMAPV5HEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = icon->w;
    bi.bV5Height = -icon->h;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00ff0000;
    bi.bV5GreenMask = 0x0000ff00;
    bi.bV5BlueMask = 0x000000ff;
    bi.bV5AlphaMask = 0xff000000;

    HDC dc = GetDC(NULL);
    void *bits = NULL;
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!color || !bits) return NULL;

    uint32_t *dst = (uint32_t *)bits;
    for (int i = 0; i < icon->w * icon->h; i++) {
        /* Icons use straight alpha. */
        uint32_t p = icon->pixels[i];
        uint32_t a = p >> 24, r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
        if (a > 0 && a < 255) {
            r = r * 255 / a;
            g = g * 255 / a;
            b = b * 255 / a;
        }
        dst[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    HBITMAP mask = CreateBitmap(icon->w, icon->h, 1, 1, NULL);
    ICONINFO ii;
    memset(&ii, 0, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON result = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return result;
}

static wchar_t *utf8_to_wide(const char *s, bool crlf) {
    /* Optionally convert "\n" to "\r\n" for the Windows clipboard. */
    size_t len = strlen(s), extra = 0;
    if (crlf)
        for (size_t i = 0; i < len; i++)
            if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r')) extra++;
    char *tmp = (char *)malloc(len + extra + 1);
    if (!tmp) return NULL;
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (crlf && s[i] == '\n' && (i == 0 || s[i - 1] != '\r')) tmp[n++] = '\r';
        tmp[n++] = s[i];
    }
    tmp[n] = '\0';
    int wlen = MultiByteToWideChar(CP_UTF8, 0, tmp, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, tmp, -1, w, wlen);
    free(tmp);
    return w;
}

bool platform_init(const char *title, int width, int height, int min_width, int min_height, const Image *icon) {
    HINSTANCE instance = GetModuleHandleW(NULL);
    g_min_width = min_width;
    g_min_height = min_height;
    g_icon = make_icon(icon);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = g_icon;
    wc.hIconSm = g_icon;
    wc.lpszClassName = L"ChessplateWindow";
    if (!RegisterClassExW(&wc)) return false;

    RECT r = {0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    wchar_t *wtitle = utf8_to_wide(title, false);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, wtitle ? wtitle : L"Chessplate", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, instance,
                             NULL);
    free(wtitle);
    if (!g_hwnd) return false;
    if (g_icon) {
        SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_icon);
        SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_icon);
    }
    RECT client;
    GetClientRect(g_hwnd, &client);
    g_width = client.right - client.left;
    g_height = client.bottom - client.top;
    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);
    return true;
}

void platform_shutdown(void) {
    if (g_hwnd) DestroyWindow(g_hwnd);
    g_hwnd = NULL;
    if (g_icon) DestroyIcon(g_icon);
    g_icon = NULL;
}

void platform_wait_events(InputState *in, int timeout_ms) {
    g_input = in;
    MSG msg;
    if (timeout_ms != 0 && !PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE)) {
        MsgWaitForMultipleObjects(0, NULL, FALSE, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms, QS_ALLINPUT);
    }
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) g_quit = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_quit) in->quit = true;
}

void platform_window_size(int *width, int *height) {
    *width = g_width > 0 ? g_width : 1;
    *height = g_height > 0 ? g_height : 1;
}

void platform_present(const uint32_t *pixels, int width, int height) {
    g_last_pixels = pixels;
    g_last_w = width;
    g_last_h = height;
    if (!g_hwnd) return;
    HDC dc = GetDC(g_hwnd);
    blit(dc, pixels, width, height);
    ReleaseDC(g_hwnd, dc);
}

void platform_set_clipboard(const char *utf8) {
    if (!g_hwnd || !OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    wchar_t *w = utf8_to_wide(utf8, true);
    if (w) {
        size_t bytes = (wcslen(w) + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem) {
            void *dst = GlobalLock(mem);
            if (dst) {
                memcpy(dst, w, bytes);
                GlobalUnlock(mem);
                if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
            } else {
                GlobalFree(mem);
            }
        }
        free(w);
    }
    CloseClipboard();
}

#endif /* _WIN32 */
