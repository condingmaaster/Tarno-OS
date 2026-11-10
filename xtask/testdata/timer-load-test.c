// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
static _Atomic int ready;
static _Atomic uint64_t finished;
static uint64_t busy_start;
static unsigned char *buf;
static uint64_t ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))exit(2);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void *worker(void *p){
 while(!atomic_load(&ready))sched_yield();
 busy_start=ns();
 // Older kernels fill the whole request with IF=0; bounded implementations
 // may return partial data, which the caller must consume correctly.
 size_t offset=0;
 while(offset<128*1024*1024){
  long n=syscall(SYS_getrandom,buf+offset,128*1024*1024-offset,0);
  if(n<=0||(size_t)n>128*1024*1024-offset)exit(3);
  offset+=(size_t)n;
 }
 atomic_store(&finished,ns());return 0;
}
int main(void){
 // A large request must report its actual prefix and leave the tail alone.
 unsigned char *sample=malloc(1024*1024);if(!sample)return 1;
 for(size_t i=0;i<1024*1024;i++)sample[i]=0xa5;
 long got=syscall(SYS_getrandom,sample,1024*1024,0);
 if(got<=0||got>1024*1024)return 1;
 for(size_t i=got;i<1024*1024;i++)if(sample[i]!=0xa5)return 1;
 int changed=0;for(long i=0;i<got;i++)changed|=sample[i]!=0xa5;
 if(!changed)return 1;free(sample);
 printf("random-large actual-bytes:%ld\n",got);
 for(int i=0;i<4;i++){
  uint64_t t=ns();struct timespec delay={0,20000000};if(nanosleep(&delay,0))return 1;
  uint64_t elapsed=ns()-t;if(elapsed<20000000||elapsed>500000000)return 1;
 }
 buf=mmap(0,128*1024*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(buf==MAP_FAILED)return 1;
 for(size_t i=0;i<128*1024*1024;i+=4096)buf[i]=0; // fault pages before entering the long request
 pthread_t thread;if(pthread_create(&thread,0,worker,0))return 1;
 uint64_t start=ns();atomic_store(&ready,1);
 struct timespec delay={0,300000000};if(nanosleep(&delay,0))return 1;
 uint64_t woke=ns();
 if(pthread_join(thread,0))return 1;
 uint64_t done=atomic_load(&finished),busy=done-busy_start;
 uint64_t late=woke>done?woke-done:0;
 printf("timer-load busy-ms:%llu wake-ms:%llu overdue-after-worker-ms:%llu\n",(unsigned long long)(busy/1000000),(unsigned long long)((woke-start)/1000000),(unsigned long long)(late/1000000));
 if(busy<400000000||woke<start+300000000||late>200000000)return 1;
 munmap(buf,128*1024*1024);puts("timer-load-test-done");return 0;
}
