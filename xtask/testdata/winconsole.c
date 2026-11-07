#include <windows.h>
static HANDLE out;static int fail;static HWND(WINAPI*console_window)(void);
static void say(const char*s){DWORD n=0;while(s[n])n++;WriteFile(out,s,n,&n,0);}
#define CHECK(s,c) do{if(c)say("winconsole-ok " s "\n");else{say("winconsole-FAIL " s "\n");fail=1;}}while(0)
static DWORD WINAPI worker(void*arg){DWORD token=(DWORD)(ULONG_PTR)arg;SetLastError(token);return console_window()==0&&GetLastError()==token?0:1;}
void start(void){out=GetStdHandle(STD_OUTPUT_HANDLE);say("winconsole-start\n");console_window=(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetConsoleWindow");CHECK("getter-export",console_window!=0);if(console_window){SetLastError(0x123456);CHECK("no-win32-console-association",console_window()==0&&GetLastError()==0x123456);
 WNDCLASSA cls={0};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(0);cls.lpszClassName="thos-console-getter-test";ATOM atom=RegisterClassA(&cls);HWND window=CreateWindowExA(0,cls.lpszClassName,"ordinary application window",WS_OVERLAPPEDWINDOW,10,10,200,100,0,0,cls.hInstance,0);RECT rect;CHECK("ordinary-window-real",atom&&window&&GetClientRect(window,&rect)&&rect.right>0&&rect.bottom>0);SetLastError(0x654321);CHECK("ordinary-window-is-not-console",console_window()==0&&GetLastError()==0x654321);/* Process exit owns ordinary window cleanup. */
 HANDLE threads[4];for(int i=0;i<4;i++)threads[i]=CreateThread(0,0,worker,(void*)(ULONG_PTR)(0x7000+i),0,0);int ok=1;for(int i=0;i<4;i++){DWORD code=9;if(!threads[i]||WaitForSingleObject(threads[i],INFINITE)!=WAIT_OBJECT_0||!GetExitCodeThread(threads[i],&code)||code)ok=0;if(threads[i])CloseHandle(threads[i]);}CHECK("thread-last-error-preserved",ok);
 }say(fail?"winconsole-result=FAIL\n":"winconsole-result=PASS\n");say("winconsole-done\n");ExitProcess(fail?1:0);
}
