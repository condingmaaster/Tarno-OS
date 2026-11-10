// SPDX-License-Identifier: GPL-2.0-or-later
// Inspect native synthetic images directly, independently of extension DLLs.
// Never execute a trampoline whose bytes or export-directory range are wrong.
#include <windows.h>
#include <stdint.h>
static unsigned failures;
static int compare(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do{if(c)say("winexports-ok " n "\n");else{say("winexports-FAIL " n "\n");failures++;}}while(0)
static void image(const char*name,unsigned flag){
 unsigned char*b=(unsigned char*)GetModuleHandleA(name);if(!b){failures++;say("winexports-FAIL missing-module\n");return;}
 unsigned pe=*(unsigned*)(b+0x3c),opt=pe+24,rva=*(unsigned*)(b+opt+112),bytes=*(unsigned*)(b+opt+116);
 unsigned char*e=b+rva;unsigned count=*(unsigned*)(e+20),*eat=(unsigned*)(b+*(unsigned*)(e+28));
 int valid=count>0&&count<128&&rva>=0x180&&rva+bytes<=4096;
 for(unsigned i=0;valid&&i<count;i++){
  unsigned at=eat[i];unsigned char*t=b+at;unsigned selector=0x4e540000|flag|i;
  if(flag==0x4000&&i>=32){if(at<0x180||at+16>rva)valid=0;continue;} /* genuine native CRT data slots */
  if(at<0x180||at+11>4096||(at<rva+bytes&&at+11>rva)||t[0]!=0xb8||*(unsigned*)(t+1)!=selector||t[5]!=0x49||t[6]!=0x89||t[7]!=0xca||t[8]!=0x0f||t[9]!=0x05||t[10]!=0xc3)valid=0;
 }
 if(valid){say("winexports-ok ");say(name);say("-all-trampolines-disjoint\n");}
 else{say("winexports-FAIL ");say(name);say("-overlap-or-bad-code\n");failures++;}
 unsigned named=*(unsigned*)(e+24),*names=(unsigned*)(b+*(unsigned*)(e+32));unsigned short*ords=(unsigned short*)(b+*(unsigned*)(e+36));int ordered=named==count;
 for(unsigned i=0;ordered&&i<named;i++){
  if(names[i]>=4096||ords[i]>=count||(i&&compare((char*)b+names[i-1],(char*)b+names[i])>=0))ordered=0;
  else{
   // An independent binary search must resolve every exported name to its
   // actual EAT entry, including the three formerly overlapping NTDLL stubs.
   unsigned lo=0,hi=named;const char*key=(char*)b+names[i];
   while(lo<hi){unsigned mid=lo+(hi-lo)/2;int c=compare(key,(char*)b+names[mid]);if(c>0)lo=mid+1;else hi=mid;}
   if(lo>=named||compare(key,(char*)b+names[lo])||ords[lo]!=ords[i])ordered=0;
  }
 }
 if(ordered){say("winexports-ok ");say(name);say("-binary-search-names\n");}else{say("winexports-FAIL ");say(name);say("-unsorted-names\n");failures++;}
 if(flag==0x8000&&valid){
  typedef LONG(WINAPI*flush_fn)(HANDLE,void**,SIZE_T*,void*);
  typedef LONG(WINAPI*notify_fn)(HANDLE,HANDLE,void*,void*,void*,ULONG,BOOL,void*,ULONG,BOOL);
  void*base=0;SIZE_T size=0;
  CHECK("real-ntflush-trampoline",((flush_fn)(b+eat[40]))((HANDLE)(LONG_PTR)-1,&base,&size,0)==(LONG)0xc0000019);
  CHECK("real-ntnotify-trampoline",((notify_fn)(b+eat[42]))(0,0,0,0,0,0,0,0,0,0)==(LONG)0xc0000008);
 }
}
void __attribute__((noreturn)) start(void){image("kernel32.dll",0);image("user32.dll",0x2000);image("gdi32.dll",0x1000);image("msvcrt.dll",0x4000);image("ntdll.dll",0x8000);say(failures?"winexports-result=FAIL\n":"winexports-result=PASS\n");say("winexports-done\n");ExitProcess(failures?1:0);}
