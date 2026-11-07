// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
static const char *path="/tmp/thos-flock-regression";
static void busy(int fd,int mode){errno=0;assert(flock(fd,mode|LOCK_NB)==-1&&errno==EWOULDBLOCK);}
static void joined(pid_t p){int s;assert(waitpid(p,&s,0)==p&&WIFEXITED(s)&&WEXITSTATUS(s)==0);}
static void caught(int s){(void)s;}
int main(int argc,char **argv){
 if(argc==3&&!strcmp(argv[1],"--exec")){
  int fd=atoi(argv[2]),other=open(path,O_RDONLY);assert(other>=0);
  assert(flock(fd,LOCK_EX|LOCK_NB)==0);busy(other,LOCK_EX);close(other);close(fd);return 0;
 }
 setbuf(stdout,0);unlink(path);
 int a=open(path,O_CREAT|O_RDWR,0600),b=open(path,O_RDONLY);assert(a>=0&&b>=0);
 int first=flock(a,LOCK_EX|LOCK_NB);
 if(first<0){printf("flock-missing errno=%d\n",errno);return 1;}
 busy(b,LOCK_EX);busy(b,LOCK_SH);assert(flock(a,LOCK_UN)==0);
 assert(flock(a,LOCK_SH)==0&&flock(b,LOCK_SH)==0);busy(a,LOCK_EX);
 // Failed conversion releases a's old shared lock on Linux.
 assert(flock(b,LOCK_EX|LOCK_NB)==0);assert(flock(b,LOCK_UN)==0);
 assert(flock(a,LOCK_EX)==0);int alias=dup(a);assert(alias>=0);close(a);busy(b,LOCK_EX);
 assert(flock(alias,LOCK_UN)==0&&flock(b,LOCK_EX)==0&&flock(b,LOCK_UN)==0);
 assert(flock(alias,LOCK_EX)==0);close(alias);assert(flock(b,LOCK_EX|LOCK_NB)==0);
 puts("flock-ok shared/exclusive, conversion, dup and last-close");
 int pipes[2];assert(pipe(pipes)==0);pid_t p=fork();assert(p>=0);
 if(!p){close(pipes[0]);int other=open(path,O_RDWR);assert(other>=0);busy(other,LOCK_EX);
  assert(flock(b,LOCK_UN)==0);assert(flock(other,LOCK_EX|LOCK_NB)==0);assert(write(pipes[1],"x",1)==1);
  close(other);close(b);close(pipes[1]);_exit(0);}
 close(pipes[1]);char c;assert(read(pipes[0],&c,1)==1);close(pipes[0]);joined(p);
 assert(flock(b,LOCK_EX|LOCK_NB)==0);puts("flock-ok fork shares description and independent open conflicts");
 assert(pipe(pipes)==0);p=fork();assert(p>=0);
 if(!p){close(pipes[0]);close(b);int other=open(path,O_RDONLY);assert(other>=0);
  assert(write(pipes[1],"r",1)==1);assert(flock(other,LOCK_EX)==0);assert(write(pipes[1],"a",1)==1);
  close(other);close(pipes[1]);_exit(0);}
 close(pipes[1]);assert(read(pipes[0],&c,1)==1);usleep(30000);int s;assert(waitpid(p,&s,WNOHANG)==0);
 assert(flock(b,LOCK_UN)==0);assert(read(pipes[0],&c,1)==1&&c=='a');close(pipes[0]);joined(p);
 puts("flock-ok blocking waits for real release");
 assert(flock(b,LOCK_EX)==0);p=fork();assert(p>=0);
 if(!p){close(b);int other=open(path,O_RDONLY);assert(other>=0);
  struct sigaction sa={0};sa.sa_handler=caught;sigemptyset(&sa.sa_mask);assert(sigaction(SIGALRM,&sa,0)==0);
  alarm(1);errno=0;assert(flock(other,LOCK_EX)==-1&&errno==EINTR);close(other);_exit(0);}
 joined(p);assert(flock(b,LOCK_UN)==0);puts("flock-ok signal interrupts waiting");
 for(int mode=0;mode<3;mode++){
  if(mode==2){int grow=open(path,O_RDWR);assert(grow>=0&&ftruncate(grow,524288)==0);close(grow);}
  int held=open(path,mode==0?O_RDWR:O_RDONLY),ready[2];assert(held>=0&&flock(held,LOCK_EX)==0&&pipe(ready)==0);p=fork();assert(p>=0);
  if(!p){close(ready[0]);close(held);int other=open(path,O_RDONLY);assert(other>=0&&write(ready[1],"r",1)==1&&flock(other,LOCK_EX)==0&&write(ready[1],"a",1)==1);close(other);_exit(0);}
  close(ready[1]);assert(read(ready[0],&c,1)==1);usleep(30000);assert(waitpid(p,&s,WNOHANG)==0);close(held);assert(read(ready[0],&c,1)==1&&c=='a');close(ready[0]);joined(p);printf("flock-ok blocked waiter wakes on final close mode=%d\n",mode);
 }
 errno=0;assert(flock(-1,LOCK_EX)==-1&&errno==EBADF);
 errno=0;assert(flock(b,0)==-1&&errno==EINVAL);errno=0;assert(flock(b,LOCK_SH|LOCK_EX)==-1&&errno==EINVAL);
 errno=0;assert(flock(b,0x100|LOCK_EX)==-1&&errno==EINVAL);
 assert(flock(b,LOCK_EX)==0);p=fork();assert(p>=0);
 if(!p){char fd[24];snprintf(fd,sizeof fd,"%d",b);execl(argv[0],argv[0],"--exec",fd,(char*)0);_exit(99);}
 joined(p);assert(flock(b,LOCK_EX|LOCK_NB)==0);close(b);
 int check=open(path,O_RDONLY);assert(check>=0&&flock(check,LOCK_EX|LOCK_NB)==0);close(check);unlink(path);
 puts("flock-test-done: conflicts, conversion, dup, close, fork, blocking, EINTR, exec and errors");return 0;
}
