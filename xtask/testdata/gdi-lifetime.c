// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
void __attribute__((noreturn)) start(void){
 HDC s=GetDC(0),d=CreateCompatibleDC(s);HBITMAP b=CreateCompatibleBitmap(s,4096,4096);
 DWORD n;const char *ok="gdi-lifetime-allocation=PASS\n",*bad="gdi-lifetime-allocation=FAIL\n";
 BOOL passed=d&&b&&SelectObject(d,b)&&SetPixel(d,4095,4095,RGB(19,70,140))==RGB(19,70,140)&&GetPixel(d,4095,4095)==RGB(19,70,140);
 // Intentionally leave a selected 64 MiB bitmap/DC behind. Process teardown
 // must release them; the next invocation must allocate its own 64 MiB.
 const char *text=passed?ok:bad;unsigned len=0;while(text[len])len++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,len,&n,0);ExitProcess(passed?0:1);
}
