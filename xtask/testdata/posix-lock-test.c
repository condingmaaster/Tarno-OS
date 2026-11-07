// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
_Static_assert(sizeof(struct flock)==32&&offsetof(struct flock,l_start)==8&&offsetof(struct flock,l_pid)==24,"Linux x86-64 flock layout");
static const char *path="/tmp/thos-posix-lock-test";
static int setlock(int fd,int cmd,int type,off_t start,off_t len,int whence){struct flock l={.l_type=type,.l_whence=whence,.l_start=start,.l_len=len};return fcntl(fd,cmd,&l);}
static void joined(pid_t p){int s;assert(waitpid(p,&s,0)==p&&WIFEXITED(s)&&WEXITSTATUS(s)==0);}
static void query_child(int fd,int want,off_t start,off_t len,int expected,off_t locked_start,off_t locked_len,pid_t owner){
 pid_t p=fork();assert(p>=0);if(!p){struct flock l={.l_type=want,.l_whence=SEEK_SET,.l_start=start,.l_len=len,.l_pid=-123};assert(fcntl(fd,F_GETLK,&l)==0);assert(l.l_type==expected);if(expected!=F_UNLCK)assert(l.l_whence==SEEK_SET&&l.l_start==locked_start&&l.l_len==locked_len&&l.l_pid==owner);_exit(0);}joined(p);}
