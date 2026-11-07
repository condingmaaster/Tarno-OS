/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real host /tmp file operations plus deterministic IO fault injection.
 * This validates the C bridge; separate PE tests validate the THOS backend. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <pthread.h>
typedef long long i64;typedef uint64_t u64;typedef uint32_t u32;typedef uint16_t u16;typedef int BOOL;
#define EXPORT
#define WINAPI __attribute__((ms_abi))
#define W(s) ((const u16*)L##s)
enum{S_OPEN=2,S_CLOSE=3,S_READ=0,S_WRITE=1,S_MMAP=9,S_MUNMAP=11,S_RENAME=82,S_UNLINK=87,S_GETPID=39,S_FSYNC=74,S_NEWFSTATAT=262};
static __thread u32 last_error;static __thread int fail_number=-1,partial_write;
static void SetLastError(u32 e){last_error=e;}
static i64 sc6(i64 n,i64 a,i64 b,i64 c,i64 d,i64 e,i64 f){if(n==fail_number){fail_number=-1;return n==S_WRITE?-28:n==S_RENAME?-13:n==S_MMAP?-12:-5;}if(n==S_WRITE&&partial_write&&c>3)c=3;errno=0;long r=syscall(n,a,b,c,d,e,f);return r==-1?-errno:r;}
#define sc3(n,a,b,c) sc6(n,(i64)(a),(i64)(b),(i64)(c),0,0,0)
static size_t slen(const char*s){return strlen(s);}
#include "../msvcrtx/winresource_tree.h"
static const u16 env_cp1252[32]={0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
#include "../msvcrtx/winprofile_write.h"
static char directory[100],filename[140];static u16 path[140];
static void raw_save(const void*p,size_t n){FILE*f=fopen(filename,"wb");assert(f&&fwrite(p,1,n,f)==n&&!fclose(f));}
static size_t raw_read(char*buffer,size_t size){FILE*f=fopen(filename,"rb");assert(f);size_t n=fread(buffer,1,size-1,f);assert(!ferror(f)&&!fclose(f));buffer[n]=0;return n;}
static void*writer(void*arg){u16 key[]={'T','0',0};key[1]+=(uintptr_t)arg;return (void*)(uintptr_t)!WritePrivateProfileStringW(W("Threads"),key,W("value"),path);}
int main(void){snprintf(directory,sizeof directory,"/tmp/thos-profile-host-%ld",(long)getpid());assert(!mkdir(directory,0700));snprintf(filename,sizeof filename,"%s/test.ini",directory);for(size_t j=0;j<=strlen(filename);j++)path[j]=(unsigned char)filename[j];
 char b[4096];const char*old=";keep\n[S]\nK=old\n";raw_save(old,strlen(old));partial_write=1;last_error=0x1234;assert(WritePrivateProfileStringW(W("S"),W("K"),W("new"),path)&&last_error==0x1234);assert(raw_read(b,sizeof b)>0&&strstr(b,"K=new\r\n")&&strstr(b,";keep\n"));partial_write=0;
 for(unsigned i=0;i<4;i++){int failures[]={S_WRITE,S_FSYNC,S_RENAME,S_MMAP};unsigned errors[]={112,1117,5,8};raw_save(old,strlen(old));fail_number=failures[i];assert(!WritePrivateProfileStringW(W("S"),W("K"),W("lost"),path)&&last_error==errors[i]);assert(raw_read(b,sizeof b)==strlen(old)&&!memcmp(b,old,strlen(old)));}
 raw_save(old,strlen(old));assert(!WritePrivateProfileStringW(W("S"),W("K"),W("\u4e2d"),path)&&last_error==1113);assert(raw_read(b,sizeof b)==strlen(old)&&!memcmp(b,old,strlen(old)));
 unsigned char unicode[]={0xff,0xfe,'[',0,'S',0,']',0,'\n',0,'K',0,'=',0,'x',0,'\n',0};raw_save(unicode,sizeof unicode);assert(WritePrivateProfileStringW(W("S"),W("K"),W("\u4e2d"),path));size_t bytes=raw_read(b,sizeof b);assert(bytes>2&&(unsigned char)b[0]==0xff&&(unsigned char)b[1]==0xfe);int found=0;for(size_t j=2;j+1<bytes;j+=2)if((unsigned char)b[j]==0x2d&&(unsigned char)b[j+1]==0x4e)found=1;assert(found);
 raw_save(old,strlen(old));pthread_t th[8];for(uintptr_t i=0;i<8;i++)assert(!pthread_create(th+i,0,writer,(void*)i));for(int i=0;i<8;i++){void*r;assert(!pthread_join(th[i],&r)&&!r);}raw_read(b,sizeof b);for(int i=0;i<8;i++){char expected[]="T0=value";expected[1]+=i;assert(strstr(b,expected));}
 pid_t kids[2];for(int i=0;i<2;i++){kids[i]=fork();assert(kids[i]>=0);if(!kids[i]){u16 key[]={'P',(u16)('0'+i),0};_exit(WritePrivateProfileStringW(W("Processes"),key,W("child"),path)?0:1);}}
 for(int i=0;i<2;i++){int status;assert(waitpid(kids[i],&status,0)==kids[i]&&WIFEXITED(status)&&!WEXITSTATUS(status));}raw_read(b,sizeof b);assert(strstr(b,"P0=child")&&strstr(b,"P1=child"));
 DIR*d=opendir(directory);assert(d);struct dirent*entry;while((entry=readdir(d)))assert(!strstr(entry->d_name,".tmp"));closedir(d);assert(!unlink(filename));snprintf(filename,sizeof filename,"%s/.thos-profile.lock",directory);assert(!unlink(filename)&&!rmdir(directory));
 puts("profile C bridge HOST actual /tmp IO: short writes, ACP1252/UTF16 persistence, injected ENOSPC/fsync/rename/mmap failures preserve original, temp cleanup,8threads/2process updates PASS; separate THOS PE proof required");}
