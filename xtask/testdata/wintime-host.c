#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../msvcrtx/wintime_format.h"
static size_t len(const uint16_t*s){size_t n=0;while(s[n])n++;return n;}
static int eq(const uint16_t*a,const uint16_t*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
#define W(s) ((const uint16_t*)L##s)
static void check(const uint16_t*f,const uint16_t*expected,struct thos_tfmt_time t,unsigned flags){uint16_t out[128];for(int i=0;i<128;i++)out[i]=0x5555;uint32_t error=9;int n=thos_tfmt_format(f,len(f),&t,W("AM"),2,W("PM"),2,flags,0,0,&error);assert(n==(int)len(expected)+1&&!error);if(n>1)assert(!thos_tfmt_format(f,len(f),&t,W("AM"),2,W("PM"),2,flags,out,n-1,&error)&&error==122&&out[0]==0x5555);assert(thos_tfmt_format(f,len(f),&t,W("AM"),2,W("PM"),2,flags,out,n,&error)==n&&!error&&eq(out,expected)&&out[n]==0x5555);}
int main(void){struct thos_tfmt_time t={13,4,9,999};check(W("\'\'"),W(""),t,0);check(W("\'\'\'\'"),W("\'"),t,0);check(W("HH:mm:ss"),W("13:04:09"),t,0);check(W("h:mm:ss tt"),W("1:04:09 PM"),t,0);check(W("hh h HH H m mm s ss t tt"),W("01 1 13 13 4 04 9 09 P PM"),t,0);check(W("HHH:mmm:sss tttt"),W("13:04:09 PM"),t,0);check(W("'o''clock' HH"),W("o'clock 13"),t,0);check(W("'Uhr \U0001f600 'HH:mm"),W("Uhr \U0001f600 13:04"),t,0);check(W("'HH:mm"),W("HH:mm"),t,0);check(W("XfZ SS"),W("XfZ SS"),t,0);check(W("h hh tt"),W("13 13 PM"),t,8);t.hour=0;check(W("hh HH tt"),W("12 00 AM"),t,0);check(W("h hh tt"),W("0 00 AM"),t,8);t.hour=12;check(W("h hh tt"),W("12 12 PM"),t,0);uint32_t e;uint16_t out[256];t.hour=24;assert(!thos_tfmt_format(W("HH"),2,&t,0,0,0,0,0,out,256,&e)&&e==87);t.hour=13;t.millisecond=1000;assert(!thos_tfmt_format(W("HH"),2,&t,0,0,0,0,0,out,256,&e)&&e==87);t.millisecond=0;assert(!thos_tfmt_format(W("HH"),2,&t,0,0,0,0,16,out,256,&e)&&e==50);assert(!thos_tfmt_format(W("HH"),1025,&t,0,0,0,0,0,out,256,&e)&&e==87);assert(thos_tfmt_format(0,0,&t,0,0,0,0,0,out,256,&e)==1&&!out[0]&&!e);
 for(unsigned s=0;s<86400;s++){t.hour=s/3600;t.minute=(s/60)%60;t.second=s%60;struct tm tm={0};tm.tm_hour=t.hour;tm.tm_min=t.minute;tm.tm_sec=t.second;char reference[40];strftime(reference,sizeof reference,"%H:%M:%S",&tm);int n=thos_tfmt_format(W("HH:mm:ss"),8,&t,W("AM"),2,W("PM"),2,0,out,256,&e);assert(n==9&&!e);for(int i=0;i<n;i++)assert(out[i]==(unsigned char)reference[i]);strftime(reference,sizeof reference,"%I:%M:%S %p",&tm);n=thos_tfmt_format(W("hh:mm:ss tt"),11,&t,W("AM"),2,W("PM"),2,0,out,256,&e);assert(n==(int)strlen(reference)+1&&!e);for(int i=0;i<n;i++)assert(out[i]==(unsigned char)reference[i]);}

 t.hour=13;t.minute=4;t.second=9;t.millisecond=0;
 const uint16_t*results[]={W("1:04:09 PM"),W("1 PM"),W("1:04 PM"),W("1 PM"),W("1:04:09"),W("1"),W("1:04"),W("1"),W("13:04:09 PM"),W("13 PM"),W("13:04 PM"),W("13 PM"),W("13:04:09"),W("13"),W("13:04"),W("13")};
 for(unsigned f=0;f<16;f++)check(W("h:mm:ss tt"),results[f],t,f);
 check(W("m1s2m3s4"),W(""),t,1);check(W("s1s2s3"),W(""),t,2);
 check(W("h.@:m.@:s.@:tt"),W("1.@:4PM"),t,2);check(W("h1t2tt3m"),W("14"),t,4);
 check(W("HH' long separator that exceeds final capacity 'mm:ss"),W("13"),t,1);
 check(W("HH:mm' quoted text 'ss tt"),W("13:04 PM"),t,2);

 for(unsigned flags=0;flags<32;flags++){int n=thos_tfmt_format(W("HH"),2,&t,W("AM"),2,W("PM"),2,flags,out,256,&e);assert(flags<16?(n==3&&!e):(!n&&e==50));}
 t.minute=60;assert(!thos_tfmt_format(W("HH"),2,&t,0,0,0,0,0,out,256,&e)&&e==87);t.minute=4;t.second=60;assert(!thos_tfmt_format(W("HH"),2,&t,0,0,0,0,0,out,256,&e)&&e==87);t.second=9;
 assert(!thos_tfmt_format(W("HH"),2,0,0,0,0,0,0,out,256,&e)&&e==87);assert(!thos_tfmt_format(W("HH"),2,&t,0,1,0,0,0,out,256,&e)&&e==87);assert(!thos_tfmt_format(W("HH"),2,&t,0,0,0,0,0,0,3,&e)&&e==122);
 uint16_t picture[1024],large[1026];for(int i=0;i<1024;i++)picture[i]='x';for(int i=0;i<1026;i++)large[i]=0x5555;assert(!thos_tfmt_format(picture,1024,&t,0,0,0,0,0,large,1024,&e)&&e==122&&large[0]==0x5555);assert(thos_tfmt_format(picture,1024,&t,0,0,0,0,0,large,1025,&e)==1025&&!e&&large[1024]==0&&large[1025]==0x5555);
 uint32_t rng=0x12345678;const uint16_t alphabet[]={'h','H','m','s','t','\'',':',' ',0,0xd800,0xdc00};for(int test=0;test<10000;test++){uint16_t fmt[64];rng=rng*1664525U+1013904223U;size_t units=rng%64;for(size_t i=0;i<units;i++){rng=rng*1664525U+1013904223U;fmt[i]=alphabet[rng%11];}for(int i=0;i<256;i++)out[i]=0x5555;int n=thos_tfmt_format(fmt,units,&t,W("AM"),2,W("PM"),2,0,0,0,&e);assert(n>0&&n<256&&!e);assert(thos_tfmt_format(fmt,units,&t,W("AM"),2,W("PM"),2,0,out,n,&e)==n&&!e&&out[n]==0x5555&&out[n-1]==0);}
 puts("time-format engine: token/quote/UTF16 bounds, 86400 times vs libc strftime for 24h/12h, 10000 generated pictures UBSan PASS; engine-only, separate API/PE tests");}