static void caught(int sig){(void)sig;}
int main(int argc,char**argv){
 if(argc==4&&!strcmp(argv[1],"--exec")){int ready=atoi(argv[2]),go=atoi(argv[3]);char c;assert(write(ready,"r",1)==1&&read(go,&c,1)==1);return 0;}
 setbuf(stdout,0);unlink(path);int a=open(path,O_CREAT|O_RDWR,0600),b=open(path,O_RDWR);assert(a>=0&&b>=0&&ftruncate(a,128)==0);pid_t parent=getpid();
 assert(setlock(a,F_SETLK,F_WRLCK,10,10,SEEK_SET)==0);
 pid_t p=fork();assert(p>=0);if(!p){struct flock l={.l_type=F_WRLCK,.l_whence=SEEK_SET,.l_start=10,.l_len=10,.l_pid=-123};int q=fcntl(a,F_GETLK,&l);errno=0;int r=setlock(a,F_SETLK,F_RDLCK,10,10,SEEK_SET),e=errno;printf("posix-crossprocess query=%d type=%d owner=%d expected-owner=%d collision=%d errno=%d\n",q,l.l_type,l.l_pid,parent,r,e);assert(q==0&&l.l_type==F_WRLCK&&l.l_pid==parent&&l.l_start==10&&l.l_len==10&&r==-1&&(e==EAGAIN||e==EACCES));assert(pwrite(a,"x",1,10)==1);_exit(0);}joined(p);
 struct flock own={.l_type=F_WRLCK,.l_whence=SEEK_SET,.l_len=0,.l_pid=-77};assert(fcntl(b,F_GETLK,&own)==0&&own.l_type==F_UNLCK&&own.l_pid==-77);
 query_child(a,F_WRLCK,10,1,F_WRLCK,10,10,parent);assert(setlock(b,F_SETLK,F_UNLCK,0,0,SEEK_SET)==0);
 assert(setlock(a,F_SETLK,F_WRLCK,0,100,SEEK_SET)==0&&setlock(a,F_SETLK,F_UNLCK,20,20,SEEK_SET)==0);
 query_child(a,F_WRLCK,20,20,F_UNLCK,0,0,0);query_child(a,F_RDLCK,0,20,F_WRLCK,0,20,parent);query_child(a,F_RDLCK,40,60,F_WRLCK,40,60,parent);
 assert(setlock(a,F_SETLK,F_RDLCK,20,20,SEEK_SET)==0);query_child(a,F_RDLCK,20,20,F_UNLCK,0,0,0);query_child(a,F_WRLCK,20,20,F_RDLCK,20,20,parent);
 assert(setlock(a,F_SETLK,F_UNLCK,0,0,SEEK_SET)==0);
 assert(setlock(a,F_SETLK,F_WRLCK,40,-10,SEEK_SET)==0);query_child(a,F_RDLCK,30,10,F_WRLCK,30,10,parent);
 assert(lseek(a,100,SEEK_SET)==100&&setlock(a,F_SETLK,F_WRLCK,-60,10,SEEK_CUR)==0);query_child(a,F_RDLCK,30,20,F_WRLCK,30,20,parent);
 assert(setlock(a,F_SETLK,F_WRLCK,-20,10,SEEK_END)==0);query_child(a,F_RDLCK,108,10,F_WRLCK,108,10,parent);
 close(b);query_child(a,F_WRLCK,0,0,F_UNLCK,0,0,0);puts("posix-ok ranges, query, conversion, merge, split, negative length and any-close");
 assert(setlock(a,F_SETLK,F_WRLCK,0,0,SEEK_SET)==0);int alias=dup(a);assert(alias>=0);close(alias);query_child(a,F_WRLCK,0,0,F_UNLCK,0,0,0);
 b=open(path,O_RDONLY);assert(b>=0);assert(setlock(a,F_SETLK,F_WRLCK,0,0,SEEK_SET)==0);int pipes[2];assert(pipe(pipes)==0&&dup2(pipes[0],b)==b);query_child(a,F_WRLCK,0,0,F_UNLCK,0,0,0);close(b);close(pipes[0]);close(pipes[1]);
 puts("posix-ok dup close and descriptor replacement release process locks");
 assert(setlock(a,F_SETLK,F_WRLCK,0,0,SEEK_SET)==0);assert(pipe(pipes)==0);p=fork();assert(p>=0);
 if(!p){close(pipes[0]);int fd=open(path,O_RDONLY);assert(fd>=0&&write(pipes[1],"r",1)==1&&setlock(fd,F_SETLKW,F_RDLCK,0,0,SEEK_SET)==0&&write(pipes[1],"a",1)==1);close(fd);_exit(0);}
 close(pipes[1]);char c;assert(read(pipes[0],&c,1)==1);usleep(30000);int status;assert(waitpid(p,&status,WNOHANG)==0);assert(setlock(a,F_SETLK,F_UNLCK,0,0,SEEK_SET)==0&&read(pipes[0],&c,1)==1&&c=='a');close(pipes[0]);joined(p);
 assert(setlock(a,F_SETLK,F_WRLCK,0,0,SEEK_SET)==0);p=fork();assert(p>=0);if(!p){struct sigaction sa={0};sa.sa_handler=caught;sigemptyset(&sa.sa_mask);assert(sigaction(SIGALRM,&sa,0)==0);alarm(1);errno=0;assert(setlock(a,F_SETLKW,F_RDLCK,0,0,SEEK_SET)==-1&&errno==EINTR);_exit(0);}joined(p);assert(setlock(a,F_SETLK,F_UNLCK,0,0,SEEK_SET)==0);puts("posix-ok blocking and signal interruption");
 for(int clo=0;clo<2;clo++){
  int ready[2],go[2];assert(pipe(ready)==0&&pipe(go)==0);p=fork();assert(p>=0);
  if(!p){close(ready[0]);close(go[1]);int fd=open(path,O_RDWR),other=open(path,O_RDONLY);assert(fd>=0&&other>=0&&setlock(fd,F_SETLK,F_WRLCK,0,0,SEEK_SET)==0);if(clo)assert(fcntl(other,F_SETFD,FD_CLOEXEC)==0);char r[24],g[24];snprintf(r,sizeof r,"%d",ready[1]);snprintf(g,sizeof g,"%d",go[0]);execl(argv[0],argv[0],"--exec",r,g,(char*)0);_exit(99);}
  close(ready[1]);close(go[0]);assert(read(ready[0],&c,1)==1);struct flock q={.l_type=F_WRLCK,.l_whence=SEEK_SET,.l_len=0};assert(fcntl(a,F_GETLK,&q)==0&&q.l_type==(clo?F_UNLCK:F_WRLCK));if(!clo)assert(q.l_pid==p);assert(write(go[1],"g",1)==1);close(ready[0]);close(go[1]);joined(p);query_child(a,F_WRLCK,0,0,F_UNLCK,0,0,0);
 }
 puts("posix-ok exec preserves locks, close-on-exec and exit release them");
 int ready_cycle[2],go_cycle[2];assert(pipe(ready_cycle)==0&&pipe(go_cycle)==0&&setlock(a,F_SETLK,F_WRLCK,0,1,SEEK_SET)==0);
 p=fork();assert(p>=0);if(!p){close(ready_cycle[0]);close(go_cycle[1]);assert(setlock(a,F_SETLK,F_WRLCK,1,1,SEEK_SET)==0&&write(ready_cycle[1],"r",1)==1&&read(go_cycle[0],&c,1)==1);errno=0;int r=setlock(a,F_SETLKW,F_WRLCK,0,1,SEEK_SET),e=errno;assert(r==0||(r==-1&&e==EDEADLK));assert(setlock(a,F_SETLK,F_UNLCK,0,0,SEEK_SET)==0);char result=r==0?'a':'d';assert(write(ready_cycle[1],&result,1)==1);_exit(0);}
 close(ready_cycle[1]);close(go_cycle[0]);assert(read(ready_cycle[0],&c,1)==1&&write(go_cycle[1],"g",1)==1);errno=0;int cycle=setlock(a,F_SETLKW,F_WRLCK,1,1,SEEK_SET),cycle_errno=errno;assert(cycle==0||(cycle==-1&&cycle_errno==EDEADLK));assert(setlock(a,F_SETLK,F_UNLCK,0,0,SEEK_SET)==0&&read(ready_cycle[0],&c,1)==1);assert((cycle==0&&c=='d')||(cycle==-1&&c=='a'));close(ready_cycle[0]);close(go_cycle[1]);joined(p);puts("posix-ok deadlock detected and released");
 int ro=open(path,O_RDONLY),wo=open(path,O_WRONLY);assert(ro>=0&&wo>=0);errno=0;assert(setlock(ro,F_SETLK,F_WRLCK,0,1,SEEK_SET)==-1&&errno==EBADF);errno=0;assert(setlock(wo,F_SETLK,F_RDLCK,0,1,SEEK_SET)==-1&&errno==EBADF);
 errno=0;assert(setlock(a,F_SETLK,F_WRLCK,-1,1,SEEK_SET)==-1&&errno==EINVAL);errno=0;assert(setlock(a,F_SETLK,99,0,1,SEEK_SET)==-1&&errno==EINVAL);errno=0;assert(fcntl(a,F_GETLK,(void*)0)==-1&&errno==EFAULT);
 close(ro);close(wo);close(a);unlink(path);puts("posix-lock-test-done: ranges, real conflicts, lifecycle, blocking, exec and access");return 0;
}
