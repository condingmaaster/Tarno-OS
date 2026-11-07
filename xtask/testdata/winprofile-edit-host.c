/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../msvcrtx/winprofile_edit.h"
static uint16_t input[PI_MAX_UNITS],output[PI_MAX_UNITS+2],section[1026],key[1026],value[32768];
static size_t ascii(uint16_t*out,const char*s){size_t n=0;while(s[n]){out[n]=(unsigned char)s[n];n++;}return n;}
static void example(const char*old,const char*s,const char*k,const char*v,const char*expected){size_t n=ascii(input,old),sn=ascii(section,s),kn=k?ascii(key,k):0,vn=v?ascii(value,v):0,need=0;memset(output,0x55,sizeof output);assert(!pi_edit(input,n,section,sn,k?key:0,kn,v?value:0,vn,0,0,&need));assert(need==strlen(expected));if(need){assert(pi_edit(input,n,section,sn,k?key:0,kn,v?value:0,vn,output,need-1,&need)==122);assert(output[0]==0x5555);}assert(!pi_edit(input,n,section,sn,k?key:0,kn,v?value:0,vn,output,need,&need));for(size_t i=0;i<need;i++)assert(output[i]==(unsigned char)expected[i]);assert(output[need]==0x5555);}
int main(void){example("","Section","Key","Value","[Section]\r\nKey=Value\r\n");example(";keep\n[section]\n Key = old\n; comment\n[Other]\nX=Y","SECTION","KEY","new",";keep\n[section]\nKEY=new\r\n; comment\n[Other]\nX=Y");example("[S]\nk=v","s","next","value","[S]\nk=v\r\nnext=value\r\n");example("[s]\nk=v\n[s2]\nx=y","S","k",0,"[s]\n[s2]\nx=y");example(";head\n[s]\nk=v\n;remove\n[s2]\nx=y","s",0,0,";head\n[s2]\nx=y");example("[s]\nk=v","none","k",0,"[s]\nk=v");example("[s]\nk=v","none",0,0,"[s]\nk=v");example("[s]\nk=v","s","k","","[s]\nk=\r\n");example("[s]\nk=1\nk=2","s","k","3","[s]\nk=3\r\nk=2");
 size_t needed=0;section[0]='S';key[0]='K';value[0]=0x20ac;assert(!pi_edit(input,0,section,1,key,1,value,1,output,100,&needed)&&output[7]==0x20ac);value[0]='\n';assert(pi_edit(input,0,section,1,key,1,value,1,output,100,&needed)==87);key[0]='=';assert(pi_edit(input,0,section,1,key,1,0,0,output,100,&needed)==87);
 uint32_t rng=1337;for(unsigned trial=0;trial<100000;trial++){size_t n=trial%1024;for(size_t j=0;j<n;j++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;input[j]="abcABC[]=; \t\r\n"[rng%14];}section[0]='a';key[0]='b';value[0]='c';unsigned e=pi_edit(input,n,section,1,key,1,value,1,0,0,&needed);assert(!e&&needed<=n+16);output[needed]=0x5555;assert(!pi_edit(input,n,section,1,key,1,value,1,output,needed,&needed)&&output[needed]==0x5555);}
 puts("INI editor HOST foundation: create/update/delete section/key, comments/unrelated bytes, CRLF/LF/no-final-newline, UTF16, exact buffers, duplicate-first policy and100000 generated inputs UBSan PASS; no file persistence/API export");}
