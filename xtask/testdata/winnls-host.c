#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#define EXPORT
#define WINAPI __attribute__((ms_abi))
typedef uint16_t u16;typedef uint32_t u32;typedef int BOOL;
static _Thread_local u32 last_error;static void SetLastError(u32 v){last_error=v;}
static unsigned char env_ansi(u16 c){return c==0x20ac?0x80:c<=255?(unsigned char)c:'?';}
#include "../msvcrtx/winnls_data.h"
static _Thread_local int seen,nested;
static BOOL WINAPI count(u16*s){assert(s[0]=='0'&&s[8]==0);u32 lc=s[7]=='7'?0x407:0x409;u16 name[16];assert(GetLocaleInfoW(lc,0x5c,name,16)==6);seen++;return 1;}
static BOOL WINAPI stop(u16*s){seen++;s[0]='x';return 0;}
static BOOL WINAPI reenter(u16*s){int old=seen;assert(EnumSystemLocalesW(count,2));assert(seen==old+2);nested++;return 1;}
static void* worker(void*p){for(int i=0;i<1000;i++){seen=0;last_error=0x1234;assert(EnumSystemLocalesW(count,0)&&seen==2&&last_error==0x1234);}return 0;}
int main(void){for(unsigned f=0;f<16;f++){seen=0;int ok=EnumSystemLocalesW(count,f);int expected=f<8&&(f&3)!=3;assert(!!ok==expected);assert(seen==(expected&&!(f&1)&&f!=4?2:0));}assert(IsValidLocale(0x407,2)&&IsValidLocale(0x409,2));assert(!IsValidLocale(0x407,1)&&last_error==50);assert(!IsValidLocale(0x411,2));seen=0;assert(EnumSystemLocalesW(count,1)&&seen==0);u16 b[64];char a[64];memset(b,0x55,sizeof b);assert(GetLocaleInfoW(0x407,0x5c,0,0)==6);assert(GetLocaleInfoW(0x407,0x5c,b,5)==0&&last_error==122&&b[0]==0x5555);assert(GetLocaleInfoW(0x407,0x5c,b,6)==6&&!memcmp(b,L"de-DE",12)&&b[6]==0x5555);assert(GetLocaleInfoA(0x409,0x5c,a,64)==6&&!strcmp(a,"en-US"));assert(GetLocaleInfoA(0x407,0x14,a,64)==2&&(unsigned char)a[0]==0x80&&!a[1]);assert(GetLocaleInfoW(0x407,0x14,b,64)==2&&b[0]==0x20ac&&!b[1]);assert(GetLocaleInfoW(0x407,0x20000001,b,0)==2);assert(GetLocaleInfoA(0x407,0x20000001,a,0)==4);assert(GetLocaleInfoW(0x407,0x20000001,b,2)==2);u32 value;memcpy(&value,b,4);assert(value==0x407);memset(b,0x55,sizeof b);assert(GetLocaleInfoW(0x407,0x20000001,b+1,2)==2);memcpy(&value,b+1,4);assert(value==0x407&&b[0]==0x5555&&b[3]==0x5555);assert(!GetLocaleInfoW(0x407,0x2000005c,b,64)&&last_error==1004);assert(!GetLocaleInfoW(0x411,0x5c,b,64)&&last_error==87);assert(!GetLocaleInfoW(0x407,0x9999,b,64)&&last_error==1004);assert(!GetLocaleInfoW(0x407,0x10000001,b,64)&&last_error==1004);assert(!GetLocaleInfoW(0x407,0x5c,b,-1)&&last_error==87);assert(GetLocaleInfoW(0x400,0x5c,b,64)==6&&!memcmp(b,L"de-DE",12));seen=0;assert(EnumSystemLocalesW(stop,2)&&seen==1);seen=0;assert(EnumSystemLocalesW(count,2)&&seen==2);seen=0;assert(EnumSystemLocalesW(count,4)&&seen==0);assert(!EnumSystemLocalesW(count,3)&&last_error==1004);assert(!EnumSystemLocalesW(0,0)&&last_error==87);seen=nested=0;assert(EnumSystemLocalesW(reenter,2)&&seen==4&&nested==2);pthread_t ts[8];for(int i=0;i<8;i++)assert(!pthread_create(&ts[i],0,worker,0));for(int i=0;i<8;i++)assert(!pthread_join(ts[i],0));puts("NLS host: bounds, ACP/UTF16, numbers, callback cancellation/mutation/reentry, 8x1000 parallel enumerations PASS");}
