typedef unsigned long long U;typedef unsigned int D;typedef unsigned short W;typedef unsigned char B;
static long long sc3(long long n,U a,U b,U c){long long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");return r;}
static void say(const char*s){U n=0;while(s[n])n++;sc3(1,1,(U)s,n);}
static int fail;
#define CHECK(s,c) do{if(c)say("winbare-ok " s "\n");else{say("winbare-FAIL " s "\n");fail=1;}}while(0)
static int eq(const char*a,const char*b){for(int i=0;i<128;i++){if(a[i]!=b[i])return 0;if(!a[i])return 1;}return 0;}
static D d(U p){return *(D*)p;}static U q(U p){return *(U*)p;}
static void* symbol(U base,const char*name){U opt=base+d(base+60)+24,ed=base+d(opt+112);D n=d(ed+24);if(n>1024)return 0;for(D i=0;i<n;i++){U text=base+d(base+d(ed+32)+4*i);if(eq((char*)text,name)){W ord=*(W*)(base+d(ed+36)+2*i);return(void*)(base+d(base+d(ed+28)+4*ord));}}return 0;}
static U head(void){U peb;__asm__ volatile("mov %%gs:0x60,%0":"=r"(peb));return q(peb+24)+16;}
static int count(void){U h=head(),cur=q(h);int n=0;while(cur&&cur!=h&&n<100){n++;cur=q(cur);}return cur==h?n:-1;}
void start(void){say("winbare-start\n");U first=q(head()),kernel=q(q(first)+48);void*(*load)(const char*)=symbol(kernel,"LoadLibraryA");void*(*get)(const char*)=symbol(kernel,"GetModuleHandleA");int(*release)(void*)=symbol(kernel,"FreeLibrary");CHECK("no-file-modules-at-entry",count()==6);CHECK("native-exports-without-runtime-dll",load&&get&&release);if(load&&get&&release){void*h=load("bareleaf.dll");int(*f)(void)=h?symbol((U)h,"magic"):0;CHECK("first-dynamic-metadata-pages",h&&f&&f()==77&&get("bareleaf.dll")==h&&count()==7);if(h)CHECK("release-first-file-module",release(h)&&get("bareleaf.dll")==0&&count()==6);void*next=load("bareleaf.dll");int(*nextf)(void)=next?symbol((U)next,"magic"):0;CHECK("empty-ring-reload",next&&next!=h&&nextf&&nextf()==77&&count()==7);if(next)CHECK("empty-ring-release",release(next)&&count()==6);}
 say(fail?"winbare-result=FAIL\n":"winbare-result=PASS\n");say("winbare-done\n");sc3(60,fail?1:0,0,0);for(;;){}
}
