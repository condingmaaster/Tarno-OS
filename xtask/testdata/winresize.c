// SPDX-License-Identifier: GPL-2.0-or-later
// An ordinary MinGW PE client lays out and paints its entire actual client area.
#include <windows.h>
#include "winframe-test.h"
static unsigned state;
static unsigned sets;
static void say(const char *s){DWORD n;unsigned len=0;while(s[len])len++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,len,&n,0);}
static char *number(char *out,unsigned n){char b[16];unsigned i=0;do{b[i++]=(char)('0'+n%10);n/=10;}while(n);while(i)*out++=b[--i];return out;}
static void painted(unsigned width,unsigned height){
    char line[96],*p=line;const char *s="winresize painted ";while(*s)*p++=*s++;
    p=number(p,width);*p++='x';p=number(p,height);s=" state";while(*s)*p++=*s++;
    p=number(p,state);*p++='\n';*p=0;
    /* One write prevents unrelated Controlbar logging from splitting the
       synchronization record; dimensions and pixel assertions are unchanged. */
    say(line);
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_SETTEXT)sets++;
    if(m==WM_CREATE){
        char text[32];const unsigned char expected[]={'G','r',0xf6,0xdf,'e',0};
        if(!SetWindowTextA(h,"Gr\xf6\xdf""e")||GetWindowTextLengthA(h)!=5||GetWindowTextA(h,text,32)!=5)ExitProcess(10);
        for(unsigned i=0;i<6;i++)if((unsigned char)text[i]!=expected[i])ExitProcess(11);
        if(!SetWindowTextA(h,"Windows sizing")||GetWindowTextLengthA(h)!=14||GetWindowTextA(h,text,8)!=7||text[7]||sets!=2)ExitProcess(12);
        if(GetWindowTextA(h,text,1)!=0||text[0])ExitProcess(13);
        say("winresize title-api-ok\n");return 0;
    }
    if(m==WM_SIZE){
        RECT r;if(!GetClientRect(h,&r)||r.left||r.top||r.right!=(int)(l&0xffff)||r.bottom!=(int)((l>>16)&0xffff)){say("winresize size-mismatch\n");ExitProcess(7);}
        state=(unsigned)w;return 0;
    }
    if(m==WM_PAINT){
        RECT r;if(!GetClientRect(h,&r)){ExitProcess(8);}
        HDC dc=GetDC(h);COLORREF color=(r.right==400&&r.bottom==240)?RGB(30,160,200):(r.right==480&&r.bottom==300)?RGB(80,180,90):RGB(140,80,210);
        SelectObject(dc,CreateSolidBrush(color));Rectangle(dc,0,0,r.right,r.bottom);
        SelectObject(dc,CreateSolidBrush(RGB(250,220,40)));Rectangle(dc,r.right-20,r.bottom-20,r.right,r.bottom);ReleaseDC(h,dc);
        painted(r.right,r.bottom);return 0;
    }
    if(m==WM_CLOSE){say("winresize close\n");PostQuitMessage(0);return 0;}
    return DefWindowProcA(h,m,w,l);
}
void __attribute__((noreturn)) start(void){
    WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.lpszClassName="winresize";RegisterClassA(&wc);
    HWND h=thos_test_client_window(0,"winresize","Win32 sizing",WS_OVERLAPPEDWINDOW,0,0,400,240,0,0,0,0);
    RECT r={1,2,3,4};if(!h||GetClientRect((HWND)0xdead,&r)||r.left!=1||GetClientRect(h,(RECT*)1)){say("winresize invalid-rect-accepted\n");ExitProcess(9);}
    ShowWindow(h,1);UpdateWindow(h);MSG msg;while(GetMessageA(&msg,0,0,0)){TranslateMessage(&msg);DispatchMessageA(&msg);}ExitProcess((UINT)msg.wParam);
}
