/* SPDX-License-Identifier: GPL-2.0-or-later
 * Locale metadata/time picture bridge. Includes follow winenv.h/winnls_data.h.
 * Windows runtime local time currently uses UTC (GetLocalTime); panel has its
 * own CET/CEST TZ. No inferred offset, TZ inheritance or full NLS is claimed.
 */
#include "wintime_format.h"
EXPORT int WINAPI GetTimeFormatW(u32 locale,u32 flags,const SYSTEMTIME *time,
                                 const u16 *picture,u16 *out,int capacity){
 if(capacity<0){SetLastError(87);return 0;}
 if(locale==0||locale==0x400||locale==0x800)locale=0x407;
 if(locale!=0x407&&locale!=0x409){SetLastError(87);return 0;}
 /* ACP selection is immaterial for W and there are no user overrides yet. */
 if(flags&~(0xfU|0x40000000U|0x80000000U)){SetLastError(1004);return 0;}
 if(picture&&(flags&0x80000000U)){SetLastError(1004);return 0;}
 const u16 *am=0,*pm=0,*def=0;
 for(unsigned i=0;i<sizeof nls_items/sizeof nls_items[0];i++){
  const u16 *value=locale==0x407?nls_items[i].de:nls_items[i].en;
  if(nls_items[i].field==0x28)am=value;
  if(nls_items[i].field==0x29)pm=value;
  if(nls_items[i].field==0x1003)def=value;
 }
 if(!picture)picture=def;
 size_t units=0;while(units<=1024&&picture[units])units++;
 if(units>1024){SetLastError(87);return 0;}
 SYSTEMTIME now;if(!time){GetLocalTime(&now);time=&now;}
 struct thos_tfmt_time t={time->h,time->mi,time->s,time->ms};
 u32 error=0;int n=thos_tfmt_format(picture,units,&t,am,env_wlen(am),pm,
                                 env_wlen(pm),flags&15U,out,capacity,&error);
 if(!n)SetLastError(error);return n;
}
