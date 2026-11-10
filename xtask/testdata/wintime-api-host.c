#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define EXPORT
#define WINAPI __attribute__((ms_abi))
typedef uint16_t u16;typedef uint32_t u32;typedef int BOOL;
typedef struct{u16 y,mo,dow,d,h,mi,s,ms;} SYSTEMTIME;
static u32 last_error;static void SetLastError(u32 v){last_error=v;}
static unsigned char env_ansi(u16 c){return c<256?c:'?';}
static size_t env_wlen(const u16*s){size_t n=0;while(s[n])n++;return n;}
static void GetLocalTime(SYSTEMTIME*t){*t=(SYSTEMTIME){2026,10,6,24,13,4,9,1};}
#include "../msvcrtx/winnls_data.h"
#include "../msvcrtx/wintime_api.h"
int main(void){SYSTEMTIME t={0,0,0,0,13,4,9,999};u16 b[1026],f[1026];
for(unsigned flags=0;flags<32;flags++){last_error=0x1234;int n=GetTimeFormatW(0x409,flags,&t,0,b,1026);if(flags>=16)assert(!n&&last_error==1004);else assert(n>0&&last_error==0x1234);}
for(int i=0;i<1026;i++){b[i]=0x5555;f[i]='x';}f[1024]=0;assert(GetTimeFormatW(0x409,0,&t,f,b,1025)==1025&&b[1024]==0&&b[1025]==0x5555);f[1024]='x';f[1025]=0;assert(!GetTimeFormatW(0x409,0,&t,f,b,1026)&&last_error==87);
last_error=0x1234;assert(GetTimeFormatW(0x407,0,0,0,b,1026)==9&&!memcmp(b,L"13:04:09",18)&&last_error==0x1234);
for(int n=-2;n<12;n++){last_error=0x1234;int v=GetTimeFormatW(0x409,0,&t,0,b,n);assert(n<0?(!v&&last_error==87):n==0?(v==11&&last_error==0x1234):n<11?(!v&&last_error==122):(v==11&&last_error==0x1234));}
puts("time API host UBSan: actual bridge, all low flags, 1024-unit boundary, NULL-provider mocked explicitly, capacities PASS; no real clock claim");}
