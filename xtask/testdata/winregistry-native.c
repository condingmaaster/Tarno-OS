/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
typedef struct{USHORT len,max;WCHAR*buf;}US;
typedef struct{ULONG len;HANDLE root;US*name;ULONG flags;void*security;void*qos;}OA;
typedef LONG(WINAPI*CREATE)(HANDLE*,DWORD,OA*,ULONG,US*,ULONG,ULONG*);
typedef LONG(WINAPI*OPEN)(HANDLE*,DWORD,OA*);
typedef LONG(WINAPI*SET)(HANDLE,US*,ULONG,ULONG,void*,ULONG);
typedef LONG(WINAPI*QUERY)(HANDLE,US*,ULONG,void*,ULONG,ULONG*);
typedef LONG(WINAPI*ONE)(HANDLE);
typedef LONG(WINAPI*ENUM)(HANDLE,ULONG,ULONG,void*,ULONG,ULONG*);static ENUM enumkey,enumvalue;
static CREATE create;static OPEN openkey;static SET set;static QUERY query;static ONE closekey,del;
static HANDLE output;static int fail;static BYTE buffer[1024];
#define AV ((LONG)0xc0000005)
#define INVALID ((LONG)0xc000000d)
#define BADHANDLE ((LONG)0xc0000008)
#define MISSING ((LONG)0xc0000034)
#define UNSUPPORTED ((LONG)0xc00000bb)
static void say(const char*s){DWORD n=0;while(s[n])n++;WriteFile(output,s,n,&n,0);}
#define CHECK(s,c) do{if(c)say("winregistry-native-ok " s "\n");else{say("winregistry-native-FAIL " s "\n");fail=1;}}while(0)
static US us(WCHAR*s){USHORT n=0;while(s[n])n++;US a={n*2,n*2,s};return a;}
static LONG read(HANDLE h,US*n,DWORD*value){ULONG bytes=0;LONG r=query(h,n,2,buffer,sizeof(buffer),&bytes);if(!r&&bytes==16&&*(ULONG*)(buffer+4)==REG_DWORD&&*(ULONG*)(buffer+8)==4)*value=*(DWORD*)(buffer+12);return r;}
/* Argument5 DataSize is ULONG: deliberately poison its unused upper half. */
extern LONG WINAPI dirty_size(SET,HANDLE,US*,DWORD*);
__asm__(".text\n.globl dirty_size\ndirty_size:\nmov %rcx,%rax\nsub $56,%rsp\nmov %r9,32(%rsp)\nmovabs $0xa5a5a5a500000004,%r10\nmov %r10,40(%rsp)\nmov %rdx,%rcx\nmov %r8,%rdx\nxor %r8d,%r8d\nmov $4,%r9d\ncall *%rax\nadd $56,%rsp\nret\n");
void start(void){output=GetStdHandle(STD_OUTPUT_HANDLE);say("winregistry-native-start\n");HMODULE m=GetModuleHandleA("ntdll.dll");create=(CREATE)GetProcAddress(m,"NtCreateKey");openkey=(OPEN)GetProcAddress(m,"NtOpenKey");set=(SET)GetProcAddress(m,"NtSetValueKey");query=(QUERY)GetProcAddress(m,"NtQueryValueKey");enumkey=(ENUM)GetProcAddress(m,"NtEnumerateKey");enumvalue=(ENUM)GetProcAddress(m,"NtEnumerateValueKey");closekey=(ONE)GetProcAddress(m,"NtClose");del=(ONE)GetProcAddress(m,"NtDeleteKey");CHECK("native-exports",create&&openkey&&set&&query&&closekey&&del&&enumkey&&enumvalue);if(!(create&&openkey&&set&&query&&closekey&&del&&enumkey&&enumvalue))goto done;
 US path=us(L"\\Registry\\User\\THOSNativeFixture\\Test");OA oa={sizeof(OA),0,&path,0x40,0,0};HANDLE h=0;ULONG disposition=0;CHECK("actual-key-created",create(&h,0,&oa,0,0,0,&disposition)==0&&h&&disposition==1);if(!h)goto done;
 US empty={0,0,0},ascii=us(L"Plain"),unicode=us(L"Gr\x00f6\x00df" L"e\xd83d\xde00"),collision=us(L"Gr??e??");DWORD data=0x12345678,out=0;CHECK("default-value-set",set(h,&empty,0,REG_DWORD,&data,4)==0);CHECK("unicode-value-set",set(h,&unicode,0,REG_DWORD,&data,4)==0);CHECK("unicode-no-lossy-ASCII-alias",read(h,&collision,&out)==MISSING);CHECK("unicode-roundtrip",read(h,&unicode,&out)==0&&out==data);
 CHECK("ULONG-stack-padding-ignored",dirty_size(set,h,&ascii,&data)==0&&read(h,&ascii,&out)==0&&out==data);
 DWORD badvalue=0xbadbad;US invalid={2,2,(void*)1};CHECK("bad-name-pointer-refused",set(h,&invalid,0,REG_DWORD,&badvalue,4)==AV);CHECK("bad-name-keeps-default",read(h,&empty,&out)==0&&out==data);US malformed=ascii;malformed.max=malformed.len-2;CHECK("length-exceeds-maximum-refused",set(h,&malformed,0,REG_DWORD,&data,4)==INVALID);malformed=ascii;malformed.len--;CHECK("odd-UTF16-byte-count-refused",set(h,&malformed,0,REG_DWORD,&data,4)==INVALID);
 CHECK("handle-upper-bits-refused",set((HANDLE)((ULONG_PTR)h|0x100000000ULL),&ascii,0,REG_DWORD,&data,4)==BADHANDLE);CHECK("query-result-pointer-validated",query(h,&ascii,2,buffer,sizeof(buffer),(void*)1)==AV);CHECK("query-output-pointer-validated",query(h,&ascii,2,(void*)1,sizeof(buffer),&disposition)==AV);
 static WCHAR longa[302],longb[302];for(int i=0;i<300;i++)longa[i]=longb[i]='x';longa[299]='A';longb[299]='B';US na=us(longa),nb=us(longb);DWORD other=0x87654321;set(h,&na,0,REG_DWORD,&data,4);set(h,&nb,0,REG_DWORD,&other,4);CHECK("long-counted-names-not-truncated",read(h,&na,&out)==0&&out==data&&read(h,&nb,&out)==0&&out==other);
 int found=0;for(ULONG index=0;index<8;index++){ULONG bytes=0;LONG r=enumvalue(h,index,0,buffer,sizeof(buffer),&bytes);if(r)break;if(*(WCHAR*)(buffer+12)=='g'&&*(WCHAR*)(buffer+16)==0x00f6){found=(*(ULONG*)(buffer+8)==unicode.len&&bytes==12+unicode.len);}}CHECK("enumerated-Unicode-value-byte-count",found);
 US sub=us(L"Child-\x76ee\x5f55\xd83d\xde00");OA rel={sizeof(OA),h,&sub,0,0,0};HANDLE child=0;CHECK("relative-key-created",create(&child,0,&rel,0,0,0,&disposition)==0&&child);if(child){ULONG bytes=0;CHECK("enumerated-Unicode-key-byte-count",enumkey(h,0,0,buffer,sizeof(buffer),&bytes)==0&&*(ULONG*)(buffer+12)==sub.len&&bytes==16+sub.len);del(child);closekey(child);}HANDLE untouched=(HANDLE)0x55555555;CHECK("open-output-refused",openkey((void*)1,0,&oa)==AV);CHECK("create-disposition-refused-before-mutation",create(&untouched,0,&rel,0,0,0,(void*)1)==AV&&untouched==(HANDLE)0x55555555);child=0;CHECK("invalid-output-created-no-key",openkey(&child,0,&rel)==MISSING);if(child){del(child);closekey(child);}
 OA bad=oa;bad.len=24;CHECK("object-attributes-size-refused",openkey(&untouched,0,&bad)==INVALID);bad=oa;bad.root=(HANDLE)((ULONG_PTR)h|0x100000000ULL);bad.name=&sub;CHECK("root-handle-upper-bits-refused",openkey(&untouched,0,&bad)==BADHANDLE);
 child=0;LONG option=create(&child,0,&rel,0,0,1,&disposition);CHECK("unsupported-volatile-not-faked",option==UNSUPPORTED);if(child){del(child);closekey(child);}del(h);closekey(h);
 done:say(fail?"winregistry-native-result=FAIL\n":"winregistry-native-result=PASS\n");say("winregistry-native-done\n");ExitProcess(fail?1:0);
}
