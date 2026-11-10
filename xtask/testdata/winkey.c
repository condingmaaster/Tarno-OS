// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include "winframe-test.h"
static unsigned downs,ups,chars,complete;
static void say(const char*s){DWORD n;unsigned len=0;while(s[len])len++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,len,&n,0);}
static void check(int condition){if(!condition){say("winkey-FAIL message-or-flags\n");ExitProcess(50);}}
static void paint(HWND h){HDC dc=GetDC(h);SelectObject(dc,CreateSolidBrush(complete?RGB(46,180,90):RGB(30,160,200)));Rectangle(dc,0,0,400,240);ReleaseDC(h,dc);}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_PAINT){paint(h);return 0;}
 if(m==WM_KEYDOWN||m==WM_KEYUP){
  if(complete&&m==WM_KEYDOWN&&w==VK_RETURN){say("winkey-close-complete\n");PostQuitMessage(0);return 0;}
  unsigned scan=0,extended=0;
  switch(w){case VK_LEFT:scan=0x4b;extended=1;break;case VK_SHIFT:scan=0x2a;break;case 'A':scan=0x1e;break;case VK_CONTROL:scan=0x1d;break;case 'B':scan=0x30;break;case 'Z':scan=0x15;break;default:check(0);}
  unsigned lp=(unsigned)l;check((lp&0xffff)==1&&((lp>>16)&255)==scan&&((lp>>24)&1)==extended&&!(lp&0x3e000000u));
  if(m==WM_KEYDOWN){check(!(lp&0xc0000000u));downs++;}
  else {check((lp&0xc0000000u)==0xc0000000u);ups++;
   if(w==VK_LEFT){check(downs==1&&ups==1&&chars==0);say("winkey-left-up\n");}
   if(w==VK_SHIFT){check(downs==3&&ups==3&&chars==1);say("winkey-shift-up\n");}
   if(w==VK_CONTROL){check(downs==5&&ups==5&&chars==2);say("winkey-control-up\n");}
   if(w=='Z'){check(downs==6&&ups==6&&chars==3);complete=1;paint(h);say("winkey-ready-close\n");}
  }return 0;
 }
 if(m==WM_CHAR&&complete){check(w==VK_RETURN);say("winkey-return-char-ok\n");return 0;} // already queued closing Enter
 if(m==WM_CHAR){unsigned expected=chars==0?'A':chars==1?2:'z';check(w==expected&&chars<3);check(((unsigned)l&0xffff)==1);chars++;return 0;}
 if(m==WM_CLOSE){say("winkey-closed\n");PostQuitMessage(0);return 0;}
 return DefWindowProcA(h,m,w,l);
}
void __attribute__((noreturn)) start(void){WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.lpszClassName="winkey";RegisterClassA(&wc);HWND h=thos_test_client_window(0,"winkey","Windows keyboard",WS_OVERLAPPEDWINDOW,0,0,400,240,0,0,0,0);check(h!=0);ShowWindow(h,1);UpdateWindow(h);say("winkey-ready\n");MSG msg;while(GetMessageA(&msg,0,0,0)){TranslateMessage(&msg);DispatchMessageA(&msg);}ExitProcess((UINT)msg.wParam);}
