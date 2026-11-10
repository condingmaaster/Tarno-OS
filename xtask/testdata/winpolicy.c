#include <windows.h>
static HANDLE out;static int fail;
static void say(const char*s){DWORD n=0;while(s[n])n++;WriteFile(out,s,n,&n,0);}
#define CHECK(s,c) do{if(c)say("winpolicy-ok " s "\n");else{say("winpolicy-FAIL " s "\n");fail=1;}}while(0)
typedef BOOL(WINAPI*POLICY)(DWORD);typedef HMODULE(WINAPI*LOADEXA)(LPCSTR,HANDLE,DWORD);typedef int(*CALL)(void);
static int value(HMODULE h){CALL f=(CALL)GetProcAddress(h,"magic");return f?f():-1;}
static int pick(const char*n,int expected){HMODULE h=LoadLibraryA(n);int ok=h&&value(h)==expected;if(h)ok=FreeLibrary(h)&&ok;return ok;}
static int pickex(const WCHAR*n,DWORD flags,int expected){HMODULE h=LoadLibraryExW(n,0,flags);int ok=h&&value(h)==expected;if(h)ok=FreeLibrary(h)&&ok;return ok;}
void start(void){out=GetStdHandle(STD_OUTPUT_HANDLE);const char*c=GetCommandLineA();int child=0;for(;*c;c++)if(c[0]=='c'&&c[1]=='h'&&c[2]=='i'&&c[3]=='l'&&c[4]=='d')child=1;
 if(child){int ok=pick("pick9.dll",100);say(ok?"winpolicy-child=PASS\n":"winpolicy-child=FAIL\n");ExitProcess(ok?42:1);}
 say("winpolicy-start\n");POLICY policy=(POLICY)GetProcAddress(GetModuleHandleA("kernel32.dll"),"SetDefaultDllDirectories");CHECK("real-policy-export",policy!=0);if(policy){
 CHECK("system32-policy",policy(LOAD_LIBRARY_SEARCH_SYSTEM32)&&pick("pick.dll",200));
 CHECK("application-policy",policy(LOAD_LIBRARY_SEARCH_APPLICATION_DIR)&&pick("pick2.dll",100));
 CHECK("default-order",policy(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)&&pick("pick3.dll",100));
 CHECK("explicit-application-overrides-default",policy(LOAD_LIBRARY_SEARCH_SYSTEM32)&&pickex(L"pick4.dll",LOAD_LIBRARY_SEARCH_APPLICATION_DIR,100));
 CHECK("explicit-system32-overrides-default",policy(LOAD_LIBRARY_SEARCH_APPLICATION_DIR)&&pickex(L"pick5.dll",LOAD_LIBRARY_SEARCH_SYSTEM32,200));
 CHECK("full-path-honoured",policy(LOAD_LIBRARY_SEARCH_SYSTEM32)&&pickex(L"C:\\tmp\\pick.dll",0,100));
 CHECK("full-path-dll-load-dir",pickex(L"C:\\tmp\\pick.dll",LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32,100));
 CHECK("cache-seed",policy(LOAD_LIBRARY_SEARCH_SYSTEM32));HMODULE first=LoadLibraryA("pick6.dll");CHECK("cache-keeps-first-loaded-module",first&&value(first)==200&&policy(LOAD_LIBRARY_SEARCH_APPLICATION_DIR)&&LoadLibraryA("pick6.dll")==first);if(first){CHECK("cache-release-two",FreeLibrary(first)&&FreeLibrary(first)&&GetModuleHandleA("pick6.dll")==0);}
 CHECK("valid-policy-before-errors",policy(LOAD_LIBRARY_SEARCH_SYSTEM32));SetLastError(0);CHECK("zero-policy-invalid",!policy(0)&&GetLastError()==87);SetLastError(0);CHECK("unknown-policy-invalid",!policy(1)&&GetLastError()==87);SetLastError(0);CHECK("dll-load-dir-not-default",!policy(LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)&&GetLastError()==87);CHECK("invalid-policy-preserves-selection",pick("pick7.dll",200));
 CHECK("empty-user-directory-policy",policy(LOAD_LIBRARY_SEARCH_USER_DIRS));SetLastError(0);CHECK("user-dirs-do-not-search-system-or-app",!LoadLibraryA("pick8.dll")&&GetLastError()==126);
 CHECK("parent-policy-for-isolation",policy(LOAD_LIBRARY_SEARCH_SYSTEM32));STARTUPINFOA si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof(si);char cmd[]="C:\\tmp\\policy.exe child";int started=CreateProcessA("C:\\tmp\\policy.exe",cmd,0,0,FALSE,0,0,0,&si,&pi);DWORD code=0;int isolated=started&&WaitForSingleObject(pi.hProcess,INFINITE)==WAIT_OBJECT_0&&GetExitCodeProcess(pi.hProcess,&code)&&code==42;CHECK("policy-is-process-local",isolated);if(started){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}CHECK("parent-policy-retained",pick("pick9.dll",200));
 LOADEXA exa=(LOADEXA)GetProcAddress(GetModuleHandleA("kernel32.dll"),"LoadLibraryExA");CHECK("ansi-loadex-export",exa!=0);if(exa){CHECK("ansi-policy-before-override",policy(LOAD_LIBRARY_SEARCH_SYSTEM32));HMODULE h=exa("pick10.dll",0,LOAD_LIBRARY_SEARCH_APPLICATION_DIR);CHECK("ansi-search-override-real-code",h&&value(h)==100);if(h)CHECK("ansi-module-release",FreeLibrary(h));SetLastError(0);CHECK("ansi-file-handle-rejected",!exa("pick10.dll",(HANDLE)1,0)&&GetLastError()==87);SetLastError(0);CHECK("ansi-resource-mode-rejected",!exa("pick10.dll",0,LOAD_LIBRARY_AS_DATAFILE)&&GetLastError()==50);}
 SetLastError(0);CHECK("dll-load-dir-needs-full-path",!LoadLibraryExW(L"pick10.dll",0,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)&&GetLastError()==87);SetLastError(0);CHECK("resource-mode-still-unsupported",!LoadLibraryExW(L"pick10.dll",0,LOAD_LIBRARY_AS_DATAFILE)&&GetLastError()==50);
 }
 say(fail?"winpolicy-result=FAIL\n":"winpolicy-result=PASS\n");say("winpolicy-done\n");ExitProcess(fail?1:0);
}
