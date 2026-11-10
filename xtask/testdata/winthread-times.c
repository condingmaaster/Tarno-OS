/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
static HANDLE output,gate;static int fail;static volatile LONG ready;static volatile ULONGLONG sink[8];
typedef BOOL(WINAPI*TIMES)(HANDLE,FILETIME*,FILETIME*,FILETIME*,FILETIME*);static TIMES times;
struct reading{FILETIME create,exit,kernel,user;};static struct reading before[8],after[8];
static ULONGLONG value(FILETIME f){return ((ULONGLONG)f.dwHighDateTime<<32)|f.dwLowDateTime;}
static ULONGLONG cpu(struct reading*t){return value(t->kernel)+value(t->user);}
static int query(HANDLE h,struct reading*t){return times(h,&t->create,&t->exit,&t->kernel,&t->user);}
static void say(const char*s){DWORD n=0;while(s[n])n++;WriteFile(output,s,n,&n,0);}
#define CHECK(s,c) do{if(c)say("winthread-times-ok " s "\n");else{say("winthread-times-FAIL " s "\n");fail=1;}}while(0)
static DWORD WINAPI worker(void*arg){unsigned i=(unsigned)(ULONG_PTR)arg;__atomic_add_fetch(&ready,1,__ATOMIC_RELEASE);if(WaitForSingleObject(gate,INFINITE)!=WAIT_OBJECT_0)return 1;ULONGLONG end=GetTickCount64()+250,n=1;do{for(unsigned k=0;k<5000;k++)n=n*1664525+1013904223;sink[i]=n;}while(GetTickCount64()<end);struct reading own;if(!query(GetCurrentThread(),&own)||!value(own.user)||!value(own.kernel))return 2;return 0;}
void start(void){output=GetStdHandle(STD_OUTPUT_HANDLE);say("winthread-times-start\n");times=(TIMES)GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetThreadTimes");CHECK("actual-time-export",times!=0);if(!times)goto finish;
 FILETIME wall;GetSystemTimeAsFileTime(&wall);struct reading first;SetLastError(0x1234);CHECK("current-thread-real-creation",query(GetCurrentThread(),&first)&&value(first.create)>116444736000000000ULL&&value(first.create)<=value(wall)&&GetLastError()==0x1234);
 struct reading a,b;query(GetCurrentThread(),&a);ULONGLONG begin=GetTickCount64();Sleep(180);query(GetCurrentThread(),&b);ULONGLONG elapsed=GetTickCount64()-begin;CHECK("sleep-is-not-CPU-time",elapsed>=150&&cpu(&b)>=cpu(&a)&&cpu(&b)-cpu(&a)<elapsed*10000/2);
 gate=CreateEventA(0,TRUE,FALSE,0);HANDLE th[8];FILETIME early,late;GetSystemTimeAsFileTime(&early);for(unsigned i=0;i<8;i++)th[i]=CreateThread(0,0,worker,(void*)(ULONG_PTR)i,0,0);ULONGLONG deadline=GetTickCount64()+10000;while(ready!=8&&GetTickCount64()<deadline)Sleep(1);CHECK("eight-workers-really-started",ready==8);Sleep(30);int ok=1;for(int i=0;i<8;i++)if(!th[i]||!query(th[i],&before[i]))ok=0;Sleep(150);for(int i=0;i<8;i++)if(!th[i]||!query(th[i],&after[i])||cpu(&before[i])!=cpu(&after[i]))ok=0;CHECK("blocked-workers-CPU-time-stays-fixed",ok);
 SetEvent(gate);for(int i=0;i<8;i++){DWORD code=99;if(!th[i]||WaitForSingleObject(th[i],20000)!=WAIT_OBJECT_0||!GetExitCodeThread(th[i],&code)||code)ok=0;}GetSystemTimeAsFileTime(&late);CHECK("workers-query-user-and-kernel-time",ok);
 ULONGLONG total=0;int clocks=1,active=1;for(int i=0;i<8;i++){if(!th[i]||!query(th[i],&after[i])){clocks=active=0;continue;}struct reading*t=&after[i];if(value(t->create)<value(early)||value(t->exit)<value(t->create)||value(t->exit)>value(late))clocks=0;if(!value(t->user)||!value(t->kernel)||cpu(t)<=cpu(&before[i]))active=0;total+=cpu(t);}
 CHECK("real-create-exit-FILETIME-bounds",clocks);CHECK("actual-user-kernel-CPU-both-increase",active);CHECK("per-thread-CPU-not-eight-wall-clocks",total<=(value(late)-value(early))*4+1000000ULL);
 Sleep(100);int stable=1;for(int i=0;i<8;i++){struct reading current;if(!th[i]||!query(th[i],&current)||cpu(&current)!=cpu(&after[i])||value(current.exit)!=value(after[i].exit))stable=0;}CHECK("completed-thread-records-stable",stable);
 struct reading untouched;untouched.create.dwLowDateTime=0x55555555;SetLastError(0);CHECK("all-output-pointers-validated-before-write",!times(GetCurrentThread(),&untouched.create,&untouched.exit,&untouched.kernel,(void*)1)&&GetLastError()==998&&untouched.create.dwLowDateTime==0x55555555);SetLastError(0);CHECK("null-output-refused",!times(GetCurrentThread(),0,&a.exit,&a.kernel,&a.user)&&GetLastError()==87);SetLastError(0);CHECK("event-is-not-thread",!query(gate,&a)&&GetLastError()==6);SetLastError(0);CHECK("high-handle-bits-refused",th[0]&&!query((HANDLE)((ULONG_PTR)th[0]|0x100000000ULL),&a)&&GetLastError()==6);
 HANDLE old=th[0];for(int i=0;i<8;i++)if(th[i])CloseHandle(th[i]);SetLastError(0);CHECK("closed-handle-refused",!query(old,&a)&&GetLastError()==6);if(gate)CloseHandle(gate);
 finish:say(fail?"winthread-times-result=FAIL\n":"winthread-times-result=PASS\n");say("winthread-times-done\n");ExitProcess(fail?1:0);
}
