/* SPDX-License-Identifier: GPL-2.0-or-later
 * Gregorian de-DE/en-US date formatting. No alternate calendar/era/layout
 * services are fabricated. Requires wintime_api.h and days_from_civil().
 */
static const u16 *date_months[2][12]={
 {L"Januar",L"Februar",L"M\u00e4rz",L"April",L"Mai",L"Juni",L"Juli",L"August",L"September",L"Oktober",L"November",L"Dezember"},
 {L"January",L"February",L"March",L"April",L"May",L"June",L"July",L"August",L"September",L"October",L"November",L"December"}};
static const u16 *date_short_months[2][12]={
 {L"Jan",L"Feb",L"M\u00e4r",L"Apr",L"Mai",L"Jun",L"Jul",L"Aug",L"Sep",L"Okt",L"Nov",L"Dez"},
 {L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"}};
static const u16 *date_days[2][7]={
 {L"Sonntag",L"Montag",L"Dienstag",L"Mittwoch",L"Donnerstag",L"Freitag",L"Samstag"},
 {L"Sunday",L"Monday",L"Tuesday",L"Wednesday",L"Thursday",L"Friday",L"Saturday"}};
static const u16 *date_short_days[2][7]={
 {L"So",L"Mo",L"Di",L"Mi",L"Do",L"Fr",L"Sa"},
 {L"Sun",L"Mon",L"Tue",L"Wed",L"Thu",L"Fri",L"Sat"}};
static void date_number(struct thos_tfmt_writer*w,unsigned n,unsigned width){
 u16 digits[10];unsigned used=0;do{digits[used++]=(u16)('0'+n%10);n/=10;}while(n);
 while(used<width)digits[used++]='0';while(used)thos_tfmt_put(w,digits[--used]);
}
static void date_text(struct thos_tfmt_writer*w,const u16*s){while(*s)thos_tfmt_put(w,*s++);}
static void date_pass(struct thos_tfmt_writer*w,const u16*fmt,size_t units,const SYSTEMTIME*t,unsigned locale,unsigned weekday){
 int quoted=0;size_t i=0;
 while(i<units&&fmt[i]&&!w->error){u16 c=fmt[i];
  if(c=='\''){if(quoted&&i+1<units&&fmt[i+1]=='\''){thos_tfmt_put(w,'\'');i+=2;}else{quoted=!quoted;i++;}continue;}
  if(!quoted&&(c=='d'||c=='M'||c=='y'||c=='g')){
   size_t run=1;while(i+run<units&&fmt[i+run]==c)run++;
   if(c=='g'){w->error=50;return;}
   if(c=='y')date_number(w,run<=2?t->y%100:t->y,run<=2?2:4);
   else if(c=='M'){if(run<3)date_number(w,t->mo,(unsigned)run);else date_text(w,run==3?date_short_months[locale][t->mo-1]:date_months[locale][t->mo-1]);}
   else{if(run<3)date_number(w,t->d,(unsigned)run);else date_text(w,run==3?date_short_days[locale][weekday]:date_days[locale][weekday]);}
   i+=run;
  }else{thos_tfmt_put(w,c);i++;}
 }
}
EXPORT int WINAPI GetDateFormatW(u32 locale,u32 flags,const SYSTEMTIME *date,const u16 *picture,u16*out,int capacity){
 if(capacity<0){SetLastError(87);return 0;}
 if(locale==0||locale==0x400||locale==0x800)locale=0x407;
 if(locale!=0x407&&locale!=0x409){SetLastError(87);return 0;}
 if(flags&~(0xffU|0xc0000000U)){SetLastError(1004);return 0;}
 u32 style=flags&0x8bU;
 if((style&&(style&(style-1)))||((flags&0x30U)==0x30U)||((flags&0x40U)&&(flags&0x30U))||(picture&&flags&~0x40000000U)){
  SetLastError(1004);return 0;
 }
 if(flags&0xfcU){SetLastError(50);return 0;}
 if(!picture){u32 field=flags&2?0x20:0x1f;for(unsigned i=0;i<sizeof nls_items/sizeof nls_items[0];i++)if(nls_items[i].field==field){picture=locale==0x407?nls_items[i].de:nls_items[i].en;break;}}
 size_t units=0;while(units<=1024&&picture[units])units++;if(units>1024){SetLastError(87);return 0;}
 SYSTEMTIME t;if(date)t=*date;else GetLocalTime(&t);
 unsigned leap=t.y%4==0&&(t.y%100!=0||t.y%400==0);static const unsigned char mdays[]={31,28,31,30,31,30,31,31,30,31,30,31};
 if(t.y<1601||t.y>30827||t.mo<1||t.mo>12||t.d<1||t.d>mdays[t.mo-1]+(t.mo==2?leap:0)){SetLastError(87);return 0;}
 i64 days=days_from_civil(t.y,t.mo,t.d);unsigned weekday=(unsigned)(((days+4)%7+7)%7);
 struct thos_tfmt_writer query={0,0,0,0};date_pass(&query,picture,units,&t,locale==0x409,weekday);
 if(query.error){SetLastError(query.error);return 0;}int needed=(int)query.used+1;
 if(!capacity)return needed;if(!out||capacity<needed){SetLastError(122);return 0;}
 struct thos_tfmt_writer writer={out,(size_t)capacity,0,0};date_pass(&writer,picture,units,&t,locale==0x409,weekday);
 if(writer.error){SetLastError(writer.error);return 0;}if(writer.used+1>(size_t)capacity){SetLastError(122);return 0;}out[writer.used]=0;return (int)writer.used+1;
}
