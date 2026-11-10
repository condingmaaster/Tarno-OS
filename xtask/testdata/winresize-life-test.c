// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise actual PE/GDI drawing across repeated backing-store generations.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>
#include <time.h>
static uint64_t ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static uint64_t free_bytes(void){struct sysinfo s;if(sysinfo(&s))exit(2);return (uint64_t)s.freeram*s.mem_unit;}
static int event(int fd,uint32_t *ev,uint64_t deadline){while(ms()<deadline){struct pollfd p={fd,POLLIN,0};int n=poll(&p,1,50);if(n>0&&read(fd,ev,32)==32)return 1;if(n<0)return 0;}return 0;}
static int corner(long va,unsigned w,unsigned h){
 volatile uint32_t *p=(void*)va;uint64_t deadline=ms()+7000;
 while((p[(h-10)*w+w-10]&0xffffff)!=0xfadc28&&ms()<deadline)usleep(1000);
 return (p[(h-10)*w+w-10]&0xffffff)==0xfadc28;
}
int main(void){
 int fd=open("/dev/winsys",O_RDWR|O_CLOEXEC);if(fd<0)return 1;
 pid_t child=fork();if(child<0)return 1;if(!child){execl("/winresize.exe","winresize.exe",NULL);_exit(2);}
 uint32_t ev[8],hwnd=0,gen=0;uint64_t deadline=ms()+7000;
 while(event(fd,ev,deadline))if(ev[0]==1){hwnd=ev[1];gen=ev[6];break;}
 if(!hwnd||!gen)return 1;
 long va=syscall(SYS_ioctl,fd,0x7701,((uint64_t)gen<<32)|hwnd);if(va<=0||!corner(va,400,240))return 1;
 uint32_t invalid[8]={15,hwnd,0,800};
 if(write(fd,invalid,32)!=-1){puts("resize-life FAIL: empty size accepted");return 1;}
 unsigned previous_w=400,previous_h=240;uint64_t baseline=0;
 for(unsigned i=0;i<40;i++){
  unsigned w=i%2?480:1280,h=i%2?300:800;uint32_t request[8]={15,hwnd,w,h,i%2?0:2};
  if(write(fd,request,32)!=32)return 1;
  int changed=0;deadline=ms()+7000;
  while(event(fd,ev,deadline))if(ev[0]==4&&ev[1]==hwnd){changed=1;break;}
  if(!changed||ev[2]!=w||ev[3]!=h||ev[6]==gen)return 1;
  if(syscall(SYS_ioctl,fd,0x7701,((uint64_t)gen<<32)|hwnd)!=-1){puts("resize-life FAIL: stale generation mapped");return 1;}
  if(!corner(va,previous_w,previous_h)){puts("resize-life FAIL: old view lost its backing frames");return 1;}
  long next=syscall(SYS_ioctl,fd,0x7701,((uint64_t)ev[6]<<32)|hwnd);if(next<=0||!corner(next,w,h))return 1;
  if(syscall(SYS_ioctl,fd,0x7702,(unsigned long)va)!=0)return 1;
  va=next;gen=ev[6];previous_w=w;previous_h=h;
  if(i==3)baseline=free_bytes();
 }
 uint64_t after=free_bytes();printf("resize-life free baseline:%llu after:%llu\n",(unsigned long long)baseline,(unsigned long long)after);
 if(after+1024*1024<baseline){puts("resize-life FAIL: retired buffers accumulated");return 1;}
 uint32_t close_record[8]={12,hwnd};if(write(fd,close_record,32)!=32)return 1;
 int destroyed=0;deadline=ms()+7000;while(event(fd,ev,deadline))if(ev[0]==2&&ev[1]==hwnd){destroyed=1;break;}
 if(!destroyed||syscall(SYS_ioctl,fd,0x7702,(unsigned long)va)!=0)return 1;
 int status;if(waitpid(child,&status,0)!=child||!WIFEXITED(status)||WEXITSTATUS(status))return 1;
 close(fd);puts("winresize-life-test-done");return 0;
}
