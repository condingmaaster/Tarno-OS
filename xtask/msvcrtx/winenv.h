/* SPDX-License-Identifier: GPL-2.0-or-later
 * Process-local runtime environment. PEB/CRT startup inheritance is separate.
 * UTF-16 is canonical. A variable APIs use ACP1252; legacy A snapshots
 * use OEM437, as documented by Windows. No best-fit transliteration.
 */
#define ENV_SLOTS 64
#define ENV_MAX_TEXT 32766u
#define ENV_MAX_UNITS 131072u
struct env_entry { const u16 *name, *value; u16 *owned; };
static struct env_entry env_entries[ENV_SLOTS];
static int env_lock, env_ready;
static const u16 *env_defaults[][2] = {
 {L"PATH",L"C:\\Windows;C:\\Windows\\System32"},{L"TEMP",L"C:\\tmp"},{L"TMP",L"C:\\tmp"},{L"OS",L"THOS"},
 {L"COMPUTERNAME",L"THOS"},{L"SystemRoot",L"C:\\Windows"},{L"USERPROFILE",L"C:\\Users\\thos"},
 {L"HOMEDRIVE",L"C:"},{L"HOMEPATH",L"\\Users\\thos"},{L"USERNAME",L"thos"},{L"PROCESSOR_ARCHITECTURE",L"AMD64"}
};
static void env_acquire(void) {
 while (__atomic_exchange_n(&env_lock,1,__ATOMIC_ACQUIRE)) sc6(202,(i64)&env_lock,128,1,0,0,0);
 if (!env_ready) {
  for (unsigned i=0;i<sizeof env_defaults/sizeof env_defaults[0];i++) {
   env_entries[i].name=env_defaults[i][0];env_entries[i].value=env_defaults[i][1];
  }
  env_ready=1;
 }
}
static void env_release(void) { __atomic_store_n(&env_lock,0,__ATOMIC_RELEASE);sc6(202,(i64)&env_lock,129,1,0,0,0); }
static const u16 env_cp1252[32]={0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
static u16 env_unit(const void *s,size_t at,int wide) {
 if(wide)return ((const u16*)s)[at];
 unsigned c=((const unsigned char*)s)[at];return c>=128&&c<160?env_cp1252[c-128]:(u16)c;
}
static int env_length(const void *s,int wide,size_t *n,int name) {
 if(!s){SetLastError(87);return 0;}
 for(*n=0;*n<=ENV_MAX_TEXT;(*n)++) {
  u16 c=env_unit(s,*n,wide);if(!c){if(name&&!*n){SetLastError(87);return 0;}return 1;}
  if(name&&c=='='){SetLastError(87);return 0;}
 }
 SetLastError(206);return 0;
}
static u16 env_fold(u16 c) { return c>='A'&&c<='Z'?c+32:c; }
static int env_compare(const u16*a,const void*b,int wide) {
 for(size_t i=0;;i++){u16 x=env_fold(a[i]),y=env_fold(env_unit(b,i,wide));if(x!=y)return x<y?-1:1;if(!x)return 0;}
}
static size_t env_wlen(const u16*s){size_t n=0;while(s[n])n++;return n;}
static int env_find(const void*n,int wide) {for(int i=0;i<ENV_SLOTS;i++)if(env_entries[i].name&&!env_compare(env_entries[i].name,n,wide))return i;return -1;}
static const u16 env_cp437[128]={0xc7,0xfc,0xe9,0xe2,0xe4,0xe0,0xe5,0xe7,0xea,0xeb,0xe8,0xef,0xee,0xec,0xc4,0xc5,0xc9,0xe6,0xc6,0xf4,0xf6,0xf2,0xfb,0xf9,0xff,0xd6,0xdc,0xa2,0xa3,0xa5,0x20a7,0x192,0xe1,0xed,0xf3,0xfa,0xf1,0xd1,0xaa,0xba,0xbf,0x2310,0xac,0xbd,0xbc,0xa1,0xab,0xbb,0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,0x3b1,0xdf,0x393,0x3c0,0x3a3,0x3c3,0xb5,0x3c4,0x3a6,0x398,0x3a9,0x3b4,0x221e,0x3c6,0x3b5,0x2229,0x2261,0xb1,0x2265,0x2264,0x2320,0x2321,0xf7,0x2248,0xb0,0x2219,0xb7,0x221a,0x207f,0xb2,0x25a0,0xa0};
static unsigned char env_oem(u16 c){if(c<128)return (unsigned char)c;for(unsigned i=0;i<128;i++)if(env_cp437[i]==c)return (unsigned char)(i+128);return '?';}
static unsigned char env_ansi(u16 c) {
 if(c<128||(c>=160&&c<=255))return (unsigned char)c;
 for(unsigned i=0;i<32;i++)if(env_cp1252[i]==c)return (unsigned char)(i+128);
 return '?';
}
static size_t env_output(const u16*s,void*out,int wide) {
 size_t n=0;for(size_t i=0;s[i];i++) {
  u16 c=s[i];if(wide==1){if(out)((u16*)out)[n]=c;}
  else {if(out)((char*)out)[n]=(char)(wide==2?env_oem(c):env_ansi(c));if(c>=0xd800&&c<=0xdbff&&s[i+1]>=0xdc00&&s[i+1]<=0xdfff)i++;}
  n++;
 }
 if(out){if(wide==1)((u16*)out)[n]=0;else((char*)out)[n]=0;}return n;
}
static u32 env_get(const void*n,void*out,u32 capacity,int wide) {
 size_t unused;if(!env_length(n,wide,&unused,1))return 0;
 env_acquire();int i=env_find(n,wide);if(i<0){env_release();SetLastError(203);return 0;}
 size_t count=env_output(env_entries[i].value,0,wide);
 if(count+1>capacity){env_release();return (u32)count+1;}
 if(!out){env_release();SetLastError(87);return 0;}
 env_output(env_entries[i].value,out,wide);env_release();return (u32)count;
}
static BOOL env_set(const void*n,const void*v,int wide) {
 size_t nl,vl=0;if(!env_length(n,wide,&nl,1)||(v&&!env_length(v,wide,&vl,0)))return 0;
 env_acquire();int i=env_find(n,wide);
 if(!v){if(i>=0){LocalFree(env_entries[i].owned);env_entries[i]=(struct env_entry){0};}env_release();return 1;}
 size_t total=nl+vl+2;
 for(int j=0;j<ENV_SLOTS;j++)if(j!=i&&env_entries[j].name)total+=env_wlen(env_entries[j].name)+env_wlen(env_entries[j].value)+2;
 if(total>ENV_MAX_UNITS){env_release();SetLastError(8);return 0;}
 if(i<0){for(i=0;i<ENV_SLOTS&&env_entries[i].name;i++);if(i==ENV_SLOTS){env_release();SetLastError(8);return 0;}}
 u16*copy=LocalAlloc(0,(nl+vl+2)*2);if(!copy){env_release();SetLastError(8);return 0;}
 for(size_t j=0;j<=nl;j++)copy[j]=env_unit(n,j,wide);
 u16*value=copy+nl+1;for(size_t j=0;j<=vl;j++)value[j]=env_unit(v,j,wide);
 u16*old=env_entries[i].owned;env_entries[i]=(struct env_entry){copy,value,copy};LocalFree(old);env_release();return 1;
}
static void *env_snapshot(int wide) {
 env_acquire();unsigned order[ENV_SLOTS],count=0;size_t units=1;
 for(unsigned i=0;i<ENV_SLOTS;i++)if(env_entries[i].name){
  unsigned j=count;while(j&&env_compare(env_entries[order[j-1]].name,env_entries[i].name,1)>0){order[j]=order[j-1];j--;}
  order[j]=i;count++;units+=env_output(env_entries[i].name,0,wide)+env_output(env_entries[i].value,0,wide)+2;
 }
 if(!count)units=2;
 void *block=LocalAlloc(0,units*(wide==1?2:1));if(!block){env_release();SetLastError(8);return 0;}
 size_t at=0;for(unsigned i=0;i<count;i++) {
  struct env_entry *e=&env_entries[order[i]];
  void*dest=wide==1?(void*)((u16*)block+at):(void*)((char*)block+at);at+=env_output(e->name,dest,wide);
  if(wide==1)((u16*)block)[at++]='=';else((char*)block)[at++]='=';
  dest=wide==1?(void*)((u16*)block+at):(void*)((char*)block+at);at+=env_output(e->value,dest,wide)+1;
 }
 if(wide==1){((u16*)block)[at]=0;if(!count)((u16*)block)[1]=0;}else{((char*)block)[at]=0;if(!count)((char*)block)[1]=0;}
 env_release();return block;
}
EXPORT u32 WINAPI GetEnvironmentVariableA(const char*n,char*b,u32 cap){return env_get(n,b,cap,0);}
EXPORT u32 WINAPI GetEnvironmentVariableW(const u16*n,u16*b,u32 cap){return env_get(n,b,cap,1);}
EXPORT BOOL WINAPI SetEnvironmentVariableA(const char*n,const char*v){return env_set(n,v,0);}
EXPORT BOOL WINAPI SetEnvironmentVariableW(const u16*n,const u16*v){return env_set(n,v,1);}
EXPORT char *WINAPI GetEnvironmentStringsA(void){return env_snapshot(2);}
EXPORT char *WINAPI GetEnvironmentStrings(void){return env_snapshot(2);}
EXPORT u16 *WINAPI GetEnvironmentStringsW(void){return env_snapshot(1);}
EXPORT BOOL WINAPI FreeEnvironmentStringsA(char*p){if(!p){SetLastError(87);return 0;}LocalFree(p);return 1;}
EXPORT BOOL WINAPI FreeEnvironmentStringsW(u16*p){if(!p){SetLastError(87);return 0;}LocalFree(p);return 1;}
