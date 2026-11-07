/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
static HANDLE output;static int failed;
static void say(const char*s){DWORD n=0;while(s[n])n++;WriteFile(output,s,n,&n,0);}
#define CHECK(s,c) do{if(c)say("winresource-ok " s "\n");else{say("winresource-FAIL " s "\n");failed=1;}}while(0)
static int payload(HMODULE module,HRSRC info){if(!info||SizeofResource(module,info)!=4)return 0;HGLOBAL h=LoadResource(module,info);unsigned char*p=LockResource(h);return p&&p[0]==0x34&&p[1]==0x12&&p[2]==0xcd&&p[3]==0xab;}
static DWORD WINAPI worker(void*p){HMODULE module=p;for(int n=0;n<200;n++){SetLastError(0x1234);HRSRC info=FindResourceW(module,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10));if(!payload(module,info)||GetLastError()!=0x1234)return 1;}return 0;}
void start(void){output=GetStdHandle(STD_OUTPUT_HANDLE);say("winresource-start\n");HMODULE main=GetModuleHandleW(0);SetLastError(0x1234);HRSRC info=FindResourceW(0,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10));CHECK("actual-exe-resource-and-default-language",payload(0,info)&&GetLastError()==0x1234);CHECK("explicit-main-same-resource",FindResourceW(main,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10))==info);
 CHECK("decimal-resource-ids",FindResourceW(0,L"#101",L"#10")==info&&FindResourceA(0,"#101","#10")==info);
 HRSRC empty=FindResourceW(0,MAKEINTRESOURCEW(102),MAKEINTRESOURCEW(10));SetLastError(0x1234);CHECK("zero-byte-resource-is-valid",empty&&SizeofResource(0,empty)==0&&GetLastError()==0x1234&&LoadResource(0,empty)&&LockResource(LoadResource(0,empty)));
 HRSRC named=FindResourceW(0,L"payload",L"custom");unsigned char*data=LockResource(LoadResource(0,named));CHECK("actual-named-data",named&&SizeofResource(0,named)==4&&data&&data[0]==0x78&&data[1]==0x56&&data[2]==0xbc&&data[3]==0x9a);
 HRSRC unicode=FindResourceW(0,L"Grüße",L"CUSTOM");CHECK("unicode-name",unicode&&SizeofResource(0,unicode)==2);CHECK("ansi-cp1252-name",FindResourceA(0,"Gr\xfc\xdf" "e","CUSTOM")==unicode);
 SetLastError(0);CHECK("unknown-type",!FindResourceW(0,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(999))&&GetLastError()==1813);
 SetLastError(0);CHECK("unknown-name",!FindResourceW(0,MAKEINTRESOURCEW(999),MAKEINTRESOURCEW(10))&&GetLastError()==1814);
 SetLastError(0);CHECK("unknown-module-no-dereference",!FindResourceW((HMODULE)1,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10))&&GetLastError()==6);
 SetLastError(0);CHECK("invalid-decimal-not-truncated",!FindResourceW(0,L"#65536",MAKEINTRESOURCEW(10))&&GetLastError()==87);
 SetLastError(0);CHECK("false-resource-info-rejected",!LoadResource(0,(HRSRC)((char*)info+1))&&GetLastError()==1812);
 CHECK("false-resource-data-rejected",!LockResource((HGLOBAL)((char*)LoadResource(0,info)+1)));
 SetLastError(0x1234);CHECK("obsolete-FreeResource-false-no-free",!FreeResource(LoadResource(0,info))&&GetLastError()==0x1234&&payload(0,info));
 HMODULE dll=LoadLibraryW(L"C:\\Windows\\System32\\resourceleaf.dll");CHECK("actual-leaf-module-loaded",dll!=0);if(dll){HRSRC di=FindResourceW(dll,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10));HGLOBAL dh=LoadResource(dll,di);CHECK("actual-dll-resource",payload(dll,di)&&di!=info);SetLastError(0);CHECK("cross-module-handle-rejected",!LoadResource(main,di)&&GetLastError()==1812);
 HANDLE threads[8];for(int i=0;i<8;i++)threads[i]=CreateThread(0,0,worker,dll,0,0);int ok=1;for(int i=0;i<8;i++){DWORD code=99;if(!threads[i]||WaitForSingleObject(threads[i],10000)!=WAIT_OBJECT_0||!GetExitCodeThread(threads[i],&code)||code)ok=0;if(threads[i])CloseHandle(threads[i]);}CHECK("eight-parallel-borrowed-resource-readers",ok);
 CHECK("leaf-module-unloaded",FreeLibrary(dll));SetLastError(0);CHECK("stale-module-rejected",!FindResourceW(dll,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10))&&GetLastError()==6);CHECK("stale-data-not-dereferenced",!LockResource(dh));SetLastError(0);CHECK("stale-resource-size-rejected",!SizeofResource(dll,di)&&GetLastError()==6);
 }
 say(failed?"winresource-result=FAIL\n":"winresource-result=PASS\n");say("winresource-done\n");ExitProcess(failed?1:0);
}
