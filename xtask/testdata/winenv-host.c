// SPDX-License-Identifier: GPL-2.0-or-later
// Host stress: allocator accounting, failed replacement and real futex contention.
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#define EXPORT
#define WINAPI
typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef int64_t i64;typedef int BOOL;typedef void*HANDLE;
static _Thread_local u32 last_error;
static long live_allocations;static int fail_next;
static void SetLastError(u32 e){last_error=e;}
static HANDLE LocalAlloc(u32 flags,u64 n){(void)flags;if(__atomic_exchange_n(&fail_next,0,__ATOMIC_SEQ_CST))return 0;void*p=malloc(n);if(p)__atomic_add_fetch(&live_allocations,1,__ATOMIC_SEQ_CST);return p;}
static HANDLE LocalFree(HANDLE p){if(p){free(p);__atomic_sub_fetch(&live_allocations,1,__ATOMIC_SEQ_CST);}return 0;}
static i64 sc6(i64 n,i64 a,i64 b,i64 c,i64 d,i64 e,i64 f){return syscall(n,a,b,c,d,e,f);}
#include "../msvcrtx/winenv.h"
static void*worker(void*p){unsigned n=(uintptr_t)p;u16 name[]={'H','O','S','T','0'+n,0},value[]={'A'+n,0},out[4];for(unsigned k=0;k<1000;k++){assert(SetEnvironmentVariableW(name,value));assert(GetEnvironmentVariableW(name,out,4)==1&&out[0]==value[0]);u16*s=GetEnvironmentStringsW();assert(s);assert(FreeEnvironmentStringsW(s));}return 0;}
int main(void){
 assert(SetEnvironmentVariableW(L"HOST",L"old"));long baseline=live_allocations;
 for(unsigned i=0;i<10000;i++)assert(SetEnvironmentVariableW(L"HOST",L"replacement"));
 assert(live_allocations==baseline);
 fail_next=1;assert(!SetEnvironmentVariableW(L"HOST",L"must-not-replace")&&last_error==8);
 u16 out[32];assert(GetEnvironmentVariableW(L"HOST",out,32)==11&&out[0]=='r');assert(live_allocations==baseline);
 for(unsigned i=0;i<1000;i++){u16*s=GetEnvironmentStringsW();char*a=GetEnvironmentStringsA();assert(s&&a);FreeEnvironmentStringsW(s);FreeEnvironmentStringsA(a);}assert(live_allocations==baseline);
 u16 *max=malloc(32768*2);for(unsigned i=0;i<32767;i++)max[i]='x';max[32766]=0;assert(SetEnvironmentVariableW(L"MAX",max));assert(GetEnvironmentVariableW(L"MAX",0,0)==32767);max[32766]='x';max[32767]=0;assert(!SetEnvironmentVariableW(L"MAX",max)&&last_error==206);assert(GetEnvironmentVariableW(L"MAX",0,0)==32767);free(max);SetEnvironmentVariableW(L"MAX",0);assert(live_allocations==baseline);
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],0,worker,(void*)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],0));assert(live_allocations==baseline+8);
 unsigned inserted=0;for(unsigned i=0;i<80;i++){u16 name[]={'C','A','P',(u16)('A'+i/26),(u16)('A'+i%26),0};if(SetEnvironmentVariableW(name,L"v"))inserted++;else assert(last_error==8);}assert(inserted==44);assert(GetEnvironmentVariableW(L"HOST",out,32)==11);
 puts("environment host stress PASS: 10000 replacements, 1000 dual snapshots, allocation failure, 32767-unit boundary, 8x1000 threads, table cap; no live allocation growth");return 0;
}
