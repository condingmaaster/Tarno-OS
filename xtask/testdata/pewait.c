// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <sys/syscall.h>
static int child(unsigned code,unsigned delay){char c[16],d[16];snprintf(c,sizeof c,"%u",code);snprintf(d,sizeof d,"%u",delay);char*args[]={"/winexit.exe",c,d,0};char*env[]={0};execve(args[0],args,env);_exit(99);}
int main(void){
 unsigned codes[]={0,1,42,127,255,301};int failures=0;
 for(unsigned round=0;round<12;round++)for(unsigned j=0;j<6;j++){pid_t p=fork();if(p<0)return 2;if(!p)child(codes[j],0);int status;if(waitpid(p,&status,0)!=p||!WIFEXITED(status)||WEXITSTATUS(status)!=(codes[j]&255))failures++;}
 printf("pewait-fast children=72 failures=%d\n",failures);
 pid_t delayed=fork();if(delayed<0)return 2;if(!delayed)child(42,80);int status=0;if(waitpid(delayed,&status,0)!=delayed||!WIFEXITED(status)||WEXITSTATUS(status)!=42)failures++;
 printf("pewait-delayed status=%d\n",status);
 pid_t any=fork();if(any<0)return 2;if(!any)_exit(17);status=0;long got=syscall(SYS_wait4,0xffffffffUL,&status,0,0);if(got!=any||!WIFEXITED(status)||WEXITSTATUS(status)!=17)failures++;printf("pewait-int32-wait-any pid=%ld status=%d\n",got,status);
 long probe=syscall(SYS_kill,0x1234567800000000UL|(unsigned)getpid(),0);if(probe)failures++;printf("pewait-int32-kill rc=%ld\n",probe);
 pid_t interrupted=fork();if(interrupted<0)return 2;if(!interrupted){setpgid(0,0);child(0,5000);}setpgid(interrupted,interrupted);usleep(150000);errno=0;int killed=kill(-interrupted,SIGTERM);printf("pewait-kill-group pid=%d rc=%d errno=%d\n",interrupted,killed,errno);if(killed)printf("pewait-kill-signextended rc=%ld\n",syscall(SYS_kill,-(long)interrupted,SIGTERM));status=0;pid_t joined=waitpid(interrupted,&status,0);if(killed||joined!=interrupted||!WIFSIGNALED(status)||WTERMSIG(status)!=SIGTERM)failures++;
 printf("pewait-signal status=%d\n",status);printf("pewait-done failures=%d\n",failures);return failures?1:0;
}
