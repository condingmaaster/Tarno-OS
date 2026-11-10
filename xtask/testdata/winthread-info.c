#define _WIN32_WINNT 0x0600
#include <windows.h>
static HANDLE output,gate;static int fail;static volatile LONG ready,done;
typedef DWORD(WINAPI*TID)(HANDLE);static TID id;
struct context{DWORD actual,code;int bad;};static struct context contexts[8];
static void say(const char*s){DWORD n=0;while(s[n])n++;WriteFile(output,s,n,&n,0);}
#define CHECK(s,c) do{if(c)say("winthread-info-ok " s "\n");else{say("winthread-info-FAIL " s "\n");fail=1;}}while(0)
static DWORD WINAPI worker(void*p){struct context*c=p;DWORD code=0;c->actual=GetCurrentThreadId();SetLastError(0x1234);if(id&&id(GetCurrentThread())!=c->actual)c->bad=1;if(!GetExitCodeThread(GetCurrentThread(),&code)||code!=STILL_ACTIVE||GetLastError()!=0x1234)c->bad=1;__atomic_add_fetch(&ready,1,__ATOMIC_RELEASE);if(WaitForSingleObject(gate,INFINITE)!=WAIT_OBJECT_0)c->bad=1;__atomic_add_fetch(&done,1,__ATOMIC_RELEASE);return c->code;}
static DWORD WINAPI explicit_worker(void*p){ExitThread((DWORD)(ULONG_PTR)p);return 99;}
static DWORD WINAPI fast_worker(void*p){return (DWORD)(ULONG_PTR)p;}
/* Win64 ULONG stack parameters occupy only the low 32 bits. Deliberately
 * dirty the unused half; a zero logical CreateFlags must still start. */
typedef LONG(WINAPI*NATIVE_CREATE)(HANDLE*,DWORD,void*,HANDLE,void*,void*,DWORD,ULONG_PTR,SIZE_T,SIZE_T,void*);
extern LONG WINAPI dirty_native(NATIVE_CREATE,HANDLE*,void*,void*,DWORD);
__asm__(".text\n.globl dirty_native\ndirty_native:\n"
 "mov %rcx,%rax\nmov 40(%rsp),%r10d\nsub $104,%rsp\n"
 "mov %r8,32(%rsp)\nmov %r9,40(%rsp)\n"
 "movabs $0xa5a5a5a500000000,%r11\nor %r10,%r11\nmov %r11,48(%rsp)\n"
 "movq $0,56(%rsp)\nmovq $0,64(%rsp)\nmovq $0,72(%rsp)\nmovq $0,80(%rsp)\n"
 "mov %rdx,%rcx\nmov $0x1fffff,%edx\nxor %r8d,%r8d\nmov $-1,%r9\n"
 "call *%rax\nadd $104,%rsp\nret\n");
