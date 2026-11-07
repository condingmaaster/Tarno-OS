// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include "winframe-test.h"
static unsigned failures;
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do {if(c)say("gdi-dcbrush-ok " n "\n");else{say("gdi-dcbrush-FAIL " n "\n");failures++;}}while(0)
static void paint(HWND h){HDC dc=GetDC(h);SelectObject(dc,CreateSolidBrush(failures?RGB(220,40,50):RGB(46,180,90)));Rectangle(dc,0,0,400,240);ReleaseDC(h,dc);}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_PAINT){paint(h);return 0;}if(m==WM_CLOSE){say("gdi-dcbrush-close\n");PostQuitMessage(failures?1:0);return 0;}return DefWindowProcA(h,m,w,l);}
void __attribute__((noreturn)) start(void){
 WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.lpszClassName="gdi-dcbrush";RegisterClassA(&wc);
 HWND a=thos_test_client_window(0,"gdi-dcbrush","GDI first",WS_OVERLAPPEDWINDOW,0,0,400,240,0,0,0,0),b=thos_test_client_window(0,"gdi-dcbrush","GDI second",WS_OVERLAPPEDWINDOW,0,0,400,240,0,0,0,0);
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

 HGDIOBJ dcbrush=GetStockObject(DC_BRUSH);CHECK("dc-brush-distinct-token",dcbrush&&dcbrush!=r&&dcbrush!=bl);
 CHECK("stock-gray-tones",GetStockObject(LTGRAY_BRUSH)==CreateSolidBrush(RGB(192,192,192))&&GetStockObject(GRAY_BRUSH)==CreateSolidBrush(RGB(128,128,128))&&GetStockObject(DKGRAY_BRUSH)==CreateSolidBrush(RGB(64,64,64)));
 CHECK("unsupported-stock-not-white-brush",GetStockObject(999)==0);
 CHECK("initial-dc-color-white",GetDCBrushColor(da)==RGB(255,255,255)&&GetDCBrushColor(db)==RGB(255,255,255));
 CHECK("select-dc-brush-returns-solid",SelectObject(da,dcbrush)==r);
 CHECK("set-dc-color-returns-old",SetDCBrushColor(da,red)==RGB(255,255,255));Rectangle(da,0,0,40,40);CHECK("dc-brush-rasterized",GetPixel(da,10,10)==red);
 CHECK("solid-selection-returns-dc-token",SelectObject(da,bl)==dcbrush);
 COLORREF green=RGB(25,190,70);CHECK("dc-color-persists-independent-of-selection",SetDCBrushColor(da,green)==red&&GetDCBrushColor(da)==green);Rectangle(da,0,0,40,40);CHECK("solid-not-recolored",GetPixel(da,10,10)==blue);
 CHECK("reselect-dc-preserves-color",SelectObject(da,dcbrush)==bl);Rectangle(da,0,0,40,40);CHECK("dc-new-color-used",GetPixel(da,10,10)==green);CHECK("other-window-color-independent",GetDCBrushColor(db)==RGB(255,255,255));
 RECT rect={2,3,18,19};CHECK("fillrect-explicit-solid",FillRect(da,&rect,r));CHECK("fillrect-excludes-right-bottom",GetPixel(da,17,18)==red&&GetPixel(da,18,18)==green&&GetPixel(da,17,19)==green);
 CHECK("fillrect-preserves-selection",SelectObject(da,dcbrush)==dcbrush);CHECK("fillrect-preserves-dc-color",GetDCBrushColor(da)==green);
 RECT clipped={-3,-4,5,6};CHECK("fillrect-negative-clipped",FillRect(da,&clipped,bl)&&GetPixel(da,0,0)==blue&&GetPixel(da,4,5)==blue&&GetPixel(da,5,5)==red);
 CHECK("fillrect-explicit-dc-brush",FillRect(da,&rect,(HBRUSH)dcbrush)&&GetPixel(da,10,10)==green);
 saved=GetPixel(da,10,10);CHECK("invalid-brush-no-paint",!FillRect(da,&rect,(HBRUSH)(ULONG_PTR)0x12345)&&GetPixel(da,10,10)==saved);
 CHECK("invalid-dc-brush-color",GetDCBrushColor(bad)==CLR_INVALID&&SetDCBrushColor(bad,red)==CLR_INVALID);CHECK("bad-pointer-fillrect",!FillRect(da,(RECT*)(ULONG_PTR)0x1000,r));
 HDC mem=CreateCompatibleDC(da);HBITMAP bitmap=CreateCompatibleBitmap(da,32,32);HGDIOBJ oldbm=SelectObject(mem,bitmap);
 CHECK("memory-default-dc-color",GetDCBrushColor(mem)==RGB(255,255,255));CHECK("memory-dc-color-independent",SetDCBrushColor(mem,blue)==RGB(255,255,255)&&GetDCBrushColor(da)==green);
 CHECK("memory-explicit-dc-brush",FillRect(mem,&rect,(HBRUSH)dcbrush)&&GetPixel(mem,10,10)==blue);
 Rectangle(mem,0,0,32,32);CHECK("memory-fill-preserves-selected-white",GetPixel(mem,10,10)==RGB(255,255,255));SelectObject(mem,dcbrush);Rectangle(mem,0,0,32,32);CHECK("memory-selected-dc-color",GetPixel(mem,15,15)==blue);CHECK("rectangle-default-pen-black",GetPixel(mem,31,31)==RGB(0,0,0));
 SelectObject(mem,oldbm);DeleteObject(bitmap);DeleteDC(mem);
 COLORREF screenold=GetDCBrushColor(screen);CHECK("screen-color-independent",SetDCBrushColor(screen,red)==screenold&&GetDCBrushColor(da)==green);SetDCBrushColor(screen,screenold);
 paint(a);paint(b);say(failures?"gdi-dcbrush-result=FAIL\n":"gdi-dcbrush-result=PASS\n");say("gdi-dcbrush-ready-close\n");MSG msg;while(GetMessageA(&msg,0,0,0)){TranslateMessage(&msg);DispatchMessageA(&msg);}ExitProcess((UINT)msg.wParam);
}
