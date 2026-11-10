// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <stdint.h>
static unsigned failures,sets,dispatched;
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do{if(c)say("winwide-ok " n "\n");else{say("winwide-FAIL " n "\n");failures++;}}while(0)
static int equal(const WCHAR*a,const WCHAR*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static LRESULT CALLBACK procw(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_SETTEXT)sets++;
 if(m==WM_PAINT)return 0;
 if(m==WM_APP+23){dispatched++;return (LRESULT)(w^(WPARAM)l);}
 if(m==WM_APP+24)return (LRESULT)(w+(WPARAM)l);
 if(m==WM_CLOSE){PostQuitMessage(failures?1:0);return 0;}
 return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK proca(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_PAINT)return 0;if(m==WM_CLOSE){PostQuitMessage(failures?1:0);return 0;}return DefWindowProcA(h,m,w,l);}
void __attribute__((noreturn)) start(void){
 WNDCLASSEXW wc={0};wc.cbSize=sizeof wc;wc.lpfnWndProc=procw;wc.lpszClassName=L"Fenster.Größe😀";wc.style=CS_HREDRAW|CS_VREDRAW;
 SetLastError(56789);ATOM wide=RegisterClassExW(&wc);CHECK("wide-extended-real-atom",wide&&GetLastError()==56789);
 SetLastError(0);CHECK("duplicate-keeps-original-class",!RegisterClassExW(&wc)&&GetLastError()==1410);
 WNDCLASSW plain={0};plain.lpfnWndProc=procw;plain.lpszClassName=L"PlainWide";ATOM second=RegisterClassW(&plain);CHECK("plain-wide-distinct-atom",second&&second!=wide);
 WNDCLASSEXA ac={0};ac.cbSize=sizeof ac;ac.lpfnWndProc=proca;ac.lpszClassName="ANSI-class";ATOM ansi=RegisterClassExA(&ac);CHECK("ansi-extended-distinct-atom",ansi&&ansi!=wide&&ansi!=second);
 wc.cbSize=1;SetLastError(0);CHECK("bad-class-size",!RegisterClassExW(&wc)&&GetLastError()==87);wc.cbSize=sizeof wc;
 SetLastError(0);CHECK("bad-class-user-pointer",!RegisterClassExW((WNDCLASSEXW*)1)&&GetLastError()==998);
 WNDCLASSW bad=plain;bad.lpszClassName=L"ExtraBytes";bad.cbWndExtra=8;SetLastError(0);CHECK("unsupported-extra-memory",!RegisterClassW(&bad)&&GetLastError()==50);
 WCHAR invalid[]={0xd800,0};bad=plain;bad.lpszClassName=invalid;SetLastError(0);CHECK("invalid-utf16-class",!RegisterClassW(&bad)&&GetLastError()==1113);
 WCHAR longname[258];for(unsigned i=0;i<257;i++)longname[i]=L'x';longname[257]=0;bad=plain;bad.lpszClassName=longname;CHECK("bounded-class-name",!RegisterClassW(&bad));
 RECT r={0,0,400,240};CHECK("same-frame-geometry",AdjustWindowRectEx(&r,WS_OVERLAPPEDWINDOW,FALSE,0));
 HWND h=CreateWindowExW(0,wc.lpszClassName,L"Initial",WS_OVERLAPPEDWINDOW,0,0,r.right-r.left,r.bottom-r.top,0,0,0,0);CHECK("wide-name-window",h!=0&&IsWindowUnicode(h));
 RECT client;CHECK("wide-client-size",GetClientRect(h,&client)&&client.right==400&&client.bottom==240);
 CHECK("wide-text-callback",SetWindowTextW(h,L"Größe 😀")&&sets==1);
 CHECK("wide-title-length-counts-utf16",GetWindowTextLengthW(h)==8);
 WCHAR text[32];for(unsigned i=0;i<32;i++)text[i]=0x5555;
 CHECK("wide-title-roundtrip",GetWindowTextW(h,text,32)==8&&equal(text,L"Größe 😀")&&text[9]==0x5555);
 text[0]=0x5555;CHECK("wide-title-empty-capacity",GetWindowTextW(h,text,0)==0&&text[0]==0x5555);
 CHECK("wide-title-terminator-only",GetWindowTextW(h,text,1)==0&&text[0]==0);
 for(unsigned i=0;i<32;i++)text[i]=0x5555;
 CHECK("wide-title-short-buffer",GetWindowTextW(h,text,7)==6&&equal(text,L"Größe ")&&text[7]==0x5555);
 SetLastError(0);CHECK("bad-title-output-pointer",!GetWindowTextW(h,(WCHAR*)1,32)&&GetLastError()==998);
 SetLastError(0);CHECK("invalid-title-retains-old-text",!SetWindowTextW(h,invalid)&&GetLastError()==1113&&GetWindowTextW(h,text,32)==8&&equal(text,L"Größe 😀"));
 SetLastError(0);CHECK("cross-encoding-callback-explicit-failure",!GetWindowTextA(h,(char*)text,sizeof text)&&GetLastError()==50);
 CHECK("high-alias-is-not-window",!IsWindowUnicode((HWND)((ULONG_PTR)h|(1ULL<<32))));
 HWND byatom=CreateWindowExW(0,(LPCWSTR)MAKEINTATOM(second),L"W",WS_OVERLAPPEDWINDOW,0,0,18,55,0,0,0,0);CHECK("wide-atom-window",byatom&&IsWindowUnicode(byatom));
 HWND ah=CreateWindowExA(0,MAKEINTATOM(ansi),"A",WS_OVERLAPPEDWINDOW,0,0,18,55,0,0,0,0);CHECK("ansi-atom-window",ah&&!IsWindowUnicode(ah));
 SetLastError(0);CHECK("wide-window-from-ansi-class-not-pretended",!CreateWindowExW(0,(LPCWSTR)MAKEINTATOM(ansi),L"bad",WS_OVERLAPPEDWINDOW,0,0,402,279,0,0,0,0)&&GetLastError()==50);
 SetLastError(0);CHECK("class-module-is-not-ignored",!CreateWindowExW(0,wc.lpszClassName,L"bad",WS_OVERLAPPEDWINDOW,0,0,402,279,0,0,(HINSTANCE)0x12345678,0)&&GetLastError()==1407);
 SetLastError(0);CHECK("unknown-atom",!CreateWindowExW(0,(LPCWSTR)MAKEINTATOM(65500),L"bad",WS_OVERLAPPEDWINDOW,0,0,402,279,0,0,0,0)&&GetLastError()==1407);
 SetLastError(0);CHECK("create-validates-title-before-window",!CreateWindowExW(0,wc.lpszClassName,invalid,WS_OVERLAPPEDWINDOW,0,0,402,279,0,0,0,0)&&GetLastError()==1113);
 CHECK("actual-wide-callback-stack-argument",CallWindowProcW(procw,h,WM_APP+24,0x123456789ULL,0x23456789aULL)==(LRESULT)(0x123456789ULL+0x23456789aULL));
 CHECK("post-wide-message",PostMessageW(h,WM_APP+23,0x3456789abULL,0x456789abcULL));
 SetLastError(0);CHECK("getmessage-bad-output-no-consumption",GetMessageW((MSG*)1,0,0,0)==-1&&GetLastError()==998);
 MSG m;SetLastError(0);CHECK("getmessage-filter-explicit-failure",GetMessageW(&m,0,WM_APP,WM_APP)==-1&&GetLastError()==50);
 int found=0;for(unsigned i=0;i<12;i++){if(GetMessageW(&m,0,0,0)!=1)break;if(m.message==WM_APP+23){found=1;break;}DispatchMessageW(&m);}
 CHECK("wide-message-preserves-64bit-parameters",found&&m.hwnd==h&&m.wParam==0x3456789abULL&&m.lParam==(LPARAM)0x456789abcULL);
 CHECK("dispatch-wide-real-procedure",found&&DispatchMessageW(&m)==(LRESULT)(0x3456789abULL^0x456789abcULL)&&dispatched==1);
 HDC dc=GetDC(h);RECT all={0,0,400,240},corner={380,220,400,240};FillRect(dc,&all,CreateSolidBrush(RGB(80,150,220)));FillRect(dc,&corner,CreateSolidBrush(RGB(250,210,30)));CHECK("wide-backing-last-pixel",GetPixel(dc,399,239)==RGB(250,210,30));CHECK("wide-client-clipping",GetPixel(dc,400,239)==CLR_INVALID&&GetPixel(dc,399,240)==CLR_INVALID);ReleaseDC(h,dc);
 ShowWindow(h,1);say(failures?"winwide-result=FAIL\n":"winwide-result=PASS\n");say("winwide-ready-close\n");while(GetMessageW(&m,0,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}ExitProcess(failures?1:0);
}
