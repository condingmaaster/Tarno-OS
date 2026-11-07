/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded UTF16 picture engine, independent of locale and clock providers.
 * wintime_api.h supplies the SYSTEMTIME/locale/LastError bridge.
 * Supports bounded picture tokens/quotes and all four TIME_* flags.
 */
#ifndef THOS_WINTIME_FORMAT_H
#define THOS_WINTIME_FORMAT_H
#include <stddef.h>
#include <stdint.h>
struct thos_tfmt_time {unsigned hour,minute,second,millisecond;};
struct thos_tfmt_writer {uint16_t *out;size_t cap,used;uint32_t error;};
static void thos_tfmt_put(struct thos_tfmt_writer*w,uint16_t c){
 if(w->error)return;if(w->used>=32766){w->error=14;return;}
 /* Rewound separators can briefly exceed the final result's capacity.
  * Count them, but never store beyond the caller's reserved terminator. */
 if(w->out&&w->used+1<w->cap)w->out[w->used]=c;w->used++;
}
static void thos_tfmt_num(struct thos_tfmt_writer*w,unsigned n,int two){if(two||n>=10)thos_tfmt_put(w,(uint16_t)('0'+n/10));thos_tfmt_put(w,(uint16_t)('0'+n%10));}
static void thos_tfmt_pass(struct thos_tfmt_writer*w,const uint16_t*fmt,size_t units,const struct thos_tfmt_time*t,const uint16_t*marker,size_t mn,uint32_t flags){
 int quoted=0,hide=0;size_t i=0,checkpoint=0;
 while(i<units&&fmt[i]&&!w->error){uint16_t c=fmt[i];
  if(c=='\''){if(quoted&&i+1<units&&fmt[i+1]=='\''){if(!hide)thos_tfmt_put(w,'\'');i+=2;}else{quoted=!quoted;i++;}continue;}
  if(!quoted&&(c=='h'||c=='H'||c=='m'||c=='s'||c=='t')){
   size_t run=1;while(i+run<units&&fmt[i+run]==c)run++;
   int omit=(c=='m'&&(flags&1))||(c=='s'&&(flags&3))||(c=='t'&&(flags&4));
   if(omit){w->used=checkpoint;hide=1;i+=run;continue;}
   if(c=='t'){size_t n=run==1?(mn?1:0):mn;for(size_t j=0;j<n&&marker[j];j++)thos_tfmt_put(w,marker[j]);}
   else{unsigned n=c=='m'?t->minute:c=='s'?t->second:t->hour;if(c=='h'&&!(flags&8))n=n%12?n%12:12;thos_tfmt_num(w,n,run>=2);checkpoint=w->used;}
   hide=0;
   i+=run;
  }else{if(!hide||(!quoted&&c==' '))thos_tfmt_put(w,c);i++;}
 }
}
static int thos_tfmt_format(const uint16_t*fmt,size_t units,const struct thos_tfmt_time*time,const uint16_t*am,size_t an,const uint16_t*pm,size_t pn,uint32_t flags,uint16_t*out,int capacity,uint32_t*error){
 uint32_t e=0;int result=0;
 if(!time||capacity<0||units>1024||an>32||pn>32||(units&&!fmt)||(an&&!am)||(pn&&!pm)){e=87;goto done;}
 if(flags&~15U){e=50;goto done;}
 struct thos_tfmt_time t=*time;if(t.hour>23||t.minute>59||t.second>59||t.millisecond>999){e=87;goto done;}
 const uint16_t*marker=t.hour<12?am:pm;size_t mn=t.hour<12?an:pn;
 struct thos_tfmt_writer query={0,0,0,0};thos_tfmt_pass(&query,fmt,units,&t,marker,mn,flags);if(query.error){e=query.error;goto done;}
 int needed=(int)query.used+1;if(capacity==0){result=needed;goto done;}
 if(!out||capacity<needed){e=122;goto done;}
 struct thos_tfmt_writer writer={out,(size_t)capacity,0,0};thos_tfmt_pass(&writer,fmt,units,&t,marker,mn,flags);if(writer.error){e=writer.error;goto done;}if(writer.used+1>(size_t)capacity){e=122;goto done;}out[writer.used]=0;result=(int)writer.used+1;
 done:if(error)*error=e;return result;
}
#endif
