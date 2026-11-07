// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../msvcrtx/winargv16.h"
static int read32(uint32_t*out){return fread(out,4,1,stdin)==1;}
int main(void){uint32_t n;
 while(read32(&n)){
  if(n>32767)return 2;uint16_t*in=calloc(n+1,2),*out=calloc(n+1,2);uint16_t**argv=calloc(n+2,sizeof(void*));if(!in||!out||!argv)return 3;
  if(fread(in,2,n,stdin)!=n)return 4;int count=thos_wargv16(in,n,out,n+1,argv,n+2);if(count<0)return 5;uint32_t c=(uint32_t)count;fwrite(&c,4,1,stdout);
  for(int i=0;i<count;i++){uint32_t len=0;while(argv[i][len])len++;fwrite(&len,4,1,stdout);fwrite(argv[i],2,len,stdout);}
  // One-unit undersized storage must fail without crossing the canary.
  uint16_t tiny[2]={0x1234,0x5678};uint16_t*ptrs[2];int r=thos_wargv16(in,n,tiny,0,ptrs,2);if(r!=-1||tiny[0]!=0x1234||tiny[1]!=0x5678)return 6;
  free(in);free(out);free(argv);
 }
 return ferror(stdin)?7:0;
}
