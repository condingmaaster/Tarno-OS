// SPDX-License-Identifier: GPL-2.0-or-later
// Real PE client: button actions complete on release, not just on press.
#include <windows.h>
#include "winframe-test.h"
static unsigned stage;
static void say(const char *s) { DWORD n; unsigned len=0;while(s[len])len++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,len,&n,0); }
static void next(HWND h,unsigned n,const char *s) { stage=n;say(s);PostMessageA(h,WM_PAINT,0,0); }
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    int x=(short)(l&0xffff),y=(short)((l>>16)&0xffff);
    if(m==WM_PAINT) {
        HDC dc=GetDC(h);SelectObject(dc,CreateSolidBrush(stage==7?RGB(46,180,90):RGB(30,160,200)));
        Rectangle(dc,0,0,300,200);ReleaseDC(h,dc);return 0;
    }
    if(m==WM_LBUTTONDOWN && stage==0 && (w&MK_LBUTTON) && x==50 && y==70) next(h,1,"wininput left-down\n");
    if(m==WM_MOUSEMOVE && stage==1 && (w&MK_LBUTTON) && x==90 && y==90) next(h,2,"wininput left-drag\n");
    if(m==WM_LBUTTONUP && stage==2 && !(w&MK_LBUTTON) && x==90 && y==90) next(h,3,"wininput left-up\n");
    if(m==WM_RBUTTONDOWN && stage==3 && (w&MK_RBUTTON)) next(h,4,"wininput right-down\n");
    if(m==WM_RBUTTONUP && stage==4 && !(w&MK_RBUTTON)) next(h,5,"wininput right-up\n");
    if(m==WM_MBUTTONDOWN && stage==5 && (w&MK_MBUTTON)) next(h,6,"wininput middle-down\n");
    if(m==WM_MBUTTONUP && stage==6 && !(w&MK_MBUTTON)) next(h,7,"wininput all-buttons-complete\n");
    if(m==WM_CLOSE) {say(stage==7?"wininput close-complete\n":"wininput premature-close\n");PostQuitMessage(stage==7?0:7);return 0;}
    return DefWindowProcA(h,m,w,l);
}
void __attribute__((noreturn)) start(void) {
    WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.lpszClassName="wininput";RegisterClassA(&wc);
    HWND h=thos_test_client_window(0,"wininput","wininput",0,0,0,300,200,0,0,0,0);ShowWindow(h,1);UpdateWindow(h);
    MSG msg;while(GetMessageA(&msg,0,0,0)){TranslateMessage(&msg);DispatchMessageA(&msg);}ExitProcess((UINT)msg.wParam);
}
