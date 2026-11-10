// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include "winframe-test.h"
static unsigned failures;
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do {if(c)say("gdi-brush-ok " n "\n");else{say("gdi-brush-FAIL " n "\n");failures++;}}while(0)
static void paint(HWND h){HDC dc=GetDC(h);SelectObject(dc,CreateSolidBrush(failures?RGB(220,40,50):RGB(46,180,90)));Rectangle(dc,0,0,400,240);ReleaseDC(h,dc);}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_PAINT){paint(h);return 0;}if(m==WM_CLOSE){say("gdi-brush-close\n");PostQuitMessage(failures?1:0);return 0;}return DefWindowProcA(h,m,w,l);}
void __attribute__((noreturn)) start(void){
 WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.lpszClassName="gdi-brush";RegisterClassA(&wc);
 HWND a=thos_test_client_window(0,"gdi-brush","GDI first",WS_OVERLAPPEDWINDOW,0,0,400,240,0,0,0,0),b=thos_test_client_window(0,"gdi-brush","GDI second",WS_OVERLAPPEDWINDOW,0,0,400,240,0,0,0,0);
 ShowWindow(a,1);ShowWindow(b,1);HDC da=GetDC(a),db=GetDC(b),screen=GetDC(0),bad=(HDC)(ULONG_PTR)0x12345,stale=(HDC)(ULONG_PTR)0x80010000;
 COLORREF red=RGB(220,40,50),blue=RGB(40,60,210),test=RGB(99,22,177);HBRUSH r=CreateSolidBrush(red),bl=CreateSolidBrush(blue);
 CHECK("default-first",SelectObject(da,r)==GetStockObject(WHITE_BRUSH));CHECK("default-second",SelectObject(db,bl)==GetStockObject(WHITE_BRUSH));
 CHECK("valid-first-rectangle",Rectangle(da,0,0,40,40));CHECK("valid-second-rectangle",Rectangle(db,0,0,40,40));
 CHECK("isolated-first",GetPixel(da,10,10)==red);CHECK("isolated-second",GetPixel(db,10,10)==blue);
 CHECK("null-brush-refused",SelectObject(da,0)==0);Rectangle(da,0,0,40,40);CHECK("null-brush-keeps-color",GetPixel(da,10,10)==red);
 CHECK("unknown-dc-select",SelectObject(bad,r)==0);CHECK("stale-dc-select",SelectObject(stale,r)==0);CHECK("invalid-window-getdc",GetDC((HWND)(ULONG_PTR)0x10000)==0);
 HDC targets[3]={bad,stale,0};
 for(unsigned i=0;i<3;i++){
  COLORREF saved=GetPixel(screen,1000,700);CHECK("invalid-dc-read",GetPixel(targets[i],1000,700)==CLR_INVALID);
  CHECK("invalid-dc-write",SetPixel(targets[i],1000,700,test)==CLR_INVALID);CHECK("desktop-pixel-preserved",GetPixel(screen,1000,700)==saved);SetPixel(screen,1000,700,saved);
  saved=GetPixel(screen,1002,700);CHECK("invalid-dc-rectangle",!Rectangle(targets[i],1002,700,1003,701));CHECK("desktop-rectangle-preserved",GetPixel(screen,1002,700)==saved);SetPixel(screen,1002,700,saved);
 }
 HDC high=(HDC)((ULONG_PTR)da|((ULONG_PTR)1<<32));COLORREF saved=GetPixel(da,10,10);
 CHECK("high-alias-read",GetPixel(high,10,10)==CLR_INVALID);CHECK("high-alias-write",SetPixel(high,10,10,test)==CLR_INVALID);CHECK("valid-window-preserved",GetPixel(da,10,10)==saved);SetPixel(da,10,10,saved);
 paint(a);paint(b);say(failures?"gdi-brush-result=FAIL\n":"gdi-brush-result=PASS\n");say("gdi-brush-ready-close\n");MSG msg;while(GetMessageA(&msg,0,0,0)){TranslateMessage(&msg);DispatchMessageA(&msg);}ExitProcess((UINT)msg.wParam);
}
