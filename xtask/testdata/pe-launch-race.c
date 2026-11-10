// SPDX-License-Identifier: GPL-2.0-or-later
// Independent inherited stdin tags detect cross-wired whole command lines.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
static int one(int worker,int round){
 int p[2];if(pipe(p))return 1;char tag[96];snprintf(tag,sizeof tag,"worker %d round %d",worker,round);
 pid_t child=fork();if(child<0){close(p[0]);close(p[1]);return 1;}
 if(!child){close(p[1]);if(dup2(p[0],0)<0)_exit(20);close(p[0]);char *args[]={tag,tag,"C:\\winlaunch.exe",0};char *env[]={0};execve("/winlaunch.exe",args,env);_exit(21);}
 close(p[0]);ssize_t written=write(p[1],tag,strlen(tag)+1);close(p[1]);int status=0;
 if(waitpid(child,&status,0)!=child||written!=(ssize_t)strlen(tag)+1||!WIFEXITED(status)||WEXITSTATUS(status)){printf("pe-launch-race-FAIL worker=%d round=%d status=%d\n",worker,round,status);return 1;}return 0;
}
int main(void){
 pid_t workers[8];int failures=0;
 for(int i=0;i<8;i++){workers[i]=fork();if(workers[i]<0)return 2;if(!workers[i]){int failed=0;for(int j=0;j<8;j++)failed+=one(i,j);_exit(failed?1:0);}}
 for(int i=0;i<8;i++){int status;if(waitpid(workers[i],&status,0)!=workers[i]||!WIFEXITED(status)||WEXITSTATUS(status))failures++;}
 printf("pe-launch-race-done children=64 failures=%d\n",failures);return failures?1:0;
}
