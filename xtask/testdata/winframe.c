// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <limits.h>
static unsigned failures;
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do{if(c)say("winframe-ok " n "\n");else{say("winframe-FAIL " n "\n");failures++;}}while(0)
static int same(RECT a,RECT b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_PAINT)return 0;if(m==WM_CLOSE){PostQuitMessage(failures?1:0);return 0;}return DefWindowProcA(h,m,w,l);}
void __attribute__((noreturn)) start(void){
 DWORD style=WS_OVERLAPPEDWINDOW;RECT r={0,0,400,240};SetLastError(56789);
 CHECK("adjust-success-preserves-error",AdjustWindowRect(&r,style,FALSE)&&GetLastError()==56789);
 CHECK("actual-thos-frame",r.left==-1&&r.top==-38&&r.right==401&&r.bottom==241);
 CHECK("system-metrics-frame",GetSystemMetrics(SM_CXFRAME)==1&&GetSystemMetrics(SM_CYFRAME)==1&&GetSystemMetrics(SM_CYCAPTION)==37);
 RECT shifted={-11,-17,389,223};CHECK("translated-client",AdjustWindowRectEx(&shifted,style,FALSE,0)&&shifted.left==-12&&shifted.top==-55&&shifted.right==390&&shifted.bottom==224);
 RECT z={0,0,0,0};CHECK("empty-client-rectangle",AdjustWindowRect(&z,style,FALSE)&&z.left==-1&&z.top==-38&&z.right==1&&z.bottom==1);
 SetLastError(0);CHECK("null-rect",!AdjustWindowRect(0,style,0)&&GetLastError()==998);
 SetLastError(0);CHECK("bad-user-rect",!AdjustWindowRect((RECT*)1,style,0)&&GetLastError()==998);
 RECT input={1,2,3,4},v=input;SetLastError(0);CHECK("menu-explicitly-unsupported",!AdjustWindowRect(&v,style,TRUE)&&GetLastError()==50&&same(v,input));
 v=input;SetLastError(0);CHECK("extended-frame-explicitly-unsupported",!AdjustWindowRectEx(&v,style,FALSE,WS_EX_CLIENTEDGE)&&GetLastError()==50&&same(v,input));
 v=input;SetLastError(0);CHECK("child-explicitly-unsupported",!AdjustWindowRect(&v,WS_CHILD,FALSE)&&GetLastError()==50&&same(v,input));
 v=input;SetLastError(0);CHECK("popup-explicitly-unsupported",!AdjustWindowRect(&v,WS_POPUP,FALSE)&&GetLastError()==50&&same(v,input));
 RECT extreme={LONG_MIN,0,0,0};v=extreme;SetLastError(0);CHECK("underflow-no-write",!AdjustWindowRect(&v,style,FALSE)&&GetLastError()==87&&same(v,extreme));
 extreme=(RECT){0,0,LONG_MAX,0};v=extreme;SetLastError(0);CHECK("overflow-no-write",!AdjustWindowRect(&v,style,FALSE)&&GetLastError()==87&&same(v,extreme));
 extreme=(RECT){3,4,2,5};v=extreme;SetLastError(0);CHECK("inverted-no-write",!AdjustWindowRect(&v,style,FALSE)&&GetLastError()==87&&same(v,extreme));
 WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.lpszClassName="winframe";CHECK("class-registered",RegisterClassA(&wc));
 HWND h=CreateWindowExA(0,"winframe","Windows client geometry",style,0,0,r.right-r.left,r.bottom-r.top,0,0,0,0);
 CHECK("adjusted-create",h!=0);RECT client={-1,-1,-1,-1};CHECK("exact-client-size",GetClientRect(h,&client)&&client.left==0&&client.top==0&&client.right==400&&client.bottom==240);
 HDC dc=GetDC(h);RECT all={0,0,400,240};CHECK("full-client-fill",FillRect(dc,&all,CreateSolidBrush(RGB(18,150,180))));
 RECT corner={380,220,400,240};CHECK("bottom-right-client-fill",FillRect(dc,&corner,CreateSolidBrush(RGB(250,220,40)))&&GetPixel(dc,399,239)==RGB(250,220,40));
 CHECK("client-clipping",GetPixel(dc,400,239)==CLR_INVALID&&GetPixel(dc,399,240)==CLR_INVALID);ReleaseDC(h,dc);
 CHECK("outer-too-small-rejected",!CreateWindowExA(0,"winframe","bad",style,0,0,2,39,0,0,0,0));
 CHECK("oversize-rejected",!CreateWindowExA(0,"winframe","bad",style,0,0,INT_MAX,INT_MAX,0,0,0,0));
 CHECK("parent-not-silently-ignored",!CreateWindowExA(0,"winframe","bad",style,0,0,402,279,h,0,0,0));
 CHECK("menu-not-silently-ignored",!CreateWindowExA(0,"winframe","bad",style,0,0,402,279,0,(HMENU)1,0,0));
 ShowWindow(h,1);say(failures?"winframe-result=FAIL\n":"winframe-result=PASS\n");say("winframe-ready-close\n");MSG m;while(GetMessageA(&m,0,0,0)){TranslateMessage(&m);DispatchMessageA(&m);}ExitProcess(failures?1:0);
}
