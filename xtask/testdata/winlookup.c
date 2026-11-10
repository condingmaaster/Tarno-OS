// SPDX-License-Identifier: GPL-2.0-or-later
// Compare ordinary imports with runtime lookup; do not report missing APIs as success.
#include <windows.h>
#include <stdio.h>
#include <string.h>
__declspec(dllimport) int marker_a(void);
__declspec(dllimport) int marker_b(void);
__declspec(dllimport) HANDLE WINAPI ForwardFind(LPCSTR,FINDEX_INFO_LEVELS,LPVOID,FINDEX_SEARCH_OPS,LPVOID,DWORD);
typedef struct { USHORT Length,MaximumLength; char *Buffer; } COUNTED_NAME;
typedef LONG (NTAPI *LDR_LOOKUP)(HMODULE,COUNTED_NAME *,ULONG,FARPROC *);
static int failures;
#define CHECK(label,condition) do { if(condition) printf("winlookup-ok %s\n",label); else {printf("winlookup-FAIL %s\n",label);failures++;} } while(0)
int main(void) {
    WIN32_FIND_DATAA data;
    HANDLE search=FindFirstFileExA("C:\\Windows\\System32\\kernel32x.dll",FindExInfoBasic,&data,FindExSearchNameMatch,0,0);
    printf("winlookup-static-find=%d\n",search!=INVALID_HANDLE_VALUE);
    if(search!=INVALID_HANDLE_VALUE)FindClose(search);
    HMODULE kernel=LoadLibraryA("kernel32.dll");
    FARPROC core=GetProcAddress(kernel,"GetLastError");
    FARPROC a=GetProcAddress(kernel,"FindFirstFileExA");
    DWORD error=GetLastError();
    FARPROC w=GetProcAddress(kernel,"FindFirstFileExW");
    HMODULE extension=LoadLibraryA("kernel32x.dll");
    FARPROC direct=extension?GetProcAddress(extension,"FindFirstFileExA"):0;
    printf("winlookup-core=%d ansi=%d wide=%d ansi-error=%lu extension=%d\n",core!=0,a!=0,w!=0,(unsigned long)error,direct!=0);
    CHECK("dynamic-ansi-wide",a && w && a==direct);
    CHECK("synthetic-ordinal",GetProcAddress(kernel,(LPCSTR)4)==core);
    CHECK("invalid-ordinal",GetProcAddress(kernel,(LPCSTR)0xffff)==0 && GetLastError()==127);
    CHECK("case-sensitive-name",GetProcAddress(kernel,"findfirstfileexa")==0 && GetLastError()==127);
    CHECK("invalid-module",GetProcAddress(0,"FindFirstFileExA")==0 && GetLastError()==127);
    CHECK("extension-override",GetProcAddress(kernel,"WriteFile")==GetProcAddress(extension,"WriteFile"));
    HMODULE crt=LoadLibraryA("msvcrt.dll"),crtx=LoadLibraryA("msvcrtx.dll");
    CHECK("crt-override",crt && crtx && GetProcAddress(crt,"printf")==GetProcAddress(crtx,"printf"));
    CHECK("synthetic-modules-visible",GetModuleHandleA("msvcrt.dll") && GetModuleHandleA("USER32.DLL") && GetModuleHandleA("gdi32.dll"));
    HMODULE fixture=LoadLibraryA("lookupa.dll");
    CHECK("fixture-markers",marker_a()==17 && marker_b()==29);
    CHECK("forwarded-name",a && GetProcAddress(fixture,"ForwardFind")==a);
    CHECK("forwarded-ordinal",core && GetProcAddress(fixture,"ForwardOrdinal")==core);
    CHECK("forward-loop-bounded",GetProcAddress(fixture,"Loop")==0 && GetLastError()==127);
    HANDLE again=ForwardFind("C:\\Windows\\System32\\kernel32x.dll",FindExInfoBasic,&data,FindExSearchNameMatch,0,0);
    CHECK("static-forward-call",again!=INVALID_HANDLE_VALUE && data.nFileSizeLow>0);
    if(again!=INVALID_HANDLE_VALUE)FindClose(again);
    if(a){
        typedef HANDLE (WINAPI *FIND_EX)(LPCSTR,FINDEX_INFO_LEVELS,LPVOID,FINDEX_SEARCH_OPS,LPVOID,DWORD);
        again=((FIND_EX)a)("C:\\Windows\\System32\\kernel32x.dll",FindExInfoBasic,&data,FindExSearchNameMatch,0,0);
        CHECK("dynamic-call",again!=INVALID_HANDLE_VALUE && data.nFileSizeLow>0);
        if(again!=INVALID_HANDLE_VALUE)FindClose(again);
    }
    LDR_LOOKUP lookup=(LDR_LOOKUP)GetProcAddress(LoadLibraryA("ntdll.dll"),"LdrGetProcedureAddress");
    CHECK("ldr-entry",lookup!=0);
    if(lookup){
        char text[]="FindFirstFileExAignored";
        COUNTED_NAME name={sizeof("FindFirstFileExA")-1,sizeof text,text}; FARPROC result=0;
        LONG status=lookup(kernel,&name,0,&result);
        CHECK("ldr-counted-name",status==0 && result && result==a);
        result=0;status=lookup(kernel,0,4,&result);
        CHECK("ldr-ordinal",status==0 && result==core);
        name.Length=sizeof text;name.MaximumLength=1;
        CHECK("ldr-invalid-length",(ULONG)lookup(kernel,&name,0,&result)==0xc000000dUL);
        CHECK("ldr-invalid-output",(ULONG)lookup(kernel,0,4,0)==0xc0000005UL);
    }
    printf("winlookup-failures=%d\n",failures);
    puts("winlookup-done");
    return failures?1:0;
}
