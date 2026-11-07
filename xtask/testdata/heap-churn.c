// SPDX-License-Identifier: GPL-2.0-or-later
// Guest workload for kernel allocation pressure, not a whole-OS speed score.
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static unsigned char chunk[65536];
static uint64_t millis(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void require(int ok,const char *what){if(!ok){perror(what);exit(1);}}
int main(void){
    char name[80];snprintf(name,sizeof name,"/tmp/heap-churn-%ld",(long)getpid());
    int fd=open(name,O_CREAT|O_TRUNC|O_RDWR,0600);require(fd>=0,"open");
    for(unsigned block=0;block<32;block++){
        for(unsigned i=0;i<16;i++){
            uint32_t first=block*16+i,last=first^0xdeadbeefU;
            memcpy(chunk+i*4096,&first,sizeof first);
            memcpy(chunk+i*4096+4092,&last,sizeof last);
        }
        require(write(fd,chunk,sizeof chunk)==sizeof chunk,"write");
    }
    require(fsync(fd)==0,"fsync");
    uint64_t start=0,io=0;
    for(unsigned round=0;round<34;round++){
        if(round==2)start=millis(); // warm filesystem caches before timing
        unsigned char *p=mmap(0,2<<20,PROT_READ,MAP_PRIVATE,fd,0);require(p!=MAP_FAILED,"file mmap");
        for(unsigned page=0;page<512;page++){
            require(*(uint32_t *)(p+page*4096)==page,"file first word");
            require(*(uint32_t *)(p+page*4096+4092)==(page^0xdeadbeefU),"file last word");
        }
        require(munmap(p,2<<20)==0,"file munmap");
    }
    io=millis()-start;close(fd);unlink(name);
    struct {unsigned char *p;size_t n;unsigned char value;} live[768]={0};
    uint32_t seed=31;start=millis();
    for(unsigned round=0;round<9000;round++){
        seed=seed*1664525U+1013904223U;unsigned index=(seed>>8)%768;
        if(live[index].p){
            require(live[index].p[0]==live[index].value && live[index].p[live[index].n-1]==(unsigned char)(live[index].value^0xa5),"anonymous payload");
            require(munmap(live[index].p,live[index].n)==0,"anonymous munmap");
        }
        size_t n=(2+(round%7))*4096;unsigned char value=(unsigned char)round;
        unsigned char *p=mmap(0,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);require(p!=MAP_FAILED,"anonymous mmap");
        p[0]=value;p[n-1]=value^0xa5;live[index].p=p;live[index].n=n;live[index].value=value;
    }
    for(unsigned i=0;i<768;i++)if(live[i].p){
        require(live[i].p[0]==live[i].value && live[i].p[live[i].n-1]==(unsigned char)(live[i].value^0xa5),"final payload");
        require(munmap(live[i].p,live[i].n)==0,"final munmap");
    }
    printf("heap-churn io-ms=%llu vma-ms=%llu\n",(unsigned long long)io,(unsigned long long)(millis()-start));
    puts("heap-churn-done");return 0;
}
