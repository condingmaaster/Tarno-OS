// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include "winframe-test.h"
static unsigned failures,checks;
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do{checks++;if(c)say("gdi-blit-ok " n "\n");else{say("gdi-blit-FAIL " n "\n");failures++;}}while(0)
static COLORREF pattern(unsigned x,unsigned y){return RGB((x*17+y*13)&255,(x*3+y*21)&255,(x*9+y*7)&255);}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_PAINT)return 0;if(m==WM_CLOSE){say("gdi-blit-close\n");PostQuitMessage(failures?1:0);return 0;}return DefWindowProcA(h,m,w,l);}
void __attribute__((noreturn)) start(void){
 WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.lpszClassName="gdi-blit";RegisterClassA(&wc);
 HWND wnd=thos_test_client_window(0,"gdi-blit","Windows bitmap copy",WS_OVERLAPPEDWINDOW,0,0,400,240,0,0,0,0);ShowWindow(wnd,1);HDC screen=GetDC(0),window=GetDC(wnd),a=CreateCompatibleDC(screen),b=CreateCompatibleDC(0);
 CHECK("memory-dcs",a&&b&&a!=b);CHECK("invalid-source-dc",!CreateCompatibleDC((HDC)(ULONG_PTR)0x12345));
 CHECK("negative-size",!CreateCompatibleBitmap(screen,-1,8));CHECK("oversize-rejected",!CreateCompatibleBitmap(screen,0x7fffffff,0x7fffffff));
 CHECK("unsupported-monochrome-explicit-failure",!CreateCompatibleBitmap(a,37,41));
 HBITMAP x=CreateCompatibleBitmap(screen,37,41),y=CreateCompatibleBitmap(window,37,41);CHECK("owned-color-bitmaps",x&&y&&x!=y);
 HGDIOBJ olda=SelectObject(a,x),oldb=SelectObject(b,y);CHECK("stock-bitmap-returned",olda==GetStockObject(21)&&oldb==GetStockObject(21));CHECK("bitmap-single-selection",!SelectObject(b,x));CHECK("selected-bitmap-not-deleted",!DeleteObject(x));
 CHECK("bitmap-not-window-brush",!SelectObject(window,x));CHECK("wrong-type-delete-dc",!DeleteDC((HDC)x));
 CHECK("negative-pixel-rejected",SetPixel(a,-1,0,RGB(1,2,3))==CLR_INVALID&&GetPixel(a,-1,0)==CLR_INVALID);
 for(int j=0;j<41;j++)for(int i=0;i<37;i++)SetPixel(a,i,j,pattern(i,j));
 CHECK("mem-mem-copy",BitBlt(b,0,0,37,41,a,0,0,SRCCOPY));int ok=1;for(int j=0;j<41;j++)for(int i=0;i<37;i++)if(GetPixel(b,i,j)!=pattern(i,j))ok=0;CHECK("all-1517-pixels-page-crossing",ok);
 CHECK("overlap-down-right",BitBlt(a,1,1,36,40,a,0,0,SRCCOPY));ok=1;for(int j=1;j<41;j++)for(int i=1;i<37;i++)if(GetPixel(a,i,j)!=pattern(i-1,j-1))ok=0;CHECK("overlap-snapshot-pixels",ok);
 CHECK("mem-window-copy",BitBlt(window,20,10,37,41,b,0,0,SRCCOPY));ok=1;for(int j=0;j<41;j++)for(int i=0;i<37;i++)if(GetPixel(window,20+i,10+j)!=pattern(i,j))ok=0;CHECK("window-page-pixels",ok);
 CHECK("window-window-overlap",BitBlt(window,21,11,36,40,window,20,10,SRCCOPY));ok=1;for(int j=0;j<40;j++)for(int i=0;i<36;i++)if(GetPixel(window,21+i,11+j)!=pattern(i,j))ok=0;CHECK("window-overlap-pixels",ok);
 CHECK("window-memory-readback",BitBlt(a,0,0,36,40,window,21,11,SRCCOPY));ok=1;for(int j=0;j<40;j++)for(int i=0;i<36;i++)if(GetPixel(a,i,j)!=pattern(i,j))ok=0;CHECK("window-readback-pixels",ok);
 SelectObject(a,CreateSolidBrush(RGB(9,8,7)));RECT bg={0,0,37,41};FillRect(a,&bg,CreateSolidBrush(RGB(9,8,7)));CHECK("negative-destination-clip",BitBlt(a,-2,-3,37,41,b,0,0,SRCCOPY));ok=1;for(int j=0;j<41;j++)for(int i=0;i<37;i++){COLORREF want=i<35&&j<38?pattern(i+2,j+3):RGB(9,8,7);if(GetPixel(a,i,j)!=want)ok=0;}CHECK("clipped-pixels-and-untouched-border",ok);
 COLORREF saved=GetPixel(a,0,0);CHECK("invalid-source-not-silently-clipped",!BitBlt(a,0,0,4,4,b,-1,0,SRCCOPY)&&GetPixel(a,0,0)==saved);CHECK("source-adjusted-after-dest-clip",BitBlt(a,-1,0,4,4,b,-1,0,SRCCOPY)&&GetPixel(a,0,0)==pattern(0,0));saved=GetPixel(a,0,0);CHECK("unsupported-rop-fails",!BitBlt(a,0,0,1,1,b,0,0,SRCINVERT));CHECK("failed-rop-no-mutation",GetPixel(a,0,0)==saved);CHECK("unknown-source-fails",!BitBlt(a,0,0,1,1,(HDC)(ULONG_PTR)0x12345,0,0,SRCCOPY));CHECK("unknown-dest-fails",!BitBlt((HDC)(ULONG_PTR)0x12345,0,0,1,1,b,0,0,SRCCOPY));CHECK("negative-size-fails",!BitBlt(a,0,0,-1,1,b,0,0,SRCCOPY));
 CHECK("restore-original-bitmap",SelectObject(a,olda)==x);CHECK("detach-delete",DeleteObject(x));CHECK("stale-bitmap-refused",!SelectObject(a,x)&&!DeleteObject(x));CHECK("delete-memory-dc",DeleteDC(a));CHECK("stale-dc-refused",!DeleteDC(a)&&GetPixel(a,0,0)==CLR_INVALID&&SetPixel(a,0,0,1)==CLR_INVALID);
 CHECK("dc-delete-retains-bitmap",DeleteDC(b)&&DeleteObject(y));
 HDC alt=CreateCompatibleDC(screen);CHECK("deleteobject-memory-dc",DeleteObject((HGDIOBJ)alt)&&!DeleteDC(alt));
 CHECK("zero-area-noop",BitBlt(window,0,0,0,12,window,0,0,SRCCOPY));
 CHECK("extreme-coordinates-noop",BitBlt(window,0x7fffffff,0,0x7fffffff,1,window,0,0,SRCCOPY));
 HDC draw=CreateCompatibleDC(screen);HBITMAP canvas=CreateCompatibleBitmap(screen,400,240);HGDIOBJ old=SelectObject(draw,canvas);SelectObject(draw,CreateSolidBrush(failures?RGB(220,40,50):RGB(46,180,90)));RECT area={0,0,400,240};FillRect(draw,&area,CreateSolidBrush(failures?RGB(220,40,50):RGB(46,180,90)));CHECK("final-window-blit",BitBlt(window,0,0,400,240,draw,0,0,SRCCOPY));CHECK("final-corner-pixels",GetPixel(window,0,0)==RGB(46,180,90)&&GetPixel(window,399,239)==RGB(46,180,90));SelectObject(draw,old);DeleteObject(canvas);DeleteDC(draw);
 say(failures?"gdi-blit-result=FAIL\n":"gdi-blit-result=PASS\n");say("gdi-blit-ready-close\n");MSG msg;while(GetMessageA(&msg,0,0,0)){TranslateMessage(&msg);DispatchMessageA(&msg);}ExitProcess(failures?1:0);
}
