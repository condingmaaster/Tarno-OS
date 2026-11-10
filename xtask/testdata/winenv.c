// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
static unsigned failures;
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do{if(c)say("winenv-ok " n "\n");else{say("winenv-FAIL " n "\n");failures++;}}while(0)
static int eq(const WCHAR*a,const WCHAR*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static int has_a(const char*b,const char*item){for(unsigned n=0;n<150000&&b[n];){unsigned j=0;while(b[n+j]&&b[n+j]==item[j])j++;if(!b[n+j]&&!item[j])return 1;while(b[n]&&n<150000)n++;n++;}return 0;}
static int has(const WCHAR*b,const WCHAR*item){for(unsigned n=0;n<150000&&b[n];){if(eq(b+n,item))return 1;while(b[n]&&n<150000)n++;n++;}return 0;}
static WCHAR large[10001],out[10004];
static DWORD WINAPI worker(void*arg){unsigned i=(unsigned)(ULONG_PTR)arg;WCHAR name[]={L'T',L'H',L'O',L'S',L'_',L'0'+i,0};WCHAR value[]={L'A'+i,0};WCHAR copy[4];for(unsigned j=0;j<80;j++){if(!SetEnvironmentVariableW(name,value)||GetEnvironmentVariableW(name,copy,4)!=1||copy[0]!=value[0])return 1;}return 0;}
void __attribute__((noreturn)) start(void){
 SetLastError(777);CHECK("set-preserves-error",SetEnvironmentVariableW(L"THOS_TEST",L"small")&&GetLastError()==777);
 CHECK("ascii-case-insensitive",GetEnvironmentVariableW(L"thos_test",out,10004)==5&&eq(out,L"small"));
 SetLastError(778);CHECK("wide-size-query",GetEnvironmentVariableW(L"THOS_TEST",0,0)==6&&GetLastError()==778);
 out[0]=0x5555;out[3]=0x5555;CHECK("wide-short-buffer-size-and-canary",GetEnvironmentVariableW(L"THOS_TEST",out,3)==6&&out[3]==0x5555);
 CHECK("unicode-roundtrip",SetEnvironmentVariableW(L"THOS_Größe",L"Größe 😀 中文")&&GetEnvironmentVariableW(L"THOS_Größe",out,10004)==11&&eq(out,L"Größe 😀 中文"));
 for(unsigned i=0;i<10000;i++)large[i]=L'a'+i%26;large[10000]=0;
 CHECK("large-value-set",SetEnvironmentVariableW(L"THOS_LARGE",large));
 CHECK("large-value-size-query",GetEnvironmentVariableW(L"THOS_LARGE",0,0)==10001);
 out[10001]=0x5555;CHECK("large-value-exact-roundtrip",GetEnvironmentVariableW(L"THOS_LARGE",out,10001)==10000&&eq(out,large)&&out[10001]==0x5555);
 WCHAR *first=GetEnvironmentStringsW();CHECK("wide-snapshot-complete",first&&has(first,L"THOS_TEST=small")&&has(first,L"THOS_Größe=Größe 😀 中文"));

 CHECK("mutation-after-snapshot",SetEnvironmentVariableW(L"THOS_TEST",L"changed"));WCHAR*second=GetEnvironmentStringsW();
 CHECK("snapshots-have-independent-lifetimes",first&&second&&first!=second&&has(first,L"THOS_TEST=small")&&has(second,L"THOS_TEST=changed"));
 CHECK("snapshot-large-not-truncated",second&&({int found=0;for(unsigned i=0;i<150000&&second[i];){unsigned j=0;const WCHAR*k=L"THOS_LARGE=";while(k[j]&&second[i+j]==k[j])j++;if(!k[j]){found=eq(second+i+j,large);break;}while(second[i]&&i<150000)i++;i++;}found;}));
 CHECK("free-first-snapshot",first&&FreeEnvironmentStringsW(first));CHECK("free-second-snapshot",second&&FreeEnvironmentStringsW(second));
 SetLastError(779);out[0]=0x5555;CHECK("empty-value-has-terminator",SetEnvironmentVariableW(L"THOS_EMPTY",L"")&&GetEnvironmentVariableW(L"THOS_EMPTY",out,2)==0&&out[0]==0&&GetLastError()==779);
 CHECK("delete-existing",SetEnvironmentVariableW(L"THOS_TEST",0));SetLastError(0);CHECK("missing-error",GetEnvironmentVariableW(L"THOS_TEST",out,10004)==0&&GetLastError()==203);
 CHECK("delete-missing-idempotent",SetEnvironmentVariableW(L"THOS_TEST",0));
 SetLastError(0);CHECK("empty-name-rejected",!SetEnvironmentVariableW(L"",L"x")&&GetLastError()==87);SetLastError(0);CHECK("equals-name-rejected",!SetEnvironmentVariableW(L"A=B",L"x")&&GetLastError()==87);
 char euro[]={ (char)0x80,0},aout[4];CHECK("ansi-set-acp",SetEnvironmentVariableA("THOS_ACP",euro));CHECK("acp-to-unicode",GetEnvironmentVariableW(L"THOS_ACP",out,10004)==1&&out[0]==0x20ac);CHECK("unicode-to-acp",SetEnvironmentVariableW(L"THOS_ACP",L"€")&&GetEnvironmentVariableA("THOS_ACP",aout,4)==1&&(unsigned char)aout[0]==0x80);
 typedef char*(WINAPI *snap_a)(void);snap_a get_a=(snap_a)GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetEnvironmentStringsA");CHECK("legacy-ansi-export-present",GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetEnvironmentStrings")!=0);CHECK("oem-snapshot-source",SetEnvironmentVariableW(L"THOS_OEM",L"äöüß"));char *asnap=get_a?get_a():0;CHECK("ansi-snapshot-allocated",asnap!=0);CHECK("ansi-snapshot-is-oem437",asnap&&has_a(asnap,"THOS_OEM=\x84\x94\x81\xe1"));CHECK("free-ansi-snapshot",asnap&&FreeEnvironmentStringsA(asnap));
 HANDLE threads[8];int okay=1;for(unsigned i=0;i<8;i++){threads[i]=CreateThread(0,0,worker,(void*)(ULONG_PTR)i,0,0);if(!threads[i])okay=0;}for(unsigned i=0;i<8;i++)if(threads[i]){DWORD code=99;if(WaitForSingleObject(threads[i],10000)!=WAIT_OBJECT_0||!GetExitCodeThread(threads[i],&code)||code)okay=0;CloseHandle(threads[i]);}CHECK("eight-threads-independent-mutations",okay);
 say(failures?"winenv-result=FAIL\n":"winenv-result=PASS\n");say("winenv-done\n");ExitProcess(failures?1:0);
}
