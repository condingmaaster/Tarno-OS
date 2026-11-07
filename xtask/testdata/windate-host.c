#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <locale.h>
typedef long long i64;
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

#include "../msvcrtx/wincivil_days.h"
#include "../msvcrtx/windate_format.h"

static int equal(const u16*a,const u16*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
int main(void){setlocale(LC_TIME,"C");u16 b[1026];SYSTEMTIME t={2026,10,0,24,65535,65535,65535,65535};last_error=0x1234;
assert(GetDateFormatW(0x407,0,&t,0,b,1026)==11&&equal(b,(u16*)L"24.10.2026")&&last_error==0x1234);
assert(GetDateFormatW(0x409,2,&t,0,b,1026)>0&&equal(b,(u16*)L"Saturday, October 24, 2026"));
assert(GetDateFormatW(0x407,2,&t,0,b,1026)>0&&equal(b,(u16*)L"Samstag, 24. Oktober 2026"));
assert(GetDateFormatW(0x409,0,&t,(u16*)L"ddd MMM d y yy yyy yyyy",b,1026)>0&&equal(b,(u16*)L"Sat Oct 24 26 26 2026 2026"));
assert(GetDateFormatW(0x409,0,&t,(u16*)L"MMMM ''''yy",b,1026)>0&&equal(b,(u16*)L"October '26"));
assert(!GetDateFormatW(0x409,0,&t,(u16*)L"gg",b,1026)&&last_error==50);
for(unsigned y=1601;y<=9999;y++)for(unsigned m=1;m<=12;m++){unsigned limit=31;int leap=y%4==0&&(y%100!=0||y%400==0);if(m==4||m==6||m==9||m==11)limit=30;else if(m==2)limit=28+leap;
 for(unsigned d=1;d<=limit;d++){t.y=y;t.mo=m;t.d=d;t.dow=65535;struct tm ref={0};ref.tm_year=y-1900;ref.tm_mon=m-1;ref.tm_mday=d;time_t seconds=timegm(&ref);assert(seconds!=(time_t)-1);char expected[80];strftime(expected,sizeof expected,"%Y-%m-%d %A",&ref);int n=GetDateFormatW(0x409,0,&t,(u16*)L"yyyy-MM-dd dddd",b,1026);assert(n==(int)strlen(expected)+1);for(int i=0;i<n;i++)assert(b[i]==(unsigned char)expected[i]);}
 t.d=limit+1;assert(!GetDateFormatW(0x409,0,&t,0,b,1026)&&last_error==87);
}
t=(SYSTEMTIME){2026,10,0,24,0,0,0,0};for(int i=0;i<1026;i++)b[i]=0x5555;assert(!GetDateFormatW(0x407,0,&t,0,b,10)&&last_error==122&&b[0]==0x5555);assert(GetDateFormatW(0x407,0,&t,0,b,11)==11&&b[11]==0x5555);
for(unsigned f=0;f<256;f++){int n=GetDateFormatW(0x409,f,&t,0,b,1026);if(f==0||f==1||f==2)assert(n>0);else assert(!n&&(last_error==50||last_error==1004));}
t.y=1600;assert(!GetDateFormatW(0x409,0,&t,0,b,1026)&&last_error==87);t.y=30828;assert(!GetDateFormatW(0x409,0,&t,0,b,1026)&&last_error==87);t.y=30827;assert(GetDateFormatW(0x409,0,&t,(u16*)L"yyyy",b,1026)==6&&equal(b,(u16*)L"30827"));
u16 picture[1026];for(int i=0;i<1025;i++)picture[i]='x';picture[1024]=0;assert(GetDateFormatW(0x409,0,&t,picture,b,1025)==1025);picture[1024]='x';picture[1025]=0;assert(!GetDateFormatW(0x409,0,&t,picture,b,1026)&&last_error==87);
puts("date API host UBSan: all valid days 1601..9999 against libc timegm/strftime; invalid days/month-length/leap centuries; locale names, year pictures, quotes, bounds, all low flags PASS; mocked NULL provider, no real clock claim");}
