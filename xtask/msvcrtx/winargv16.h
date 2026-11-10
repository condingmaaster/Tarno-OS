// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded Microsoft CRT UTF16 command-line parser. No shell, allocation or
// encoding conversion. The program name has the CRT's special quote rule.
#ifndef THOS_WINARGV16_H
#define THOS_WINARGV16_H
#include <stddef.h>
#include <stdint.h>
static int thos_wspace(uint16_t c){return c==' '||c=='\t';}
static int thos_wappend(uint16_t*out,size_t cap,size_t*used,uint16_t c){if(*used>=cap)return 0;out[(*used)++]=c;return 1;}
static int thos_wargv16(const uint16_t*src,size_t len,uint16_t*out,size_t cap,uint16_t**argv,size_t argcap){
 size_t p=0,o=0,n=0;int quoted=0;
 #define START_ARG() do{if(n+1>=argcap)return -1;argv[n++]=out+o;}while(0)
 #define UNIT(c) do{if(!thos_wappend(out,cap,&o,(uint16_t)(c)))return -1;}while(0)
 START_ARG();
 while(p<len&&src[p]&&(quoted||!thos_wspace(src[p]))){if(src[p]=='"')quoted=!quoted;else UNIT(src[p]);p++;}
 UNIT(0);
 while(p<len&&src[p]){
  while(p<len&&thos_wspace(src[p]))p++;if(p==len||!src[p])break;
  START_ARG();quoted=0;
  while(p<len&&src[p]&&(quoted||!thos_wspace(src[p]))){
   size_t slashes=0;while(p<len&&src[p]=='\\'){slashes++;p++;}
   if(p<len&&src[p]=='"'){
    for(size_t i=0;i<slashes/2;i++)UNIT('\\');
    if(slashes&1){UNIT('"');p++;}
    else if(quoted&&p+1<len&&src[p+1]=='"'){UNIT('"');p+=2;}
    else{quoted=!quoted;p++;}
   }else{
    for(size_t i=0;i<slashes;i++)UNIT('\\');
    if(p==len||!src[p]||(!quoted&&thos_wspace(src[p])))break;
    UNIT(src[p]);p++;
   }
  }
  UNIT(0);
 }
 argv[n]=0;
 #undef UNIT
 #undef START_ARG
 return (int)n;
}
#endif
