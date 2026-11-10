// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include "winframe-test.h"
static unsigned failures;
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do{if(c)say("gdi-font-ok " n "\n");else{say("gdi-font-FAIL " n "\n");failures++;}}while(0)
static unsigned length(const WCHAR*s){unsigned n=0;while(s[n])n++;return n;}
static HFONT font(int px,int weight){return CreateFontW(-px,0,0,0,weight,0,0,0,DEFAULT_CHARSET,OUT_TT_PRECIS,0,CLEARTYPE_QUALITY,0,L"Segoe UI");}
static void text(HDC d,int x,int y,const WCHAR*s){TextOutW(d,x,y,s,length(s));}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_PAINT)return 0;if(m==WM_CLOSE){say("gdi-font-close\n");PostQuitMessage(failures?1:0);return 0;}return DefWindowProcA(h,m,w,l);}
void __attribute__((noreturn)) start(void){
 WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.lpszClassName="gdi-font";RegisterClassA(&wc);HWND wnd=thos_test_client_window(0,"gdi-font","Windows antialiased text",WS_OVERLAPPEDWINDOW,0,0,640,360,0,0,0,0);ShowWindow(wnd,1);
 HDC window=GetDC(wnd),mem=CreateCompatibleDC(window);HBITMAP bitmap=CreateCompatibleBitmap(window,160,90);HGDIOBJ oldbitmap=SelectObject(mem,bitmap);
 HFONT regular=font(16,FW_NORMAL),bold=font(32,FW_BOLD),ansi=CreateFontA(-16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,0,0,"Arial");CHECK("real-font-objects",regular&&bold&&ansi&&regular!=bold);
 HGDIOBJ original=SelectObject(mem,regular);CHECK("default-font-selection",original==GetStockObject(SYSTEM_FONT));CHECK("unsupported-monospace-not-proportional-fake",GetStockObject(SYSTEM_FIXED_FONT)==0&&GetStockObject(ANSI_FIXED_FONT)==0);CHECK("font-current-object",GetCurrentObject(mem,OBJ_FONT)==regular);CHECK("bitmap-selection-independent",GetCurrentObject(mem,OBJ_BITMAP)==bitmap);
 CHECK("selected-font-cannot-delete",!DeleteObject(regular));CHECK("unknown-font-cannot-select",!SelectObject(mem,(HGDIOBJ)(ULONG_PTR)0x40001234567890abULL));
 TEXTMETRICW tm;CHECK("actual-text-metrics",GetTextMetricsW(mem,&tm)&&tm.tmHeight==tm.tmAscent+tm.tmDescent&&tm.tmHeight>8&&tm.tmWeight==400&&(tm.tmPitchAndFamily&TMPF_FIXED_PITCH));
 WCHAR name[16]={0};CHECK("actual-fallback-face-query",GetTextFaceW(mem,0,0)==8);CHECK("actual-fallback-face-name",GetTextFaceW(mem,16,name)>0&&name[0]=='T'&&name[1]=='H'&&name[4]==' '&&name[5]=='U'&&name[6]=='I'&&name[7]==0);
 WCHAR small[5]={1,1,1,1,0x1234};CHECK("face-name-truncation-bounded",GetTextFaceW(mem,4,small)>0&&small[0]=='T'&&small[2]=='O'&&small[3]==0&&small[4]==0x1234);
 SIZE a,b,empty={123,456};CHECK("empty-extent-zero",GetTextExtentPoint32W(mem,0,0,&empty)&&empty.cx==0&&empty.cy==0);
 CHECK("measured-positive-extent",GetTextExtentPoint32W(mem,L"THOS",4,&a)&&a.cx>16&&a.cy==tm.tmHeight);
 CHECK("latin-euro-extent",GetTextExtentPoint32W(mem,L"ä€",2,&a)&&a.cx>0&&a.cy==tm.tmHeight);
 WCHAR emoji[2]={0xd83d,0xde00},broken[1]={0xd800};CHECK("surrogate-pair-one-fallback",GetTextExtentPoint32W(mem,emoji,2,&a)&&GetTextExtentPoint32W(mem,L"?",1,&b)&&a.cx==b.cx&&a.cy==b.cy);CHECK("invalid-surrogate-safe-fallback",GetTextExtentPoint32W(mem,broken,1,&a)&&a.cx==b.cx);
 CHECK("text-color-default-and-set",GetTextColor(mem)==RGB(0,0,0)&&SetTextColor(mem,RGB(255,255,255))==RGB(0,0,0)&&GetTextColor(mem)==RGB(255,255,255));CHECK("bk-color-default-and-set",GetBkColor(mem)==RGB(255,255,255)&&SetBkColor(mem,RGB(20,30,40))==RGB(255,255,255));CHECK("mode-default-opaque",GetBkMode(mem)==OPAQUE);
 SelectObject(mem,CreateSolidBrush(RGB(64,80,96)));Rectangle(mem,0,0,160,90);CHECK("opaque-text-render",TextOutW(mem,4,4,L"A",1));CHECK("opaque-cell-background-and-border",GetPixel(mem,4,4)==RGB(20,30,40)&&GetPixel(mem,3,4)==RGB(64,80,96));
 CHECK("transparent-mode-change",SetBkMode(mem,TRANSPARENT)==OPAQUE&&GetBkMode(mem)==TRANSPARENT);CHECK("transparent-space-preserves-pixel",TextOutW(mem,30,4,L" ",1)&&GetPixel(mem,30,4)==RGB(64,80,96));CHECK("invalid-mode-preserves-state",!SetBkMode(mem,99)&&GetBkMode(mem)==TRANSPARENT);
 CHECK("negative-origin-clips",TextOutW(mem,-4,-3,L"THOS",4));CHECK("invalid-input-pointer",!TextOutW(mem,0,0,(WCHAR*)(ULONG_PTR)0x1000,1));CHECK("invalid-count",!TextOutW(mem,0,0,L"A",-1));CHECK("invalid-metrics-pointer",!GetTextMetricsW(mem,(TEXTMETRICW*)(ULONG_PTR)0x1000));CHECK("invalid-dc-no-text",!TextOutW((HDC)(ULONG_PTR)0x12345,0,0,L"A",1));
 CHECK("unsupported-width-explicit-fail",!CreateFontW(-16,12,0,0,400,0,0,0,1,0,0,0,0,0));CHECK("unsupported-rotation-explicit-fail",!CreateFontW(-16,0,900,0,400,0,0,0,1,0,0,0,0,0));
 CHECK("window-state-independent",GetTextColor(window)==RGB(0,0,0)&&GetBkMode(window)==OPAQUE);
 HGDIOBJ winoriginal=SelectObject(window,regular);CHECK("font-selection-multiple-dcs-valid",winoriginal==GetStockObject(SYSTEM_FONT));CHECK("window-selected-font-not-delete",!DeleteObject(regular));SelectObject(mem,original);CHECK("window-selection-still-retains-font",!DeleteObject(regular));SelectObject(window,winoriginal);CHECK("unselected-font-delete",DeleteObject(regular));CHECK("stale-font-refused",!SelectObject(mem,regular)&&!DeleteObject(regular));
 CHECK("independent-ansi-font-delete",DeleteObject(ansi));
 SelectObject(window,CreateSolidBrush(RGB(14,17,24)));Rectangle(window,0,0,640,360);SetBkMode(window,TRANSPARENT);SetTextColor(window,RGB(235,240,248));SelectObject(window,bold);text(window,24,32,L"THOS Windows");
 HFONT body=font(16,FW_NORMAL);SelectObject(window,body);SetTextColor(window,RGB(156,172,204));text(window,24,100,L"Schriften: ä ö ü ß €");HFONT large=font(26,FW_NORMAL);SelectObject(window,large);SetTextColor(window,RGB(100,220,180));text(window,24,142,L"Text mit echter Kantenglättung");
 SelectObject(window,winoriginal);DeleteObject(bold);DeleteObject(body);DeleteObject(large);SelectObject(mem,oldbitmap);DeleteObject(bitmap);DeleteDC(mem);
 say(failures?"gdi-font-result=FAIL\n":"gdi-font-result=PASS\n");say("gdi-font-ready-close\n");MSG msg;while(GetMessageA(&msg,0,0,0)){TranslateMessage(&msg);DispatchMessageA(&msg);}ExitProcess(failures?1:0);
}
