// SPDX-License-Identifier: GPL-2.0-or-later
// Real PE process repeatedly creates/draws a window, then exits via WM_CLOSE.
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
int main(void){
 int fd=open("/dev/winsys",O_RDWR|O_CLOEXEC);if(fd<0)return 1;
 uint64_t baseline=0;
 for(int i=0;i<20;i++){
  pid_t child=fork();if(child<0)return 1;
  if(!child){execl("/winhello.exe","winhello.exe",NULL);_exit(2);}
  uint32_t ev[8],hwnd=0;uint64_t deadline=ms()+5000;
  while(event(fd,ev,deadline)){if(ev[0]==1){hwnd=ev[1];break;}}
  if(!hwnd){puts("winlife FAIL: no created window");return 1;}
  long address=syscall(SYS_ioctl,fd,0x7701,(unsigned long)hwnd);if(address<=0)return 1;
  volatile uint32_t *pixels=(void*)address;deadline=ms()+5000;
  while((pixels[20*300+250]&0xffffff)!=0x1ea0c8&&ms()<deadline)usleep(1000);
  if((pixels[20*300+250]&0xffffff)!=0x1ea0c8){puts("winlife FAIL: real GDI painting missing");return 1;}
  uint32_t close[8]={12,hwnd};if(write(fd,close,32)!=32)return 1;
  int destroyed=0;deadline=ms()+5000;
  while(event(fd,ev,deadline)){if(ev[0]==2&&ev[1]==hwnd){destroyed=1;break;}}
  if(!destroyed){puts("winlife FAIL: exited owner left its window/store alive");return 1;}
  if(syscall(SYS_ioctl,fd,0x7702,(unsigned long)address)!=0){puts("winlife FAIL: unmap");return 1;}
  if(syscall(SYS_ioctl,fd,0x7702,(unsigned long)address)!=-1){puts("winlife FAIL: duplicate unmap accepted");return 1;}
  int status;if(waitpid(child,&status,0)!=child||!WIFEXITED(status)||WEXITSTATUS(status)!=0)return 1;
  usleep(50000);if(i==2)baseline=free_bytes();
 }
 usleep(200000);uint64_t after=free_bytes();
 printf("winlife free baseline:%llu after:%llu\n",(unsigned long long)baseline,(unsigned long long)after);
 if(after+1024*1024<baseline){puts("winlife FAIL: backing-store memory grew after exit");return 1;}
 close(fd);puts("winlife-test-done");return 0;
}
