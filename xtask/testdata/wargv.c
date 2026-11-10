// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <errno.h>
#include <direct.h>
__declspec(dllimport) int __cdecl __wgetmainargs(int*,WCHAR***,WCHAR***,int,void*);
__declspec(dllimport) WCHAR **__winitenv;
__declspec(dllimport) WCHAR *_wcmdln;
static unsigned failures;
static HANDLE gate;
static struct{int status,argc;WCHAR**argv;WCHAR**env;WCHAR*raw;} result[8];
static int equal(const WCHAR*a,const WCHAR*b){if(!a||!b)return a==b;while(*a&&*a==*b){a++;b++;}return *a==*b;}
static void say(const char*s){DWORD n;unsigned k=0;while(s[k])k++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,k,&n,0);}
#define CHECK(n,c) do{if(c)say("wargv-ok " n "\n");else{say("wargv-FAIL " n "\n");failures++;}}while(0)
static DWORD WINAPI worker(void*arg){unsigned i=(unsigned)(ULONG_PTR)arg;WaitForSingleObject(gate,INFINITE);result[i].raw=GetCommandLineW();result[i].status=__wgetmainargs(&result[i].argc,&result[i].argv,&result[i].env,0,0);return 0;}
void __attribute__((noreturn)) start(void){
 ULONG_PTR peb;__asm__ volatile("movq %%gs:0x60,%0":"=r"(peb));ULONG_PTR params=*(ULONG_PTR*)(peb+0x20);
 WCHAR*raw=*(WCHAR**)(params+0x78);unsigned units=*(unsigned short*)(params+0x70)/2;
 CHECK("raw-wide-command-initialized",_wcmdln&&_wcmdln[0]);CHECK("initial-env-not-eagerly-built",__winitenv==0);
 SetLastError(56789);WCHAR*api=GetCommandLineW();
 CHECK("commandline-system-buffer",api==raw&&api==_wcmdln&&raw[units]==0);
 CHECK("commandline-preserves-last-error",GetLastError()==56789);
 CHECK("commandline-unicode-raw-text",equal(api,L"\"/wargv.exe\" \"a b\" \"\" \"Größe 😀\" \"a\\\"b\" \"C:\\dir\\\\\""));
 typedef WCHAR*(WINAPI*getcmd)(void);getcmd dynamic=(getcmd)GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetCommandLineW");
 CHECK("commandline-dynamic-export",dynamic&&dynamic()==raw);

 int argc=-7;WCHAR**argv=(WCHAR**)(ULONG_PTR)0x1234,**env=argv;errno=0;
 CHECK("null-parameter-error",__wgetmainargs(0,&argv,&env,0,0)==EINVAL&&errno==EINVAL);
 CHECK("unsupported-wildcard-no-partial-output",__wgetmainargs(&argc,&argv,&env,1,0)==ENOSYS&&argc==-7&&argv==(WCHAR**)(ULONG_PTR)0x1234&&__winitenv==0);
 WCHAR**slot=(WCHAR**)(params+0x80);WCHAR*old=*slot;
 static WCHAR controlled[]=L"THOS_TAG=Größe 😀\0SECOND=value with spaces\0\0";*slot=controlled;
 gate=CreateEventA(0,TRUE,FALSE,0);HANDLE threads[8];int created=gate!=0;for(unsigned i=0;i<8;i++){threads[i]=CreateThread(0,0,worker,(void*)(ULONG_PTR)i,0,0);if(!threads[i])created=0;}CHECK("eight-threads-created",created);SetEvent(gate);CHECK("parallel-initialization-joined",WaitForMultipleObjects(8,threads,TRUE,10000)==WAIT_OBJECT_0);
 int same=1;for(unsigned i=0;i<8;i++){if(result[i].status||result[i].argc!=6||result[i].argv!=result[0].argv||result[i].env!=result[0].env)same=0;if(threads[i])CloseHandle(threads[i]);}CHECK("shared-complete-cache",same);int raw_same=1;for(unsigned i=0;i<8;i++)if(result[i].raw!=raw)raw_same=0;CHECK("commandline-eight-thread-identity",raw_same);
 argc=result[0].argc;argv=result[0].argv;env=result[0].env;CHECK("argv-program",equal(argv[0],L"/wargv.exe"));CHECK("quoted-space",equal(argv[1],L"a b"));CHECK("empty-argument",equal(argv[2],L""));CHECK("unicode-surrogate-argument",equal(argv[3],L"Größe 😀"));CHECK("literal-quote",equal(argv[4],L"a\"b"));CHECK("trailing-backslash",equal(argv[5],L"C:\\dir\\"));CHECK("argv-terminator",argv[argc]==0);
 CHECK("initial-env-metadata-copy",env&&equal(env[0],L"THOS_TAG=Größe 😀")&&equal(env[1],L"SECOND=value with spaces")&&env[2]==0&&env[0]!=controlled);CHECK("winitenv-data-export",__winitenv==env);*slot=old;
 WCHAR**again=0,**againenv=0;int againc=0;CHECK("repeated-initial-env-snapshot",__wgetmainargs(&againc,&again,&againenv,0,0)==0&&again==argv&&againenv==env&&againc==argc&&equal(againenv[0],L"THOS_TAG=Größe 😀"));
 /* Controlled private process metadata exercises long UTF16 and pointer
    lifetime separately from the loader's current 255-byte launch limit.
    Applications must not normally modify their system command line. */
 static WCHAR longraw[1537];for(unsigned i=0;i<1536;i++)longraw[i]=(WCHAR)(0x400+i%128);longraw[1300]=0xd83d;longraw[1301]=0xde00;longraw[1536]=0;
 WCHAR**cmdslot=(WCHAR**)(params+0x78);unsigned short*lengths=(unsigned short*)(params+0x70);unsigned short oldlen=lengths[0],oldmax=lengths[1];*cmdslot=longraw;lengths[0]=3072;lengths[1]=3074;
 api=GetCommandLineW();CHECK("commandline-long-metadata-without-truncation",api==longraw&&equal(api,longraw)&&api[1536]==0);
 *cmdslot=raw;lengths[0]=oldlen;lengths[1]=oldmax;CHECK("commandline-restored-metadata-no-cache",GetCommandLineW()==raw);
 char tiny[1]={0x55};errno=0;CHECK("runtime-errno-visible-to-caller",!_getcwd(tiny,1)&&errno==ERANGE&&tiny[0]==0x55);CloseHandle(gate);
 say(failures?"wargv-result=FAIL\n":"wargv-result=PASS\n");say("wargv-done\n");ExitProcess(failures?1:0);
}
