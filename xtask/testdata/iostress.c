// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#define N 4
static int source,dest;
static uint64_t expected[256],counts[N][256],bytes[N];
static unsigned char *reference;
static size_t reference_size;
static void *positional_reader(void *arg) {
    size_t id=(size_t)arg;unsigned char buf[777];
    for(size_t p=id*777;p<reference_size;p+=N*777) {
        size_t n=reference_size-p;if(n>sizeof buf)n=sizeof buf;
        assert(pread(source,buf,n,p)==(ssize_t)n);
        assert(!memcmp(buf,reference+p,n));
    }
    return 0;
}
static void *reader(void *arg) {
    size_t id=(size_t)arg;unsigned char data[513];
    for(;;) {ssize_t n=read(source,data,sizeof data);assert(n>=0);if(!n)break;
        bytes[id]+=n;for(ssize_t i=0;i<n;i++)counts[id][data[i]]++;
    }
    return 0;
}
static void *writer(void *arg) {
    unsigned char data[4096];memset(data,0xa0+(size_t)arg,sizeof data);
    for(int i=0;i<64;i++)assert(write(dest,data,sizeof data)==sizeof data);
    return 0;
}
int main(void) {
    int ref=open("/rg.exe",O_RDONLY);assert(ref>=0);unsigned char data[65536];uint64_t total=0;
    for(;;) {ssize_t n=read(ref,data,sizeof data);assert(n>=0);if(!n)break;
        total+=n;for(ssize_t i=0;i<n;i++)expected[data[i]]++;
    }
    close(ref);source=open("/rg.exe",O_RDONLY);assert(source>=0);
    pthread_t threads[N];
    for(size_t i=0;i<N;i++)assert(pthread_create(&threads[i],0,reader,(void*)i)==0);
    for(int i=0;i<N;i++)assert(pthread_join(threads[i],0)==0);
    uint64_t got=0;for(int i=0;i<N;i++)got+=bytes[i];assert(got==total);
    for(int c=0;c<256;c++) {uint64_t sum=0;for(int i=0;i<N;i++)sum+=counts[i][c];assert(sum==expected[c]);}
    close(source);puts("shared stream reads passed");fflush(stdout);
    source=open("/rg.exe",O_RDONLY);assert(source>=0);
    reference_size=total;reference=malloc(reference_size);assert(reference);
    assert(read(source,reference,reference_size)==(ssize_t)reference_size);
    assert(lseek(source,19,SEEK_SET)==19);
    for(size_t i=0;i<N;i++)assert(pthread_create(&threads[i],0,positional_reader,(void*)i)==0);
    for(int i=0;i<N;i++)assert(pthread_join(threads[i],0)==0);
    assert(lseek(source,0,SEEK_CUR)==19);
    void *mapped=mmap(0,8192,PROT_READ,MAP_PRIVATE,source,4096);
    assert(mapped!=MAP_FAILED&&!memcmp(mapped,reference+4096,8192));
    assert(lseek(source,0,SEEK_CUR)==19);assert(munmap(mapped,8192)==0);
    free(reference);close(source);
    int mem=memfd_create("positional-test",0);assert(mem>=0&&ftruncate(mem,8192)==0);
    assert(write(mem,"memfd-data",10)==10&&lseek(mem,3,SEEK_SET)==3);
    assert(pread(mem,data,10,0)==10&&!memcmp(data,"memfd-data",10));
    mapped=mmap(0,4096,PROT_READ,MAP_PRIVATE,mem,0);
    assert(mapped!=MAP_FAILED&&!memcmp(mapped,"memfd-data",10));
    assert(lseek(mem,0,SEEK_CUR)==3);assert(munmap(mapped,4096)==0);close(mem);
    puts("positional reads and mmap passed");fflush(stdout);
    dest=open("/tmp/concurrent-write",O_CREAT|O_TRUNC|O_WRONLY,0600);assert(dest>=0);
    for(size_t i=0;i<N;i++)assert(pthread_create(&threads[i],0,writer,(void*)i)==0);
    for(int i=0;i<N;i++)assert(pthread_join(threads[i],0)==0);
    assert(close(dest)==0);ref=open("/tmp/concurrent-write",O_RDONLY);assert(ref>=0);
    uint64_t seen[N]={0};total=0;
    for(;;) {ssize_t n=read(ref,data,sizeof data);assert(n>=0);if(!n)break;
        total+=n;for(ssize_t i=0;i<n;i++) {assert(data[i]>=0xa0&&data[i]<0xa0+N);seen[data[i]-0xa0]++;}
    }
    for(int i=0;i<N;i++)assert(seen[i]==64*4096);
    assert(total==N*64*4096);close(ref);
    puts("concurrent durable writes passed");puts("iostress-done");return 0;
}
