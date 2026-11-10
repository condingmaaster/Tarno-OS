#include <windows.h>
static HANDLE output;static int explicit_exit;
static void say(const char*s){DWORD n=0;while(s[n])n++;WriteFile(output,s,n,&n,0);}
static DWORD WINAPI worker(void*p){Sleep(250);say("winthread-worker-survived-main\n");if(explicit_exit)ExitThread(37);return 42;}
void start(void){output=GetStdHandle(STD_OUTPUT_HANDLE);const char*cmd=GetCommandLineA();for(int i=0;cmd[i];i++)if(cmd[i]=='X')explicit_exit=1;if(!CreateThread(0,0,worker,0,0,0))ExitProcess(99);say("winthread-main-exits-first\n");ExitThread(17);}
