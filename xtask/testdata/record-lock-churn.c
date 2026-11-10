// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
struct message {int stop,point,want;unsigned char model[128];};
static void transfer(int fd,void *buf,size_t size,int writing){char*p=buf;while(size){ssize_t n=writing?write(fd,p,size):read(fd,p,size);assert(n>0);p+=n;size-=n;}}
static uint32_t seed=0xaced1234;static unsigned random32(void){seed=seed*1664525u+1013904223u;return seed;}
static void query(int fd,const struct message*m,pid_t parent){struct flock q={.l_type=F_WRLCK,.l_whence=SEEK_SET,.l_start=m->point,.l_len=1,.l_pid=-1};assert(fcntl(fd,F_GETLK,&q)==0);int kind=m->model[m->point];assert(q.l_type==kind);if(kind!=F_UNLCK){int lo=m->point,hi=lo+1;while(lo&&m->model[lo-1]==kind)lo--;while(hi<128&&m->model[hi]==kind)hi++;assert(q.l_pid==parent&&q.l_whence==SEEK_SET&&q.l_start==lo&&q.l_len==hi-lo);}}
int main(void){setbuf(stdout,0);const char*path="/tmp/thos-record-lock-churn";unlink(path);int fd=open(path,O_CREAT|O_RDWR,0600),request[2],reply[2];assert(fd>=0&&ftruncate(fd,128)==0&&pipe(request)==0&&pipe(reply)==0);pid_t parent=getpid(),child=fork();assert(child>=0);
 if(!child){close(request[1]);close(reply[0]);for(;;){struct message m;transfer(request[0],&m,sizeof m,0);if(m.stop)break;query(fd,&m,parent);struct flock l={.l_type=m.want,.l_whence=SEEK_SET,.l_start=m.point,.l_len=1};errno=0;int r=fcntl(fd,F_SETLK,&l),e=errno;int blocked=m.model[m.point]==F_WRLCK||(m.want==F_WRLCK&&m.model[m.point]==F_RDLCK);assert(blocked?(r==-1&&(e==EAGAIN||e==EACCES)):r==0);query(fd,&m,parent);if(!r){l.l_type=F_UNLCK;assert(fcntl(fd,F_SETLK,&l)==0);}char ack='a';transfer(reply[1],&ack,1,1);}close(fd);_exit(0);}
 close(request[0]);close(reply[1]);struct message m={0};memset(m.model,F_UNLCK,sizeof m.model);
 for(int i=0;i<1000;i++){int lo=random32()%128,len=1+random32()%(128-lo),kind=random32()%3,whence=random32()%3,negative=random32()&1;off_t absolute=negative?lo+len:lo;assert(lseek(fd,64,SEEK_SET)==64);struct flock l={.l_type=kind,.l_whence=whence,.l_start=absolute-(whence==SEEK_CUR?64:whence==SEEK_END?128:0),.l_len=negative?-len:len};assert(fcntl(fd,F_SETLK,&l)==0);memset(m.model+lo,kind,len);m.point=random32()%128;m.want=random32()%2;transfer(request[1],&m,sizeof m,1);char ack;transfer(reply[0],&ack,1,0);assert(ack=='a');}
 m.stop=1;transfer(request[1],&m,sizeof m,1);int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);close(request[1]);close(reply[0]);close(fd);unlink(path);puts("record-lock-churn-done: 1000 mixed ranges compared with independent byte model in second process, canonical queries and actual read/write conflicts");return 0;}
