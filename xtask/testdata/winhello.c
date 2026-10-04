// SPDX-License-Identifier: GPL-2.0-or-later
// A real Win32 program (built with mingw, no CRT): one window, a WM_PAINT handler that draws
// coloured rectangles, a click handler that recolours it. Runs on THOS's NT personality; under the
// desktop it is one more window next to the Linux clients.
#include <windows.h>

static int clicks;

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        HDC dc = GetDC(h);
        HBRUSH bg = CreateSolidBrush(RGB(30, 160, 200 - 60 * (clicks & 1)));
        SelectObject(dc, bg);
        Rectangle(dc, 0, 0, 300, 200);
        SelectObject(dc, CreateSolidBrush(RGB(250, 220, 40)));
        Rectangle(dc, 40, 40, 140, 100);
        ReleaseDC(h, dc);
        return 0;
    }
    if (m == 0x201) {   /* WM_LBUTTONDOWN */
        clicks++;
        PostMessageA(h, WM_PAINT, 0, 0);
        return 0;
    }
    if (m == 0x10) {    /* WM_CLOSE */
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

void __attribute__((noreturn)) start(void) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = proc;
    wc.lpszClassName = "winhello";
    RegisterClassA(&wc);
    HWND h = CreateWindowExA(0, "winhello", "winhello", 0, 0, 0, 300, 200, 0, 0, 0, 0);
    ShowWindow(h, 1);
    UpdateWindow(h);
    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
