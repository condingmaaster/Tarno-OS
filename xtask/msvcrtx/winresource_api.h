/* SPDX-License-Identifier: GPL-2.0-or-later
 * Resource views borrow the loaded module's existing pages. The caller must
 * keep that module loaded while using them. No resource allocation/copy,
 * background task, MUI/satellite loading, data-file loader mode or full locale
 * negotiation. Include after winenv.h. */
#include "winresource_tree.h"
static unsigned char*wr_peb(void){unsigned char*p;__asm__("mov %%gs:0x60,%0":"=r"(p));return p;}
static unsigned wr_module(HANDLE requested,struct wr_image*out){
 unsigned char*peb=wr_peb();if(!peb)return 6;if(!requested)requested=*(HANDLE*)(peb+0x10);
 unsigned char*ldr=*(unsigned char**)(peb+0x18);if(!ldr)return 6;
 unsigned char*head=ldr+0x10,*node=*(unsigned char**)head;
 for(unsigned n=0;node&&node!=head&&n<128;n++,node=*(unsigned char**)node){
  if(*(HANDLE*)(node+0x30)==requested){size_t bytes=*(u32*)(node+0x40);if(!bytes||bytes>128U*1024*1024)return 193;return wr_init(out,requested,bytes);}}
 return 6;
}
static unsigned wr_argument(const void*p,int wide,u16*storage,struct wr_key*out){
 if((u64)p<=65535){*out=(struct wr_key){1,(u16)(u64)p,0,0};return 0;}
 size_t units=0;for(;units<=1024;units++){u16 c=env_unit(p,units,wide);if(!c)break;if(units==1024)return 87;storage[units]=c;}
 if(!units)return 87;storage[units]=0;
 if(storage[0]=='#') {unsigned id=0;if(units==1)return 87;for(size_t n=1;n<units;n++){unsigned c=storage[n];if(c<'0'||c>'9'||id>(65535U-(c-'0'))/10)return 87;id=id*10+c-'0';}*out=(struct wr_key){1,(u16)id,0,0};return 0;}
 *out=(struct wr_key){0,0,storage,units};return 0;
}
static HANDLE wr_api_find(HANDLE module,const void*name,const void*type,int wide){
 struct wr_image image;unsigned e=wr_module(module,&image);if(e){SetLastError(e);return 0;}
 u16 nbuf[1025],tbuf[1025];struct wr_key nk,tk;e=wr_argument(name,wide,nbuf,&nk);if(!e)e=wr_argument(type,wide,tbuf,&tk);
 struct wr_leaf leaf;if(!e)e=wr_find(&image,&tk,&nk,&leaf);if(e){SetLastError(e);return 0;}return (HANDLE)(image.p+leaf.entry);
}
EXPORT HANDLE WINAPI FindResourceW(HANDLE module,const u16*name,const u16*type){return wr_api_find(module,name,type,1);}
EXPORT HANDLE WINAPI FindResourceA(HANDLE module,const char*name,const char*type){return wr_api_find(module,name,type,0);}
static unsigned wr_api_leaf(HANDLE module,HANDLE info,struct wr_image*image,struct wr_leaf*leaf){
 unsigned e=wr_module(module,image);if(e)return e;u64 base=(u64)image->p,ptr=(u64)info;
 if(ptr<base||ptr-base>=image->bytes)return 1812;return wr_member(image,(size_t)(ptr-base),0,leaf);
}
EXPORT HANDLE WINAPI LoadResource(HANDLE module,HANDLE info){struct wr_image image;struct wr_leaf leaf;unsigned e=wr_api_leaf(module,info,&image,&leaf);if(e){SetLastError(e);return 0;}return (HANDLE)(image.p+leaf.rva);}
EXPORT u32 WINAPI SizeofResource(HANDLE module,HANDLE info){struct wr_image image;struct wr_leaf leaf;unsigned e=wr_api_leaf(module,info,&image,&leaf);if(e){SetLastError(e);return 0;}return leaf.bytes;}
EXPORT void* WINAPI LockResource(HANDLE data){
 unsigned char*peb=wr_peb();if(!peb)return 0;unsigned char*ldr=*(unsigned char**)(peb+0x18);if(!ldr)return 0;
 unsigned char*head=ldr+0x10,*node=*(unsigned char**)head;u64 ptr=(u64)data;
 for(unsigned n=0;node&&node!=head&&n<128;n++,node=*(unsigned char**)node){u64 base=*(u64*)(node+0x30);size_t bytes=*(u32*)(node+0x40);
  if(ptr>=base&&ptr-base<=bytes){struct wr_image image;struct wr_leaf leaf;if(!wr_module((HANDLE)base,&image)&&!wr_member(&image,(size_t)(ptr-base),1,&leaf))return (void*)data;}}
 return 0;
}
/* Obsolete on Win32; resources belong to their module and this returns FALSE. */
EXPORT BOOL WINAPI FreeResource(HANDLE data){(void)data;return 0;}
