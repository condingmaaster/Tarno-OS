#include <windows.h>
typedef unsigned long long U;static HANDLE out;static int fail;
static long long sc6(long long n,U a,U b,U c,U d,U e,U f){long long r;register U r10 __asm__("r10")=d,r8 __asm__("r8")=e,r9 __asm__("r9")=f;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8),"r"(r9):"rcx","r11","memory");return r;}
static void say(const char*s){DWORD n=0;while(s[n])n++;WriteFile(out,s,n,&n,0);}
#define CHECK(s,c) do{if(c)say("winrollback-ok " s "\n");else{say("winrollback-FAIL " s "\n");fail=1;}}while(0)
static U q(U p){return *(U*)p;}static DWORD d(U p){return *(DWORD*)p;}
static U ldr(void){U p;__asm__ volatile("mov %%gs:0x60,%0":"=r"(p));return q(p+24);}
static int count(int list){U h=ldr()+16+list*16,c=q(h);int n=0;while(c&&c!=h&&n<100){n++;c=q(c);}return c==h?n:-1;}
static int rings(int n){return count(0)==n&&count(1)==n&&count(2)==n;}
static U nextbase(void){U h=ldr()+16,exe=q(h),c=q(exe),next=0;for(int i=0;c&&c!=h&&i<100;i++,c=q(c)){U end=(q(c+48)+(U)d(c+64)+65535)&~65535ULL;if(end>next)next=end;}return next;}
static U freeram(void){unsigned char b[112] __attribute__((aligned(8)));if(sc6(99,(U)b,0,0,0,0,0))return 0;return q((U)b+40);}
static void frames(U a,U b){const char hex[]="0123456789abcdef";char line[35];for(int i=0;i<16;i++){line[i]=hex[(a>>(60-i*4))&15];line[17+i]=hex[(b>>(60-i*4))&15];}line[16]=' ';line[33]='\n';line[34]=0;say("winrollback-frame-bytes ");say(line);}
void start(void){out=GetStdHandle(STD_OUTPUT_HANDLE);say("winrollback-start\n");U base=nextbase(),page=base+4096;int n=count(0);CHECK("initial-module-rings",n>=6&&rings(n));long long got=sc6(9,page,4096,3,0x32,~0ULL,0);CHECK("real-second-page-collision-mapped",got==(long long)page);if(got==(long long)page){volatile U*sentinel=(volatile U*)page;*sentinel=0x123456789abcdef0ULL;SetLastError(0);HMODULE warm=LoadLibraryA("rollbackleaf.dll");int warmok=!warm&&GetLastError()==487;CHECK("partially-prepared-load-fails",warmok);if(warmok){U before=freeram();int ok=1;for(int i=0;i<32;i++){SetLastError(0);HMODULE h=LoadLibraryA("rollbackleaf.dll");if(h||GetLastError()!=487||GetModuleHandleA("rollbackleaf.dll")||!rings(n)||*sentinel!=0x123456789abcdef0ULL){ok=0;break;}}U after=freeram();frames(before,after);CHECK("failed-loads-preserve-mapping-and-rings",ok);CHECK("prepared-frames-returned-on-failure",before&&after>=before);CHECK("collision-mapping-removed",sc6(11,page,4096,0,0,0,0)==0);HMODULE h=LoadLibraryA("rollbackleaf.dll");int(*magic)(void)=(void*)GetProcAddress(h,"magic");CHECK("failed-loads-did-not-consume-arena",h==(HMODULE)base);CHECK("retry-real-code",h&&magic&&magic()==77&&rings(n+1));if(h)CHECK("retry-module-unloaded",FreeLibrary(h)&&!GetModuleHandleA("rollbackleaf.dll")&&rings(n));}else{sc6(11,page,4096,0,0,0,0);}}
 say(fail?"winrollback-result=FAIL\n":"winrollback-result=PASS\n");say("winrollback-done\n");ExitProcess(fail?1:0);
}
