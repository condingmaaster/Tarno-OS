// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static int failures;
#define CHECK(n,c) do { if(c) printf("winunicode-ok %s\n",n); else {printf("winunicode-FAIL %s error=%lu\n",n,(unsigned long)GetLastError()); failures++;} } while(0)
int main(void){
 unsigned lead_pages[]={1252,437,28591,65001};int lead_ok=1;
 for(unsigned p=0;p<4;p++)for(unsigned b=0;b<256;b++)if(IsDBCSLeadByteEx(lead_pages[p],(BYTE)b))lead_ok=0;
 CHECK("leadbyte-supported-non-dbcs-pages",lead_ok);
 unsigned aliases[]={CP_ACP,CP_OEMCP,CP_THREAD_ACP};lead_ok=1;
 for(unsigned p=0;p<3;p++)for(unsigned b=0;b<256;b++)if(IsDBCSLeadByteEx(aliases[p],(BYTE)b))lead_ok=0;
 CHECK("leadbyte-aliases",lead_ok);lead_ok=1;
 for(unsigned b=0;b<256;b++)if(IsDBCSLeadByte((BYTE)b))lead_ok=0;CHECK("leadbyte-default-acp",lead_ok);
 SetLastError(56789);CHECK("leadbyte-utf8-is-not-dbcs",!IsDBCSLeadByteEx(CP_UTF8,0xf0)&&GetLastError()==56789);
 SetLastError(0);CHECK("leadbyte-unknown-codepage",!IsDBCSLeadByteEx(999999,0x81)&&GetLastError()==87);
 SetLastError(0);CHECK("leadbyte-unsupported-dbcs-codepage",!IsValidCodePage(932)&&!IsDBCSLeadByteEx(932,0x81)&&GetLastError()==87);
 CPINFO info;memset(&info,0x55,sizeof info);SetLastError(56789);
 CHECK("cpinfo-utf8",GetCPInfo(CP_UTF8,&info) && info.MaxCharSize==4 && GetLastError()==56789);
 const WCHAR largest[]={0xdbff,0xdfff};char encoded[4]={0};
 CHECK("cpinfo-real-buffer",info.MaxCharSize>0 && info.MaxCharSize<=4 && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,largest,2,encoded,(int)info.MaxCharSize,0,0)==4 && !memcmp(encoded,"\xf4\x8f\xbf\xbf",4));
 int no_leads=1;for(int i=0;i<MAX_LEADBYTES;i++)if(info.LeadByte[i])no_leads=0;
 CHECK("cpinfo-utf8-no-dbcs",no_leads);
 const UINT pages[]={437,1252,28591,CP_ACP,CP_OEMCP,CP_THREAD_ACP};int legacy=1;
 for(unsigned i=0;i<sizeof pages/sizeof pages[0];i++)if(!GetCPInfo(pages[i],&info)||info.MaxCharSize!=1)legacy=0;
 CHECK("cpinfo-legacy-metadata",legacy);
 memset(&info,0x55,sizeof info);SetLastError(0);
 CHECK("cpinfo-unknown",!GetCPInfo(99999,&info) && GetLastError()==87);
 const char text[]="A\xc3\xa4\xe2\x82\xac\xf0\x9f\x98\x80";
 const WCHAR wide[]={65,0xe4,0x20ac,0xd83d,0xde00,0};
 WCHAR w[32];char a[64];DWORD n;memset(w,0x55,sizeof w);memset(a,0x55,sizeof a);
 CHECK("mb-query-terminated",MultiByteToWideChar(CP_UTF8,0,text,-1,0,0)==6);
 CHECK("mb-query-counted",MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,sizeof text-1,0,0)==5);
 n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,w,6);CHECK("mb-utf8-supplementary",n==6 && !memcmp(w,wide,sizeof wide) && w[6]==0x5555);
 CHECK("wc-query-terminated",WideCharToMultiByte(CP_UTF8,0,wide,-1,0,0,0,0)==sizeof text);
 CHECK("wc-query-counted",WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,5,0,0,0,0)==sizeof text-1);
 n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,a,sizeof text,0,0);CHECK("wc-utf8-supplementary",n==sizeof text && !memcmp(a,text,sizeof text) && a[sizeof text]==0x55);
 w[0]=0x5555;SetLastError(0);CHECK("mb-short-buffer",MultiByteToWideChar(CP_UTF8,0,text,-1,w,5)==0 && GetLastError()==122 && w[0]==0x5555);
 a[0]='!';SetLastError(0);CHECK("wc-short-buffer",WideCharToMultiByte(CP_UTF8,0,wide,-1,a,sizeof text-1,0,0)==0 && GetLastError()==122 && a[0]=='!');
 const char zeros[]={'a',0,'b'};const WCHAR wz[]={L'a',0,L'b'};
 memset(w,0x55,sizeof w);CHECK("mb-counted-embedded-nul",MultiByteToWideChar(CP_UTF8,0,zeros,3,w,32)==3 && !memcmp(w,wz,sizeof wz) && w[3]==0x5555);
 memset(a,0x55,sizeof a);CHECK("wc-counted-embedded-nul",WideCharToMultiByte(CP_UTF8,0,wz,3,a,64,0,0)==3 && !memcmp(a,zeros,3) && a[3]==0x55);
 const char invalid[]={ (char)0xe2,(char)0x82 };const WCHAR bad[]={0xd800,L'A',0xdc00};
 SetLastError(0);CHECK("mb-reject-truncated",MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,invalid,2,w,32)==0 && GetLastError()==1113);
 CHECK("mb-replace-truncated",MultiByteToWideChar(CP_UTF8,0,invalid,2,w,32)==1 && w[0]==0xfffd);
 SetLastError(0);CHECK("wc-reject-surrogate",WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,bad,3,a,64,0,0)==0 && GetLastError()==1113);
 const char replacement[]="\xef\xbf\xbd" "A" "\xef\xbf\xbd";
 CHECK("wc-replace-surrogates",WideCharToMultiByte(CP_UTF8,0,bad,3,a,64,0,0)==7 && !memcmp(a,replacement,7));
 SetLastError(0);CHECK("mb-invalid-flags",MultiByteToWideChar(CP_UTF8,MB_PRECOMPOSED,text,-1,w,32)==0 && GetLastError()==1004);
 SetLastError(0);CHECK("wc-invalid-flags",WideCharToMultiByte(CP_UTF8,WC_NO_BEST_FIT_CHARS,wide,-1,a,64,0,0)==0 && GetLastError()==1004);
 BOOL used=1;SetLastError(0);CHECK("wc-no-used-default",WideCharToMultiByte(CP_UTF8,0,wide,-1,a,64,0,&used)==0 && GetLastError()==87);
 SetLastError(0);CHECK("wc-no-default-char",WideCharToMultiByte(CP_UTF8,0,wide,-1,a,64,"?",0)==0 && GetLastError()==87);
 SetLastError(0);CHECK("mb-invalid-source-length",MultiByteToWideChar(CP_UTF8,0,text,-2,w,32)==0 && GetLastError()==87);
 SetLastError(0);CHECK("wc-invalid-source-length",WideCharToMultiByte(CP_UTF8,0,wide,0,a,64,0,0)==0 && GetLastError()==87);
 SetLastError(0);CHECK("mb-negative-capacity",MultiByteToWideChar(CP_UTF8,0,text,-1,w,-1)==0 && GetLastError()==87);
 SetLastError(0);CHECK("wc-negative-capacity",WideCharToMultiByte(CP_UTF8,0,wide,-1,a,-1,0,0)==0 && GetLastError()==87);
 SetLastError(0);CHECK("mb-null-output",MultiByteToWideChar(CP_UTF8,0,text,-1,0,32)==0 && GetLastError()==87);
 SetLastError(0);CHECK("wc-null-output",WideCharToMultiByte(CP_UTF8,0,wide,-1,0,64,0,0)==0 && GetLastError()==87);
 SetLastError(0);CHECK("mb-invalid-user-address",MultiByteToWideChar(CP_UTF8,0,(const char *)(ULONG_PTR)0xffff800000000000ULL,1,w,32)==0 && GetLastError()==998);
 SetLastError(0);CHECK("wc-invalid-user-address",WideCharToMultiByte(CP_UTF8,0,(const WCHAR *)(ULONG_PTR)0xffff800000000000ULL,1,a,64,0,0)==0 && GetLastError()==998);
 SetLastError(56789);CHECK("mb-preserve-error",MultiByteToWideChar(CP_UTF8,0,text,-1,0,0)==6 && GetLastError()==56789);
 SetLastError(56789);CHECK("wc-preserve-error",WideCharToMultiByte(CP_UTF8,0,wide,-1,0,0,0,0)==sizeof text && GetLastError()==56789);
 printf("winunicode-failures=%d\n",failures);puts("winunicode-done");return failures?1:0;
}
