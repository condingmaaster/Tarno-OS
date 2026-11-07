// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>
static unsigned char *concurrent;
static pthread_barrier_t barrier;
static void *touch(void *arg) {
    uintptr_t i=(uintptr_t)arg;pthread_barrier_wait(&barrier);
    for(int round=0;round<16;round++) {concurrent[(round*8+i)*4096]=(unsigned char)(i+round+1);sched_yield();}
    return 0;
}
static unsigned long long free_bytes(void) {struct sysinfo info;assert(sysinfo(&info)==0);return (unsigned long long)info.freeram*info.mem_unit;}
static void child_status(pid_t child,int sig) {int s;assert(waitpid(child,&s,0)==child);if(sig)assert(WIFSIGNALED(s)&&WTERMSIG(s)==sig);else assert(WIFEXITED(s)&&WEXITSTATUS(s)==0);}
int main(void) {
    const size_t size=1ULL<<30,page=4096,count=size/page;
    unsigned char *residency=calloc(count,1);assert(residency);memset(residency,0,count);
    unsigned long long before=free_bytes();
    unsigned char *p=mmap(0,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(p!=MAP_FAILED);
    assert(mincore(p,size,residency)==0);for(size_t i=0;i<count;i++)assert(!(residency[i]&1));
    p[0]=0x51;p[size-1]=0x72;assert(p[100*page]==0);
    assert(mincore(p,page,residency)==0&&residency[0]&1);
    assert(mincore(p+page,page,residency)==0&&!(residency[0]&1));
    unsigned long long after=free_bytes();assert(before<=after||before-after<8*1024*1024);
    printf("anonymous committed: %llu bytes for 1 GiB reservation\n",before>after?before-after:0);
    assert(mprotect(p,page,PROT_NONE)==0);assert(mincore(p,page,residency)==0&&residency[0]&1);
    pid_t child=fork();assert(child>=0);
    if(!child){assert(mprotect(p,page,PROT_READ|PROT_WRITE)==0&&p[0]==0x51&&p[size-1]==0x72);p[0]=0x81;p[size-1]=0x82;_exit(0);}child_status(child,0);
    assert(mprotect(p,page,PROT_READ|PROT_WRITE)==0&&p[0]==0x51&&p[size-1]==0x72);
    child=fork();assert(child>=0);if(!child){assert(mprotect(p,page,PROT_READ)==0);*(volatile unsigned char*)p=9;_exit(90);}child_status(child,SIGSEGV);
    assert(madvise(p,page,MADV_DONTNEED)==0);assert(mincore(p,page,residency)==0&&!(residency[0]&1));assert(p[0]==0);
    assert(munmap(p+16*page,page)==0);errno=0;assert(mincore(p+16*page,page,residency)==-1&&errno==ENOMEM);
    assert(mmap(p+16*page,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==p+16*page&&p[16*page]==0);
    int fd=open("/tmp/anonymous-file",O_CREAT|O_TRUNC|O_RDWR,0600);assert(fd>=0&&write(fd,"kernelcopy",10)==10&&lseek(fd,0,SEEK_SET)==0);
    assert(read(fd,p+20*page,10)==10&&!memcmp(p+20*page,"kernelcopy",10));close(fd);unlink("/tmp/anonymous-file");
    unsigned char *code=mmap(0,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(code!=MAP_FAILED);
    memcpy(code,"\xb8\x2a\x00\x00\x00\xc3",6);assert(mprotect(code,page,PROT_READ|PROT_EXEC)==0);assert(((int(*)(void))code)()==42);assert(munmap(code,page)==0);
    concurrent=mmap(0,128*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(concurrent!=MAP_FAILED);
    assert(pthread_barrier_init(&barrier,0,9)==0);pthread_t threads[8];for(uintptr_t i=0;i<8;i++)assert(pthread_create(&threads[i],0,touch,(void*)i)==0);
    pthread_barrier_wait(&barrier);for(int i=0;i<8;i++){assert(pthread_join(threads[i],0)==0);for(int round=0;round<16;round++)assert(concurrent[(round*8+i)*page]==i+round+1);}pthread_barrier_destroy(&barrier);
    assert(munmap(concurrent,128*page)==0&&munmap(p,size)==0);free(residency);
    child=fork();assert(child>=0);if(!child) {
        int exhausted=0;
        for(int i=0;i<300;i++){void *v=mmap(0,1ULL<<36,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(v==MAP_FAILED){assert(errno==ENOMEM);exhausted=1;break;}assert((uintptr_t)v+(1ULL<<36)<0x800000000000ULL);}
        assert(exhausted);_exit(0);
    }child_status(child,0);
    puts("anonymous-test-done: sparse RAM, zero fill, protections, fork isolation, fixed replacement, kernel copies and concurrent faults");return 0;
}
