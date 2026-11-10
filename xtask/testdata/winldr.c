// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <stdint.h>
/* GENERATED_IMPORTS */
static unsigned failures;
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do{if(c)say("winldr-ok " n "\n");else{say("winldr-FAIL " n "\n");failures++;}}while(0)
static int eq(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static const char*names[]={ /* GENERATED_NAMES */ };
void __attribute__((noreturn)) start(void){
 /* GENERATED_CALLS */
 for(unsigned i=0;i</* GENERATED_COUNT */;i++){
  HMODULE m=GetModuleHandleA(names[i]);CHECK("every-static-import-discoverable",m!=0);CHECK("loadlibrary-finds-already-loaded",m&&LoadLibraryA(names[i])==m);
  char path[512],want[512];const char*prefix="C:\\Windows\\System32\\";unsigned at=0;while(prefix[at]){want[at]=prefix[at];at++;}unsigned j=0;while(names[i][j])want[at++]=names[i][j++];want[at]=0;
  CHECK("full-module-path",m&&GetModuleFileNameA(m,path,sizeof path)==at&&eq(path,want));
 }
 uintptr_t peb;__asm__ volatile("movq %%gs:0x60,%0":"=r"(peb));uintptr_t ldr=*(uintptr_t*)(peb+0x18);
 unsigned expected=/* GENERATED_COUNT */+6+(GetModuleHandleA("kernel32x.dll")!=0)+(GetModuleHandleA("ntdllx.dll")!=0)+(GetModuleHandleA("msvcrtx.dll")!=0);
 for(unsigned ring=0;ring<3;ring++){
  uintptr_t head=ldr+0x10+ring*16,node=*(uintptr_t*)head,prev=head;unsigned n=0;int valid=1;
  while(node!=head&&n<80){if(!node||*(uintptr_t*)(node+8)!=prev){valid=0;break;}prev=node;node=*(uintptr_t*)node;n++;}
  CHECK("all-three-rings-complete",valid&&node==head&&*(uintptr_t*)(head+8)==prev&&n==expected);
 }
 CHECK("executable-and-system-modules-retained",GetModuleHandleA(0)&&GetModuleHandleA("user32.dll")&&GetModuleHandleA("kernel32x.dll"));
 say(failures?"winldr-result=FAIL\n":"winldr-result=PASS\n");say("winldr-done\n");ExitProcess(failures?1:0);
}
