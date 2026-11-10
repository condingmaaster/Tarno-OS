#include <windows.h>
static HANDLE out;static int fail;
static void say(const char*s){DWORD n=0;while(s[n])n++;WriteFile(out,s,n,&n,0);}
#define CHECK(s,c) do {if(c){say("windyn-ok " s "\n");}else{say("windyn-FAIL " s "\n");fail=1;}}while(0)
typedef int (*CALL)(void);
static DWORD WINAPI worker(void*x){for(int i=0;i<40;i++){HMODULE h=LoadLibraryA("leaf.dll");CALL f=(CALL)GetProcAddress(h,"magic");if(!h||!f||f()!=77||!FreeLibrary(h))return 1;}return 0;}
void start(void){out=GetStdHandle(STD_OUTPUT_HANDLE);say("windyn-start\n");CHECK("not-statically-imported",GetModuleHandleA("leaf.dll")==0);
 HMODULE h=LoadLibraryA("leaf.dll");CHECK("load-new-file",h!=0);if(h){
 CALL f=(CALL)GetProcAddress(h,"counter"),magic=(CALL)GetProcAddress(h,"magic");CHECK("real-code-and-relocated-data",f&&f()==8&&f()==9&&magic&&magic()==77);
 CHECK("discoverable",GetModuleHandleA("leaf.dll")==h);CHECK("full-module-handle",GetModuleHandleW(L"C:\\Windows\\System32\\leaf.dll")==h);CHECK("wrong-path-same-basename-rejected",GetModuleHandleA("C:\\nowhere\\leaf.dll")==0&&GetModuleHandleW(L"C:\\nowhere\\leaf.dll")==0);CHECK("executable-name-handle",GetModuleHandleA("windyn.exe")==GetModuleHandleA(0)&&GetModuleHandleW(L"C:\\windyn.exe")==GetModuleHandleW(0));char name[256];DWORD n=GetModuleFileNameA(h,name,sizeof(name));CHECK("full-path",n==28&&name[0]=='C');
 HMODULE again=LoadLibraryA("LEAF.DLL");CHECK("repeat-reference",again==h);CHECK("release-one-reference",FreeLibrary(again)&&GetModuleHandleA("leaf.dll")==h&&magic&&magic()==77);
 HANDLE threads[8];for(int i=0;i<8;i++)threads[i]=CreateThread(0,0,worker,0,0,0);int ok=1;for(int i=0;i<8;i++){DWORD code=9;if(!threads[i]||WaitForSingleObject(threads[i],INFINITE)!=WAIT_OBJECT_0||!GetExitCodeThread(threads[i],&code)||code)ok=0;if(threads[i])CloseHandle(threads[i]);}CHECK("parallel-load-call-release",ok);
 HMODULE other=LoadLibraryW(L"C:\\tmp\\leaf.dll");CALL othermagic=(CALL)GetProcAddress(other,"magic");CHECK("full-path-distinguishes-same-basename",other&&other!=h&&othermagic&&othermagic()==177);if(other)CHECK("unload-other",FreeLibrary(other));
 CHECK("unload-last-reference",FreeLibrary(h)&&GetModuleHandleA("leaf.dll")==0);SetLastError(0);CHECK("stale-export-handle-rejected",GetProcAddress(h,"magic")==0&&GetLastError()==127);SetLastError(0);CHECK("double-free-rejected",!FreeLibrary(h)&&GetLastError()==6);
 HMODULE reload=LoadLibraryA("leaf.dll");CALL reset=(CALL)GetProcAddress(reload,"counter");CHECK("reload-fresh-image",reload&&reload!=h&&reset&&reset()==8);if(reload)CHECK("release-reload",FreeLibrary(reload));
 }
 HMODULE unicode=LoadLibraryW(L"C:\\tmp\\Größe😀.dll");CALL uf=(CALL)GetProcAddress(unicode,"magic");CHECK("unicode-file-load-and-handle",unicode&&uf&&uf()==177&&GetModuleHandleW(L"Größe😀.dll")==unicode);if(unicode)CHECK("unicode-unload",FreeLibrary(unicode)&&GetModuleHandleW(L"Größe😀.dll")==0);
 HMODULE ansi=LoadLibraryA("C:\\tmp\\\x80leaf.dll");CALL af=(CALL)GetProcAddress(ansi,"magic");CHECK("ansi-1252-file-load-and-handle",ansi&&af&&af()==177&&GetModuleHandleA("\x80leaf.dll")==ansi);if(ansi)CHECK("ansi-unload",FreeLibrary(ansi));
 int cycles=1;for(int i=0;i<200;i++){HMODULE h=LoadLibraryA("leaf.dll");CALL f=(CALL)GetProcAddress(h,"counter");if(!h||!f||f()!=8||!FreeLibrary(h)){cycles=0;break;}}CHECK("two-hundred-fresh-load-unload-cycles",cycles);
 HMODULE a=LoadLibraryW(L"C:\\Windows\\System32\\leaf.dll"),b=LoadLibraryW(L"C:\\tmp\\leaf.dll");if(a)FreeLibrary(a);HMODULE anew=LoadLibraryW(L"C:\\Windows\\System32\\leaf.dll"),oldest=LoadLibraryA("leaf.dll");CHECK("slot-reuse-keeps-first-loaded-order",a&&b&&anew&&anew!=a&&oldest==b&&GetModuleHandleA("leaf.dll")==b&&GetModuleHandleW(L"leaf.dll")==b);if(oldest)FreeLibrary(oldest);if(b)FreeLibrary(b);if(anew)FreeLibrary(anew);
 HMODULE native=GetModuleHandleA("kernel32.dll");CHECK("full-native-module-cache",LoadLibraryW(L"C:\\Windows\\System32\\kernel32.dll")==native);HMODULE shadow=LoadLibraryW(L"C:\\tmp\\kernel32.dll");CALL shadowmagic=(CALL)GetProcAddress(shadow,"magic");CHECK("explicit-same-name-file-has-own-code",shadow&&shadow!=native&&shadowmagic&&shadowmagic()==177);CHECK("native-first-loaded-cache-retained",LoadLibraryA("kernel32.dll")==native&&GetModuleHandleA("kernel32.dll")==native&&GetModuleHandleW(L"kernel32.dll")==native);if(shadow)CHECK("same-name-file-unload",FreeLibrary(shadow));
 SetLastError(0);CHECK("unsupported-flags-not-ignored",!LoadLibraryExW(L"leaf.dll",0,LOAD_LIBRARY_AS_DATAFILE)&&GetLastError()==50&&GetModuleHandleA("leaf.dll")==0);
 SetLastError(0);CHECK("file-handle-not-ignored",!LoadLibraryExW(L"leaf.dll",(HANDLE)1,0)&&GetLastError()==87);
 SetLastError(0);CHECK("missing-file",!LoadLibraryA("absent-leaf.dll")&&GetLastError()==126);
 SetLastError(0);CHECK("dllmain-explicitly-unsupported",!LoadLibraryA("entryleaf.dll")&&GetLastError()==50&&GetModuleHandleA("entryleaf.dll")==0);
 DWORD before=GetTickCount();HMODULE imported=LoadLibraryA("importleaf.dll");CALL tick=(CALL)GetProcAddress(imported,"magic");CHECK("already-pinned-import-called",imported&&tick&&(DWORD)((DWORD)tick()-before)<5000);if(imported)CHECK("unload-pinned-import-leaf",FreeLibrary(imported));
 SetLastError(0);CHECK("new-dependency-explicitly-unavailable",!LoadLibraryA("newdep.dll")&&GetLastError()==126&&GetModuleHandleA("newdep.dll")==0);
 SetLastError(0);CHECK("bad-export-rejected",!LoadLibraryA("badleaf.dll")&&GetLastError()==193&&GetModuleHandleA("badleaf.dll")==0);
 CHECK("pinned-system-module",LoadLibraryA("kernel32.dll")==GetModuleHandleA("kernel32.dll")&&FreeLibrary(GetModuleHandleA("kernel32.dll")));
 say(fail?"windyn-result=FAIL\n":"windyn-result=PASS\n");say("windyn-done\n");ExitProcess(fail?1:0);
}
