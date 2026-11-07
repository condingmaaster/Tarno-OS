// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include "winframe-test.h"
static unsigned failures;
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do{if(c)say("gdi-alpha-ok " n "\n");else{say("gdi-alpha-FAIL " n "\n");failures++;}}while(0)
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_PAINT)return 0;if(m==WM_CLOSE){say("gdi-alpha-close\n");PostQuitMessage(failures?1:0);return 0;}return DefWindowProcA(h,m,w,l);}
static void solid(HDC dc,COLORREF c,int w,int h){RECT r={0,0,w,h};FillRect(dc,&r,CreateSolidBrush(c));}
void __attribute__((noreturn)) start(void){
 WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.lpszClassName="gdi-alpha";RegisterClassA(&wc);HWND wnd=thos_test_client_window(0,"gdi-alpha","Windows alpha blend",WS_OVERLAPPEDWINDOW,0,0,400,240,0,0,0,0);ShowWindow(wnd,1);
 HDC window=GetDC(wnd),a=CreateCompatibleDC(window),b=CreateCompatibleDC(window);HBITMAP x=CreateCompatibleBitmap(window,400,240),y=CreateCompatibleBitmap(window,400,240);HGDIOBJ oldx=SelectObject(a,x),oldy=SelectObject(b,y);CHECK("real-memory-dcs",a&&b&&x&&y&&oldx&&oldy);
 BLENDFUNCTION f={AC_SRC_OVER,0,128,0};solid(a,RGB(0,0,255),400,240);solid(b,RGB(255,0,0),400,240);
 CHECK("msimg32-entry-blends",AlphaBlend(b,0,0,37,41,a,0,0,37,41,f));int ok=1;for(int j=0;j<41;j++)for(int i=0;i<37;i++)if(GetPixel(b,i,j)!=RGB(127,0,128))ok=0;CHECK("all-page-crossing-half-alpha-pixels",ok);CHECK("outside-region-kept",GetPixel(b,37,0)==RGB(255,0,0)&&GetPixel(b,0,41)==RGB(255,0,0));
 f.SourceConstantAlpha=0;solid(b,RGB(25,60,90),400,240);CHECK("transparent-noop",AlphaBlend(b,0,0,37,41,a,0,0,37,41,f)&&GetPixel(b,0,0)==RGB(25,60,90));
 f.SourceConstantAlpha=255;CHECK("opaque-copy",AlphaBlend(b,0,0,37,41,a,0,0,37,41,f)&&GetPixel(b,36,40)==RGB(0,0,255));
 f.SourceConstantAlpha=64;solid(b,RGB(255,255,255),400,240);CHECK("quarter-alpha-gdi-entry",GdiAlphaBlend(b,0,0,37,41,a,0,0,37,41,f)&&GetPixel(b,36,40)==RGB(191,191,255));
 f.SourceConstantAlpha=128;solid(b,RGB(255,0,0),400,240);CHECK("destination-negative-clipping",AlphaBlend(b,-2,-3,37,41,a,0,0,37,41,f)&&GetPixel(b,34,37)==RGB(127,0,128)&&GetPixel(b,35,37)==RGB(255,0,0));
 COLORREF saved=GetPixel(b,0,0);f.AlphaFormat=AC_SRC_ALPHA;CHECK("unsupported-per-pixel-explicit-fail",!AlphaBlend(b,0,0,1,1,a,0,0,1,1,f));f.AlphaFormat=0;f.BlendFlags=1;CHECK("invalid-flags-fail",!AlphaBlend(b,0,0,1,1,a,0,0,1,1,f));f.BlendFlags=0;f.BlendOp=1;CHECK("invalid-op-fail",!AlphaBlend(b,0,0,1,1,a,0,0,1,1,f));f.BlendOp=0;
 CHECK("unsupported-stretch-explicit-fail",!AlphaBlend(b,0,0,2,2,a,0,0,1,1,f));CHECK("negative-size-fail",!AlphaBlend(b,0,0,-1,1,a,0,0,-1,1,f));CHECK("out-of-bounds-source-fail",!AlphaBlend(b,0,0,37,41,a,399,0,37,41,f));CHECK("negative-source-fail",!AlphaBlend(b,0,0,1,1,a,-1,0,1,1,f));CHECK("invalid-handle-fail",!AlphaBlend(b,0,0,1,1,(HDC)(ULONG_PTR)0x12345,0,0,1,1,f));CHECK("failed-calls-no-mutation",GetPixel(b,0,0)==saved);
 CHECK("same-surface-overlap-fail",!AlphaBlend(a,1,1,37,41,a,0,0,37,41,f));f.SourceConstantAlpha=0;CHECK("overlap-invalid-even-transparent",!AlphaBlend(a,1,1,37,41,a,0,0,37,41,f));f.SourceConstantAlpha=128;CHECK("same-surface-disjoint-valid",AlphaBlend(a,100,100,37,41,a,0,0,37,41,f));
 solid(window,RGB(255,0,0),400,240);CHECK("real-window-alpha-output",AlphaBlend(window,0,0,400,240,a,0,0,400,240,f));CHECK("window-corner-color",GetPixel(window,0,0)==RGB(127,0,128)&&GetPixel(window,399,239)==RGB(127,0,128));
 SelectObject(a,oldx);SelectObject(b,oldy);DeleteObject(x);DeleteObject(y);DeleteDC(a);DeleteDC(b);
 say(failures?"gdi-alpha-result=FAIL\n":"gdi-alpha-result=PASS\n");say("gdi-alpha-ready-close\n");MSG msg;while(GetMessageA(&msg,0,0,0)){TranslateMessage(&msg);DispatchMessageA(&msg);}ExitProcess(failures?1:0);
}
