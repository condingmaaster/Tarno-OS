/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "../msvcrtx/winresource_tree.h"
static unsigned char*stage(const char*path,size_t*size){
 FILE*f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=64&&n<16*1024*1024);rewind(f);
 unsigned char*raw=malloc(n);assert(raw&&fread(raw,1,n,f)==(size_t)n);fclose(f);size_t pe=wr_u32(raw+60);assert(wr_range(n,pe,24));
 size_t opt=pe+24,os=wr_u16(raw+pe+20),sections=wr_u16(raw+pe+6);assert(os>=64&&wr_range(n,opt,os));
 *size=wr_u32(raw+opt+56);size_t headers=wr_u32(raw+opt+60);assert(*size&&*size<16*1024*1024&&wr_range(n,0,headers)&&headers<=*size);
 unsigned char*mem=calloc(1,*size);assert(mem);memcpy(mem,raw,headers);size_t table=opt+os;assert(wr_range(n,table,sections*40));
 for(size_t j=0;j<sections;j++){const unsigned char*s=raw+table+40*j;size_t va=wr_u32(s+12),bytes=wr_u32(s+16),off=wr_u32(s+20);assert(wr_range(n,off,bytes)&&wr_range(*size,va,bytes));memcpy(mem+va,raw+off,bytes);}
 free(raw);return mem;
}
static uint32_t rng=123;static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(int argc,char**argv){assert(argc==2||argc==3);size_t size;unsigned char*p=stage(argv[1],&size);struct wr_image image;assert(!wr_init(&image,p,size));
 struct wr_key type={1,10,0,0},name={1,101,0,0};struct wr_leaf leaf;assert(!wr_find(&image,&type,&name,&leaf));assert(leaf.bytes==4&&!memcmp(p+leaf.rva,"\x34\x12\xcd\xab",4));
 struct wr_leaf checked;assert(!wr_member(&image,leaf.entry,0,&checked)&&checked.rva==leaf.rva);assert(!wr_member(&image,leaf.rva,1,&checked)&&checked.entry==leaf.entry);
 assert(wr_member(&image,leaf.entry+1,0,&checked)==1812);assert(wr_member(&image,leaf.rva+1,1,&checked)==1812);
 uint16_t text[]={ 'p','a','y','l','o','a','d'},custom[]={'C','U','S','T','O','M'};struct wr_key named={0,0,text,7},ct={0,0,custom,6};
 assert(!wr_find(&image,&ct,&named,&leaf)&&leaf.bytes==4&&!memcmp(p+leaf.rva,"\x78\x56\xbc\x9a",4));
 struct wr_key absent={1,999,0,0};assert(wr_find(&image,&absent,&name,&leaf)==1813);assert(wr_find(&image,&type,&absent,&leaf)==1814);
 for(size_t end=0;end<size;end++){struct wr_image x;if(!wr_init(&x,p,end)){(void)wr_find(&x,&type,&name,&leaf);(void)wr_member(&x,0,0,&leaf);}}
 unsigned char*mut=malloc(size);assert(mut);for(unsigned j=0;j<100000;j++){memcpy(mut,p,size);for(unsigned k=0;k<4;k++)mut[next()%size]=(unsigned char)next();struct wr_image x;if(!wr_init(&x,mut,size)){(void)wr_find(&x,&type,&name,&leaf);(void)wr_member(&x,j,0,&leaf);}}
 /* A root self-cycle is bounded by the fixed three-level grammar. */
 memcpy(mut,p,size);size_t entry=image.root+16;mut[entry+4]=mut[entry+5]=mut[entry+6]=0;mut[entry+7]=0x80;struct wr_image cycle;assert(!wr_init(&cycle,mut,size));assert(wr_member(&cycle,0,0,&leaf)!=0);
 puts("resource HOST foundation: real windres PE integer/named/de-en leaves, exact payloads, membership/bounds/truncations/100000 mutations/cycle termination UBSan PASS; no exported API");free(mut);free(p);
 if(argc==3){p=stage(argv[2],&size);assert(!wr_init(&image,p,size));unsigned named_count,count;assert(!wr_dir(&image,0,&named_count,&count));unsigned ok=0;
  struct wr_budget budget={8192,131072};for(unsigned j=0;j<count;j++){struct wr_key key;uint32_t child;assert(!wr_entry(&image,16+8*j,j<named_count,&budget,&key,&child));if(!wr_find(&image,&key,&name,&leaf)){assert(!wr_member(&image,leaf.entry,0,&checked));ok++;}}
  printf("actual ASUS PE resource directory bounded: %u types, %u contain numeric name101; parser inspection only, no installer execution\n",count,ok);free(p);
 }
}
