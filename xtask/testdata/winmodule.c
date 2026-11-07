// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static int failures;
#define CHECK(n,c) do { if(c) printf("winmodule-ok %s\n",n); else {printf("winmodule-FAIL %s error=%lu\n",n,(unsigned long)GetLastError()); failures++;} } while(0)
/* Independent fixture decoder: the runtime conversion APIs are a separate
   compatibility surface and must not decide the expected module pathname. */
static void expected_wide(const char *s,WCHAR *d) {
 while(*s){unsigned c=(unsigned char)*s++;
  if(c>=0xf0){c=((c&7)<<18)|(((unsigned char)s[0]&63)<<12)|(((unsigned char)s[1]&63)<<6)|((unsigned char)s[2]&63);s+=3;}
  else if(c>=0xe0){c=((c&15)<<12)|(((unsigned char)s[0]&63)<<6)|((unsigned char)s[1]&63);s+=2;}
  else if(c>=0xc0){c=((c&31)<<6)|((unsigned char)*s++&63);}
  if(c>=0x10000){c-=0x10000;*d++=0xd800+(c>>10);*d++=0xdc00+(c&1023);}else *d++=c;
 }*d=0;
}
static void check_module(HMODULE m,const char *expected) {
 char a[2048]; WCHAR w[2048]; DWORD n=GetModuleFileNameA(m,a,sizeof a);
 printf("winmodule-path=%s expected=%s\n",n?a:"<none>",expected);
 CHECK("ansi-path",n==strlen(expected) && strcmp(a,expected)==0);
 DWORD wn=GetModuleFileNameW(m,w,2048); WCHAR ew[2048];
 expected_wide(expected,ew);
 CHECK("wide-path",wn==wcslen(ew) && wcscmp(w,ew)==0);
 char tiny[5]={'!','!','!','!','!'}; SetLastError(0);
 CHECK("ansi-truncated",GetModuleFileNameA(m,tiny,4)==4 && GetLastError()==122 && tiny[3]==0 && tiny[4]=='!' && memcmp(tiny,expected,3)==0);
 WCHAR tw[5]={L'!',L'!',L'!',L'!',L'!'}; SetLastError(0);
 CHECK("wide-truncated",GetModuleFileNameW(m,tw,4)==4 && GetLastError()==122 && tw[3]==0 && tw[4]==L'!' && wmemcmp(tw,ew,3)==0);
 tiny[0]=tiny[1]='!';SetLastError(0);CHECK("ansi-one",GetModuleFileNameA(m,tiny,1)==1 && tiny[0]==0 && tiny[1]=='!' && GetLastError()==122);
 tw[0]=tw[1]=L'!';SetLastError(0);CHECK("wide-one",GetModuleFileNameW(m,tw,1)==1 && tw[0]==0 && tw[1]==L'!' && GetLastError()==122);
 tiny[0]='!';SetLastError(54321);CHECK("ansi-zero",GetModuleFileNameA(m,tiny,0)==0 && tiny[0]=='!' && GetLastError()==54321);
 tw[0]=L'!';SetLastError(54321);CHECK("wide-zero",GetModuleFileNameW(m,tw,0)==0 && tw[0]==L'!' && GetLastError()==54321);
 if(n){a[0]='!';CHECK("ansi-exact",GetModuleFileNameA(m,a,n)==n && a[n-1]==0 && GetLastError()==122);CHECK("ansi-fit",GetModuleFileNameA(m,a,n+1)==n && !strcmp(a,expected));}
 if(wn){w[0]=L'!';CHECK("wide-exact",GetModuleFileNameW(m,w,wn)==wn && w[wn-1]==0 && GetLastError()==122);CHECK("wide-fit",GetModuleFileNameW(m,w,wn+1)==wn && !wcscmp(w,ew));}
}
int main(int argc,char **argv){
 const char *expected=argc>1?argv[1]:"C:\\winmodule.exe";
 char recorded[2048];DWORD got=0;HANDLE file=CreateFileA("C:\\tmp\\module-expected.txt",GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
 if(file!=INVALID_HANDLE_VALUE){if(ReadFile(file,recorded,sizeof recorded-1,&got,0)){recorded[got]=0;expected=recorded;}CloseHandle(file);}

 check_module(0,expected);check_module(GetModuleHandleA(0),expected);
 HMODULE h=GetModuleHandleA("kernel32.dll"); CHECK("kernel-handle",h!=0);
 check_module(h,"C:\\Windows\\System32\\KERNEL32.DLL");
 h=GetModuleHandleA("kernel32x.dll");CHECK("extension-handle",h!=0);check_module(h,"C:\\Windows\\System32\\kernel32x.dll");
 char a[8]="SENT";WCHAR w[8]=L"SENT";SetLastError(0);
 CHECK("invalid-ansi",GetModuleFileNameA((HMODULE)(ULONG_PTR)0x12345,a,8)==0 && GetLastError()==126 && !strcmp(a,"SENT"));SetLastError(0);
 CHECK("invalid-wide",GetModuleFileNameW((HMODULE)(ULONG_PTR)0x12345,w,8)==0 && GetLastError()==126 && !wcscmp(w,L"SENT"));
 printf("winmodule-failures=%d\n",failures);puts("winmodule-done");return failures?1:0;
}
