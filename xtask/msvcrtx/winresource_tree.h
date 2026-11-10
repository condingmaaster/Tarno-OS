/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded, allocation-free reader for an ALREADY MAPPED PE image.
 * Host-testable foundation; no exported resource API until bridge is verified.
 * Supported tree: type/name/language, numeric or UTF16 names, limited de/en
 * language fallback. No MUI files, satellite loading or Unicode case folding.
 */
#ifndef THOS_WINRESOURCE_TREE_H
#define THOS_WINRESOURCE_TREE_H
#include <stddef.h>
#include <stdint.h>
struct wr_image {const unsigned char *p;size_t bytes,root,span;};
struct wr_key {int numeric;uint16_t id;const uint16_t *text;size_t units;};
struct wr_leaf {size_t entry;uint32_t rva,bytes;};
struct wr_budget {unsigned entries,text;};
static uint16_t wr_u16(const unsigned char*p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t wr_u32(const unsigned char*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int wr_range(size_t size,size_t at,size_t n){return at<=size&&n<=size-at;}
/* Errors are Win32 ERROR_BAD_EXE_FORMAT / INVALID_DATA / RESOURCE_* . */
static unsigned wr_init(struct wr_image *im,const void*base,size_t bytes){
 const unsigned char*p=base;if(!p||bytes<64||wr_u16(p)!=0x5a4d)return 193;
 size_t pe=wr_u32(p+60);if(!wr_range(bytes,pe,24)||wr_u32(p+pe)!=0x4550)return 193;
 size_t opt=pe+24,os=wr_u16(p+pe+20);if(!wr_range(bytes,opt,os)||os<2)return 193;
 unsigned magic=wr_u16(p+opt);size_t dir=magic==0x20b?112:magic==0x10b?96:0;
 if(!dir||os<dir||wr_u32(p+opt+dir-4)<3)return 1813;
 if(os<dir+24)return 193;
 size_t root=wr_u32(p+opt+dir+16),span=wr_u32(p+opt+dir+20);
 if(!root||!span)return 1813;if(span<16||!wr_range(bytes,root,span))return 13;
 *im=(struct wr_image){p,bytes,root,span};return 0;
}
static unsigned wr_dir(const struct wr_image*i,size_t off,unsigned*named,unsigned*count){
 if(!wr_range(i->span,off,16))return 13;
 const unsigned char*p=i->p+i->root+off;*named=wr_u16(p+12);*count=*named+wr_u16(p+14);
 if(*count>4096||!wr_range(i->span,off+16,(size_t)*count*8))return 13;return 0;
}
static unsigned wr_entry(const struct wr_image*i,size_t at,int named,struct wr_budget*b,struct wr_key*k,uint32_t*child){
 if(!b->entries||!wr_range(i->span,at,8))return 13;b->entries--;
 const unsigned char*p=i->p+i->root+at;uint32_t name=wr_u32(p);*child=wr_u32(p+4);
 if(!!(name&0x80000000U)!=!!named)return 13;
 if(!named){if(name>65535)return 13;*k=(struct wr_key){1,(uint16_t)name,0,0};return 0;}
 size_t off=name&0x7fffffffU;if(!wr_range(i->span,off,2))return 13;
 unsigned units=wr_u16(i->p+i->root+off);if(units>1024||units>b->text||!wr_range(i->span,off+2,(size_t)units*2))return 13;
 b->text-=units;/* text is byte-read below: PE names need not be host-aligned. */
 *k=(struct wr_key){0,0,(const uint16_t*)(i->p+i->root+off+2),units};return 0;
}
static int wr_equal(const struct wr_key*a,const struct wr_key*b){
 if(a->numeric!=b->numeric)return 0;if(a->numeric)return a->id==b->id;if(a->units!=b->units)return 0;
 for(size_t n=0;n<a->units;n++){unsigned x=wr_u16((const unsigned char*)a->text+2*n),y=wr_u16((const unsigned char*)b->text+2*n);
  if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return 0;}return 1;
}
static unsigned wr_child(const struct wr_image*i,size_t dir,const struct wr_key*key,struct wr_budget*b,size_t*out,unsigned missing){
 unsigned named,count,e=wr_dir(i,dir,&named,&count);if(e)return e;
 for(unsigned n=0;n<count;n++){struct wr_key k;uint32_t child;e=wr_entry(i,dir+16+8*n,n<named,b,&k,&child);if(e)return e;
  if(wr_equal(&k,key)){if(!(child&0x80000000U))return 13;*out=child&0x7fffffffU;return 0;}}
 return missing;
}
static unsigned wr_data(const struct wr_image*i,size_t off,struct wr_leaf*out){
 if(!wr_range(i->span,off,16))return 13;const unsigned char*p=i->p+i->root+off;
 uint32_t rva=wr_u32(p),bytes=wr_u32(p+4);if(!wr_range(i->bytes,rva,bytes)||wr_u32(p+12))return 13;
 *out=(struct wr_leaf){i->root+off,rva,bytes};return 0;
}
static unsigned wr_find(const struct wr_image*i,const struct wr_key*type,const struct wr_key*name,struct wr_leaf*out){
 struct wr_budget b={8192,131072};size_t types,names;unsigned e=wr_child(i,0,type,&b,&types,1813);if(e)return e;
 e=wr_child(i,types,name,&b,&names,1814);if(e)return e;unsigned named,count;e=wr_dir(i,names,&named,&count);if(e)return e;
 int rank=99;size_t picked=0;
 for(unsigned n=0;n<count;n++){struct wr_key k;uint32_t child;e=wr_entry(i,names+16+8*n,n<named,&b,&k,&child);if(e)return e;
  if(!k.numeric||(child&0x80000000U))return 13;
  int r=k.id==0x407?0:k.id==7?1:k.id==0?2:k.id==0x409?3:4;
  if(r<rank){rank=r;picked=child;}}
 if(rank==99)return 1815;return wr_data(i,picked,out);
}
/* Prove a resource-information handle or resource-data pointer belongs to an
 * actual leaf, rather than trusting an arbitrary in-image address. Depth is
 * fixed, so cyclic tables terminate; one budget spans the complete traversal. */
static unsigned wr_member_dir(const struct wr_image*i,size_t dir,unsigned depth,struct wr_budget*b,size_t wanted,int payload,struct wr_leaf*out){
 unsigned named,count,e=wr_dir(i,dir,&named,&count);if(e)return e;
 for(unsigned n=0;n<count;n++){struct wr_key key;uint32_t child;e=wr_entry(i,dir+16+8*n,n<named,b,&key,&child);if(e)return e;
  if(depth<2){if(!(child&0x80000000U))return 13;e=wr_member_dir(i,child&0x7fffffffU,depth+1,b,wanted,payload,out);if(!e)return 0;if(e!=1812)return e;}
  else {if(!key.numeric||(child&0x80000000U))return 13;struct wr_leaf leaf;e=wr_data(i,child,&leaf);if(e)return e;
   if(wanted==(payload?leaf.rva:leaf.entry)){*out=leaf;return 0;}}}
 return 1812;
}
static unsigned wr_member(const struct wr_image*i,size_t wanted,int payload,struct wr_leaf*out){struct wr_budget b={8192,131072};return wr_member_dir(i,0,0,&b,wanted,payload,out);}
#endif
