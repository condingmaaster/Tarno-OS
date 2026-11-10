// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>
#define CHUNK (1024*1024)
static void pattern(unsigned char *b,unsigned i) {for(size_t p=0;p<CHUNK;p++)b[p]=(unsigned char)(p*17+i*31);}
static void descriptor_cache_regression(unsigned char *b,unsigned char *ref) {
    int a=open("/tmp/shared-write-cache",O_CREAT|O_TRUNC|O_RDWR,0600);
    int other=open("/tmp/reused-cache-blocks",O_CREAT|O_TRUNC|O_RDWR,0600);
    int second=open("/tmp/shared-write-cache",O_RDWR);assert(a>=0&&other>=0&&second>=0);
    struct statvfs before,after;assert(statvfs("/",&before)==0);
    pattern(b,41);assert(write(a,b,CHUNK)==CHUNK);
    assert(ftruncate(second,0)==0);memset(b,0xc7,CHUNK);assert(write(other,b,CHUNK)==CHUNK);
    pattern(ref,92);assert(write(second,ref,CHUNK)==CHUNK);
    assert(pwrite(a,"Q",1,123)==1&&lseek(a,0,SEEK_CUR)==CHUNK);
    ref[123]='Q';assert(pread(second,b,CHUNK,0)==CHUNK&&!memcmp(b,ref,CHUNK));
    assert(pread(other,b,CHUNK,0)==CHUNK);for(int i=0;i<CHUNK;i++)assert(b[i]==0xc7);
    assert(pwrite(second,"R",1,456)==1);ref[456]='R';
    assert(pwrite(a,"S",1,789)==1);ref[789]='S';
    assert(pread(a,b,CHUNK,0)==CHUNK&&!memcmp(b,ref,CHUNK));
    assert(ftruncate(a,0)==0&&ftruncate(other,0)==0);
    assert(statvfs("/",&after)==0&&after.f_bfree==before.f_bfree);
    close(a);close(second);close(other);unlink("/tmp/shared-write-cache");unlink("/tmp/reused-cache-blocks");
    puts("descriptor cache invalidation and block reuse passed");
}
int main(void) {
    unsigned char *b=malloc(CHUNK),*ref=malloc(CHUNK);assert(b&&ref);
    int f=open("/tmp/large-write-test",O_CREAT|O_TRUNC|O_RDWR,0600);assert(f>=0);
    struct statvfs before,after;assert(statvfs("/",&before)==0);
    for(unsigned i=0;i<128;i++) {
        pattern(b,i);assert(write(f,b,CHUNK)==CHUNK);
        if(i%16==15){printf("large-write progress: %u MiB\n",i+1);fflush(stdout);}
    }
    assert(fsync(f)==0&&close(f)==0);f=open("/tmp/large-write-test",O_RDWR);assert(f>=0);
    for(unsigned i=0;i<128;i++){pattern(ref,i);assert(read(f,b,CHUNK)==CHUNK&&!memcmp(b,ref,CHUNK));}
    assert(lseek(f,23,SEEK_SET)==23);assert(pwrite(f,"patch",5,64*CHUNK+17)==5&&lseek(f,0,SEEK_CUR)==23);
    assert(pread(f,b,5,64*CHUNK+17)==5&&!memcmp(b,"patch",5));
    assert(ftruncate(f,64*CHUNK+123)==0);assert(ftruncate(f,64*CHUNK+8192)==0);
    memset(b,1,8069);assert(pread(f,b,8069,64*CHUNK+123)==8069);for(int i=0;i<8069;i++)assert(b[i]==0);
    assert(ftruncate(f,0)==0);assert(statvfs("/",&after)==0&&after.f_bfree==before.f_bfree);
    assert(ftruncate(f,512LL*CHUNK)==0);assert(statvfs("/",&after)==0&&after.f_bfree==before.f_bfree);
    assert(pwrite(f,"Z",1,511LL*CHUNK+19)==1);assert(pread(f,b,32,511LL*CHUNK)==32);
    for(int i=0;i<32;i++)assert(b[i]==(i==19?'Z':0));
    assert(ftruncate(f,0)==0);assert(statvfs("/",&after)==0&&after.f_bfree==before.f_bfree);
    assert(close(f)==0&&unlink("/tmp/large-write-test")==0);
    descriptor_cache_regression(b,ref);
    free(b);free(ref);puts("large-write-test-done: 128 MiB byte match, positional patch, shrink/grow zeros, 512 MiB sparse extent and complete block reclamation");return 0;
}
