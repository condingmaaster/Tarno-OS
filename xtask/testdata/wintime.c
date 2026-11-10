#include <windows.h>
static HANDLE out;static int fail;
static void say(const char*s){DWORD n=0;while(s[n])n++;WriteFile(out,s,n,&n,0);}
#define CHECK(s,c) do{if(c)say("wintime-ok " s "\n");else{say("wintime-FAIL " s "\n");fail=1;}}while(0)
static int eq(const WCHAR*a,const WCHAR*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static DWORD WINAPI worker(void*p){SYSTEMTIME t={0};t.wHour=13;t.wMinute=4;t.wSecond=9;WCHAR b[32];for(int i=0;i<1000;i++){SetLastError(0xabc);if(GetTimeFormatW(0x409,0,&t,0,b,32)!=11||!eq(b,L"1:04:09 PM")||GetLastError()!=0xabc)return 1;}return 0;}
void start(void){out=GetStdHandle(STD_OUTPUT_HANDLE);say("wintime-start\n");SYSTEMTIME t={0xffff,0xffff,0xffff,0xffff,13,4,9,999};WCHAR b[128];for(int i=0;i<128;i++)b[i]=0x5555;
 SetLastError(0x1234);CHECK("query-count-and-lasterror",GetTimeFormatW(0x407,0,&t,0,0,0)==9&&GetLastError()==0x1234);
 SetLastError(0);CHECK("short-buffer-untouched",!GetTimeFormatW(0x407,0,&t,0,b,8)&&GetLastError()==122&&b[0]==0x5555);
 SetLastError(0x1234);CHECK("german-default-and-date-ignored",GetTimeFormatW(0x407,0,&t,0,b,9)==9&&eq(b,L"13:04:09")&&b[9]==0x5555&&GetLastError()==0x1234);
 CHECK("english-default",GetTimeFormatW(0x409,0,&t,0,b,128)==11&&eq(b,L"1:04:09 PM"));
 CHECK("force24-keeps-marker",GetTimeFormatW(0x409,8,&t,0,b,128)==12&&eq(b,L"13:04:09 PM"));
 CHECK("tokens-and-repeated-tokens",GetTimeFormatW(0x409,0,&t,L"hhh HH mmm sss ttt",b,128)==15&&eq(b,L"01 13 04 09 PM"));
 CHECK("quoted-unicode",GetTimeFormatW(0x409,0,&t,L"'o''clock \xD83D\xDE00 'HH",b,128)==14&&eq(b,L"o'clock \xD83D\xDE00 13"));
 CHECK("unmatched-quote-best-effort",GetTimeFormatW(0x409,0,&t,L"'HH:mm",b,128)==6&&eq(b,L"HH:mm"));
 CHECK("empty-quoted-literal",GetTimeFormatW(0x409,0,&t,L"''",b,128)==1&&!b[0]);
 CHECK("escaped-quote-enclosed",GetTimeFormatW(0x409,0,&t,L"''''",b,128)==2&&eq(b,L"'"));
 CHECK("empty-format",GetTimeFormatW(0x409,0,&t,L"",b,128)==1&&!b[0]);
 CHECK("default-aliases",GetTimeFormatW(0x400,0,&t,0,b,128)==9&&eq(b,L"13:04:09")&&GetTimeFormatW(0x800,0,&t,0,b,128)==9&&GetTimeFormatW(0,0,&t,0,b,128)==9);
 CHECK("metadata-flags",GetTimeFormatW(0x409,LOCALE_NOUSEROVERRIDE|LOCALE_USE_CP_ACP,&t,0,b,128)==11&&eq(b,L"1:04:09 PM"));
 SetLastError(0);CHECK("custom-format-nooverride-invalid",!GetTimeFormatW(0x409,LOCALE_NOUSEROVERRIDE,&t,L"HH",b,128)&&GetLastError()==1004);
 const WCHAR*results[]={L"1:04:09 PM",L"1 PM",L"1:04 PM",L"1 PM",L"1:04:09",L"1",L"1:04",L"1",L"13:04:09 PM",L"13 PM",L"13:04 PM",L"13 PM",L"13:04:09",L"13",L"13:04",L"13"};
 for(unsigned f=0;f<16;f++){int n=0;while(results[f][n])n++;CHECK("all-omission-combinations",GetTimeFormatW(0x409,f,&t,0,b,128)==n+1&&eq(b,results[f]));}
 CHECK("omission-removes-multiple-separators",GetTimeFormatW(0x409,2,&t,L"h.@:m.@:s.@:tt",b,128)==8&&eq(b,L"1.@:4PM"));
 CHECK("omission-exact-small-buffer",GetTimeFormatW(0x409,1,&t,L"HH' long separator 'mm:ss",b,3)==3&&eq(b,L"13"));
 SetLastError(0);CHECK("unknown-flags",!GetTimeFormatW(0x409,16,&t,0,b,128)&&GetLastError()==1004);
 SetLastError(0);CHECK("unknown-locale",!GetTimeFormatW(0x411,0,&t,0,b,128)&&GetLastError()==87);
 SetLastError(0);CHECK("negative-capacity",!GetTimeFormatW(0x409,0,&t,0,b,-1)&&GetLastError()==87);
 SetLastError(0);CHECK("null-positive-output",!GetTimeFormatW(0x409,0,&t,0,0,30)&&GetLastError()==122);
 WORD *values[]={&t.wHour,&t.wMinute,&t.wSecond,&t.wMilliseconds};WORD bad[]={24,60,60,1000};for(int i=0;i<4;i++){WORD saved=*values[i];*values[i]=bad[i];SetLastError(0);CHECK("invalid-time-component",!GetTimeFormatW(0x409,0,&t,0,b,128)&&GetLastError()==87);*values[i]=saved;}
 t.wHour=0;CHECK("midnight",GetTimeFormatW(0x409,0,&t,0,b,128)==12&&eq(b,L"12:04:09 AM"));t.wHour=12;CHECK("noon",GetTimeFormatW(0x409,0,&t,0,b,128)==12&&eq(b,L"12:04:09 PM"));
 SYSTEMTIME before,after;GetLocalTime(&before);CHECK("current-clock-not-constant",GetTimeFormatW(0x407,0,0,L"HH:mm:ss",b,128)==9);GetLocalTime(&after);WCHAR a[32],z[32];GetTimeFormatW(0x407,0,&before,L"HH:mm:ss",a,32);GetTimeFormatW(0x407,0,&after,L"HH:mm:ss",z,32);CHECK("current-localtime-provider",eq(b,a)||eq(b,z));
 HANDLE threads[8];for(int i=0;i<8;i++)threads[i]=CreateThread(0,0,worker,0,0,0);int ok=1;for(int i=0;i<8;i++){DWORD code=9;if(!threads[i]||WaitForSingleObject(threads[i],INFINITE)!=WAIT_OBJECT_0||!GetExitCodeThread(threads[i],&code)||code)ok=0;if(threads[i])CloseHandle(threads[i]);}CHECK("parallel-format-lasterror",ok);
 say(fail?"wintime-result=FAIL\n":"wintime-result=PASS\n");say("wintime-done\n");ExitProcess(fail?1:0);
}