void start(void){output=GetStdHandle(STD_OUTPUT_HANDLE);say("winthread-info-start\n");id=(TID)GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetThreadId");CHECK("thread-id-export",id!=0);DWORD main=GetCurrentThreadId();SetLastError(0x1234);CHECK("current-thread-pseudo-id",id&&id(GetCurrentThread())==main&&main!=0&&GetLastError()==0x1234);
 gate=CreateEventA(0,TRUE,FALSE,0);HANDLE threads[8];DWORD ids[8];for(int i=0;i<8;i++){contexts[i].code=0xf1230000U+i;threads[i]=CreateThread(0,0,worker,&contexts[i],0,&ids[i]);}ULONGLONG end=GetTickCount64()+5000;while(ready<8&&GetTickCount64()<end)Sleep(1);CHECK("eight-workers-running",ready==8);
 int active=1,identities=1;for(int i=0;i<8;i++){DWORD code=0;SetLastError(0x1234);if(!threads[i]||!GetExitCodeThread(threads[i],&code)||code!=STILL_ACTIVE||GetLastError()!=0x1234||WaitForSingleObject(threads[i],0)!=WAIT_TIMEOUT)active=0;if(!id||!threads[i]||!ids[i]||ids[i]==main||ids[i]!=contexts[i].actual||id(threads[i])!=ids[i]||contexts[i].bad)identities=0;for(int j=0;j<i;j++)if(ids[i]==ids[j])identities=0;}CHECK("actual-STILL_ACTIVE-and-unsignaled",active);CHECK("actual-unique-create-current-handle-identities",identities);
 CHECK("thread-handle-is-not-writable-event",threads[0]&&!SetEvent(threads[0])&&!ResetEvent(threads[0])&&WaitForSingleObject(threads[0],0)==WAIT_TIMEOUT);
 SetEvent(gate);int results=1;for(int i=0;i<8;i++){DWORD code=0;if(!threads[i]||WaitForSingleObject(threads[i],5000)!=WAIT_OBJECT_0||!GetExitCodeThread(threads[i],&code)||code!=contexts[i].code||contexts[i].bad||WaitForSingleObject(threads[i],0)!=WAIT_OBJECT_0)results=0;if(id&&id(threads[i])!=ids[i])results=0;}CHECK("actual-32bit-return-codes-and-stable-ids",results&&done==8);
 DWORD code=0x55555555;SetLastError(0);CHECK("event-is-not-thread",!GetExitCodeThread(gate,&code)&&GetLastError()==6&&code==0x55555555);if(id){SetLastError(0);CHECK("output-pointer-validated",threads[0]&&!GetExitCodeThread(threads[0],(void*)1)&&GetLastError()==998);SetLastError(0);CHECK("null-output-validated",threads[0]&&!GetExitCodeThread(threads[0],0)&&GetLastError()==87);}SetLastError(0);CHECK("high-handle-bits-rejected",threads[0]&&!GetExitCodeThread((HANDLE)((ULONG_PTR)threads[0]|0x100000000ULL),&code)&&GetLastError()==6);SetLastError(0);CHECK("id-type-validation",id&&!id(gate)&&GetLastError()==6);
 HANDLE old=threads[0];for(int i=0;i<8;i++)if(threads[i])CloseHandle(threads[i]);SetLastError(0);CHECK("closed-thread-handle-rejected",!GetExitCodeThread(old,&code)&&GetLastError()==6);
 HANDLE e=CreateThread(0,0,explicit_worker,(void*)0x89abcdefU,0,0);code=0;CHECK("explicit-ExitThread-status",e&&WaitForSingleObject(e,5000)==WAIT_OBJECT_0&&GetExitCodeThread(e,&code)&&code==0x89abcdefU);if(e)CloseHandle(e);
 HANDLE a=CreateThread(0,0,fast_worker,(void*)259,0,0);code=0;CHECK("returned-259-is-completed-not-running",a&&WaitForSingleObject(a,5000)==WAIT_OBJECT_0&&GetExitCodeThread(a,&code)&&code==259&&WaitForSingleObject(a,0)==WAIT_OBJECT_0);if(a)CloseHandle(a);
 SetLastError(0);CHECK("suspended-option-not-ignored",!CreateThread(0,0,fast_worker,0,CREATE_SUSPENDED,0)&&GetLastError()==50);SetLastError(0);CHECK("stack-option-not-ignored",!CreateThread(0,1024*1024,fast_worker,0,0,0)&&GetLastError()==50);SetLastError(0);CHECK("invalid-creation-flags",!CreateThread(0,0,fast_worker,0,2,0)&&GetLastError()==87);
 NATIVE_CREATE native=(NATIVE_CREATE)GetProcAddress(GetModuleHandleA("ntdll.dll"),"NtCreateThreadEx");HANDLE nh=0;code=0;
 LONG ns=native?dirty_native(native,&nh,fast_worker,(void*)0x76543210U,0):(LONG)0xc0000002U;
 CHECK("native-DWORD-stack-padding-ignored",ns==0&&nh&&WaitForSingleObject(nh,5000)==WAIT_OBJECT_0&&GetExitCodeThread(nh,&code)&&code==0x76543210U);if(nh)CloseHandle(nh);
 nh=0;ns=native?dirty_native(native,&nh,fast_worker,0,1):0;
 CHECK("native-real-create-flags-still-rejected",(DWORD)ns==0xc0000002U&&!nh);
 if(gate)CloseHandle(gate);say(fail?"winthread-info-result=FAIL\n":"winthread-info-result=PASS\n");say("winthread-info-done\n");ExitProcess(fail?1:0);
}
