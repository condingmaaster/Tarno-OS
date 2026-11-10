// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <stdint.h>
static unsigned failures;static unsigned char*b;
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do{if(c)say("winexportbounds-ok " n "\n");else{say("winexportbounds-FAIL " n "\n");failures++;}}while(0)
static void p16(unsigned at,unsigned short v){*(unsigned short*)(b+at)=v;}
static void p32(unsigned at,unsigned v){*(unsigned*)(b+at)=v;}
static void text(unsigned at,const char*s){while((*((char*)b+at++)=*s++));}
static void reset(void){for(unsigned i=0;i<4096;i++)b[i]=0;b[0]='M';b[1]='Z';p32(60,64);p32(64,0x4550);p16(84,240);p16(88,0x20b);p32(144,4096);p32(196,16);p32(200,0x400);p32(204,0x200);p32(0x410,1);p32(0x414,2);p32(0x418,2);p32(0x41c,0x700);p32(0x420,0x710);p32(0x424,0x720);p32(0x700,0x800);p32(0x704,0x810);p32(0x710,0x740);p32(0x714,0x750);p16(0x720,0);p16(0x722,1);text(0x740,"Alpha");text(0x750,"Zeta");b[0x800]=0xc3;b[0x810]=0xc3;}
static int absent(const char*name){SetLastError(0);return !GetProcAddress((HMODULE)b,name)&&GetLastError()==127;}
void __attribute__((noreturn)) start(void){
 b=VirtualAlloc(0,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!b){say("winexportbounds-FAIL allocation\nwinexportbounds-done\n");ExitProcess(1);}
 reset();CHECK("valid-mapped-name-first",GetProcAddress((HMODULE)b,"Alpha")== (FARPROC)(b+0x800));CHECK("valid-mapped-name-last",GetProcAddress((HMODULE)b,"Zeta")== (FARPROC)(b+0x810));CHECK("valid-ordinal",GetProcAddress((HMODULE)b,(const char*)2)==(FARPROC)(b+0x810));CHECK("missing-name",absent("Middle"));
 p32(0x418,0xffffffff);CHECK("huge-name-count-bounded",absent("Alpha"));reset();p32(0x414,0xffffffff);CHECK("huge-function-count-bounded",absent("Alpha"));
 reset();p32(0x420,4092);CHECK("name-table-crosses-image",absent("Alpha"));reset();p32(0x424,4094);CHECK("ordinal-table-crosses-image",absent("Alpha"));reset();p32(0x41c,4092);CHECK("eat-crosses-image",absent("Alpha"));
 reset();p16(0x720,2);CHECK("ordinal-index-outside-eat",absent("Alpha"));reset();p32(0x700,4096);CHECK("target-outside-image",absent("Alpha"));reset();p32(0x700,0);CHECK("zero-export-slot",absent("Alpha"));
 reset();p32(0x710,4095);b[4095]='x';CHECK("name-must-terminate-inside-image",absent("Alpha"));reset();p32(0x700,0x5f8);text(0x5f8,"bad.bad.bad");CHECK("forwarder-must-terminate-inside-directory",absent("Alpha"));
 reset();p32(0x700,0x500);text(0x500,"kernel32.GetCurrentProcessId");CHECK("valid-forwarder-resolves",GetProcAddress((HMODULE)b,"Alpha")==GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetCurrentProcessId"));
 reset();p32(0x700,0x500);text(0x500,"missing_dll.no_entry");CHECK("missing-forwarder-module",absent("Alpha"));
 reset();p32(200,4090);p32(204,40);CHECK("directory-crosses-image",absent("Alpha"));reset();p32(204,39);CHECK("directory-too-short",absent("Alpha"));reset();p16(84,119);CHECK("optional-header-too-short",absent("Alpha"));reset();p32(60,0xfffffff0);CHECK("hostile-pe-offset-bounded",absent("Alpha"));reset();b[0]='X';CHECK("dos-magic-required",absent("Alpha"));
 reset();p32(144,0xffffffff);CHECK("hostile-image-size-bounded",absent("Alpha"));
 // Two lexical aliases may legitimately share one EAT slot.
 reset();p32(0x414,1);p16(0x722,0);CHECK("multiple-names-one-function",GetProcAddress((HMODULE)b,"Zeta")== (FARPROC)(b+0x800));
 reset();CHECK("healthy-after-malformed-images",GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetCurrentProcessId")!=0);CHECK("high-kernel-pointer-rejected",!GetProcAddress((HMODULE)(uintptr_t)0xfffffffffffffff0ULL,"Alpha"));
 say(failures?"winexportbounds-result=FAIL\n":"winexportbounds-result=PASS\n");say("winexportbounds-done\n");ExitProcess(failures?1:0);
}
