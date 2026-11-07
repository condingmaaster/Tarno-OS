// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include "winframe-test.h"
static HWND first,second;static unsigned lost_first,lost_second,gained_first,gained_second,stage,lost_native,gained_native;
static void say(const char*s){DWORD n;unsigned len=0;while(s[len])len++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,len,&n,0);}
static void paint(HWND h){HDC dc=GetDC(h);SelectObject(dc,CreateSolidBrush(stage>=5?RGB(46,180,90):RGB(30,160,200)));Rectangle(dc,0,0,400,240);ReleaseDC(h,dc);}
static void progress(void){
 if(stage==0&&lost_second>=1&&gained_first>=1){stage=1;say("winfocus-step:1\n");}
 if(stage==1&&lost_first>=1&&gained_second>=1){stage=2;say("winfocus-step:2\n");}
 if(stage==2&&lost_second>=2&&gained_first>=2){stage=3;say("winfocus-step:3\n");}
 if(stage==3&&lost_native){stage=4;say("winfocus-step:4\n");}
 if(stage==4&&gained_native){stage=5;paint(first);say("winfocus-step:5\n");}
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_PAINT){paint(h);return 0;}
 if(m==WM_SETFOCUS){if(h==first&&!w&&stage==4)gained_native=1;if(h==first&&(HWND)w==second)gained_first++;if(h==second&&(HWND)w==first)gained_second++;progress();return 0;}
 if(m==WM_KILLFOCUS){if(h==first&&!w&&stage==3)lost_native=1;if(h==first&&(HWND)w==second)lost_first++;if(h==second&&(HWND)w==first)lost_second++;progress();return 0;}
 if(m==WM_CLOSE){say("winfocus-closed\n");PostQuitMessage(0);return 0;}
 return DefWindowProcA(h,m,w,l);
}
void __attribute__((noreturn)) start(void){WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.lpszClassName="winfocus";RegisterClassA(&wc);first=thos_test_client_window(0,"winfocus","Focus one",WS_OVERLAPPEDWINDOW,0,0,400,240,0,0,0,0);ShowWindow(first,1);UpdateWindow(first);second=thos_test_client_window(0,"winfocus","Focus two",WS_OVERLAPPEDWINDOW,0,0,400,240,0,0,0,0);ShowWindow(second,1);UpdateWindow(second);say("winfocus-ready\n");MSG msg;while(GetMessageA(&msg,0,0,0)){TranslateMessage(&msg);DispatchMessageA(&msg);}ExitProcess((UINT)msg.wParam);}
