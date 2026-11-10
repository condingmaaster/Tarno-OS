/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
typedef struct{USHORT len,max;WCHAR*buf;}US;typedef struct{ULONG len;HANDLE root;US*name;ULONG flags;void*security;void*qos;}OA;
typedef LONG(WINAPI*CREATE)(HANDLE*,DWORD,OA*,ULONG,US*,ULONG,ULONG*);typedef LONG(WINAPI*SET)(HANDLE,US*,ULONG,ULONG,void*,ULONG);typedef LONG(WINAPI*QUERY)(HANDLE,US*,ULONG,void*,ULONG,ULONG*);typedef LONG(WINAPI*ONE)(HANDLE);typedef LONG(WINAPI*NOTIFY)(HANDLE,HANDLE,void*,void*,void*,ULONG,BOOL,void*,ULONG,BOOL);
static BYTE fill[1024*1024],replacement[65536],result[65548],before[4096],after[4096];static HANDLE output;static int fail;
static void say(const char*s){DWORD n=0;while(s[n])n++;WriteFile(output,s,n,&n,0);}
#define CHECK(s,c) do{if(c)say("winregistry-io-ok " s "\n");else{say("winregistry-io-FAIL " s "\n");fail=1;}}while(0)
static US us(WCHAR*s){USHORT n=0;while(s[n])n++;US a={n*2,n*2,s};return a;}
static DWORD disk(BYTE*b){DWORD n=0;HANDLE h=CreateFileA("C:\\etc\\thos\\registry\\user.hiv",GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);if(h==INVALID_HANDLE_VALUE)return 0;BOOL ok=ReadFile(h,b,4096,&n,0);CloseHandle(h);return ok?n:0;}
static long long syscall3(long long n,long long a,long long b,long long c){long long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");return r;}
static int same(BYTE*a,BYTE*b,DWORD n){for(DWORD i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
void start(void){output=GetStdHandle(STD_OUTPUT_HANDLE);say("winregistry-io-start\n");HMODULE m=GetModuleHandleA("ntdll.dll");CREATE create=(CREATE)GetProcAddress(m,"NtCreateKey");SET set=(SET)GetProcAddress(m,"NtSetValueKey");QUERY query=(QUERY)GetProcAddress(m,"NtQueryValueKey");ONE closekey=(ONE)GetProcAddress(m,"NtClose"),del=(ONE)GetProcAddress(m,"NtDeleteKey");NOTIFY notify=(NOTIFY)GetProcAddress(m,"NtNotifyChangeKey");if(!(create&&set&&query&&closekey&&del&&notify)){CHECK("actual-native-exports",0);goto done;}
 US path=us(L"\\Registry\\User\\THOSNativeFixture\\IO"),name=us(L"Value");OA oa={sizeof(OA),0,&path,0x40,0,0};HANDLE key=0,event=0;ULONG disposition=0,bytes=0;DWORD old=0x11223344;CHECK("seed-real-registry-value",create(&key,0,&oa,0,0,0,&disposition)==0&&key&&set(key,&name,0,REG_DWORD,&old,4)==0);if(!key)goto done;
 DWORD original=disk(before);CHECK("initial-hive-on-disk",original>0);event=CreateEventA(0,TRUE,FALSE,0);CHECK("actual-change-watch",event&&notify(key,event,0,0,0,4,FALSE,0,0,TRUE)==0);
 HANDLE file=CreateFileA("C:\\tmp\\registry-disk-full.bin",GENERIC_READ|GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);CHECK("private-filler-created",file!=INVALID_HANDLE_VALUE);if(file==INVALID_HANDLE_VALUE)goto cleanup;
 for(unsigned i=0;i<sizeof(fill);i++)fill[i]='F';unsigned chunks=0;DWORD count=0;while(WriteFile(file,fill,sizeof(fill),&count,0)&&count==sizeof(fill)&&chunks<1024){chunks++;if(chunks%64==0)say("winregistry-io-fill-progress\n");}
 CHECK("actual-disk-space-exhausted",chunks>1&&chunks<1024);unsigned tail=0;while(WriteFile(file,fill,1024,&count,0)&&count==1024&&tail<1024)tail++;CHECK("small-block-remainder-exhausted",tail<1024);
 ULONGLONG space[15],later[15];CHECK("real-ext2-free-space-exhausted",syscall3(137,(long long)"/",(long long)space,0)==0&&space[0]==0xef53&&space[3]<4);
 for(unsigned i=0;i<sizeof(replacement);i++)replacement[i]=(BYTE)(i*31+7);LONG status=set(key,&name,0,REG_BINARY,replacement,sizeof(replacement));CHECK("filesystem-failure-is-not-success",status==(LONG)0xc0000185);CHECK("failed-write-keeps-live-value",query(key,&name,2,result,sizeof(result),&bytes)==0&&bytes==16&&*(ULONG*)(result+4)==REG_DWORD&&*(DWORD*)(result+12)==old);DWORD remaining=disk(after);CHECK("failed-write-keeps-hive-bytes",remaining==original&&same(before,after,original));CHECK("failed-write-does-not-notify",WaitForSingleObject(event,0)==WAIT_TIMEOUT);CHECK("failed-hive-allocation-returns-blocks",syscall3(137,(long long)"/",(long long)later,0)==0&&later[3]==space[3]);
 CloseHandle(file);CHECK("filler-space-reclaimed",DeleteFileA("C:\\tmp\\registry-disk-full.bin"));CHECK("retry-really-commits",set(key,&name,0,REG_BINARY,replacement,sizeof(replacement))==0&&query(key,&name,2,result,sizeof(result),&bytes)==0&&bytes==sizeof(result)&&*(ULONG*)(result+4)==REG_BINARY&&same(result+12,replacement,sizeof(replacement)));CHECK("committed-change-notifies",WaitForSingleObject(event,0)==WAIT_OBJECT_0);
 cleanup:if(event)CloseHandle(event);if(key){del(key);closekey(key);}
 done:say(fail?"winregistry-io-result=FAIL\n":"winregistry-io-result=PASS\n");say("winregistry-io-done\n");ExitProcess(fail?1:0);
}
