// SPDX-License-Identifier: GPL-2.0-or-later
// THOS root-command frontends, using actual kernel password authentication.
// Supports -n, -u root/0, --, -s, and sudo -S; no credential cache or daemon.
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#define THOS_BASE 0x54480000L
static long thos3(long n,long a,long b,long c) {
    long r;__asm__ volatile("syscall":"=a"(r):"0"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");return r;
}
static const char *name;
static struct termios password_termios;
static volatile sig_atomic_t password_restore;
static void password_interrupt(int sig) {if(password_restore)tcsetattr(0,TCSANOW,&password_termios);_exit(128+sig);}
static void wipe_password(char *pw,size_t cap) {volatile char *p=pw;for(size_t k=0;k<cap;k++)p[k]=0;}
static int usage(void) {fprintf(stderr,"Usage: %s [-n] [-u root] [--] command [args...]\n       %s -s\n",name,name);return 2;}
static int resolve(const char *program,char *path,size_t cap) {
    if(strchr(program,'/')) {if(strlen(program)>=cap)return 0;strcpy(path,program);return access(path,X_OK)==0;}
    const char *dirs[]={"/usr/bin","/bin","/usr/sbin","/sbin","/",0};
    for(int i=0;dirs[i];i++){int n=snprintf(path,cap,"%s/%s",dirs[i],program);if(n>=0&&(size_t)n<cap&&access(path,X_OK)==0)return 1;}return 0;
}
static int password(char *pw,size_t cap,int from_stdin) {
    struct termios old,t;int tty=isatty(0),changed=0;
    int signals[]={SIGINT,SIGTERM,SIGHUP};struct sigaction previous[3],action={.sa_handler=password_interrupt};sigemptyset(&action.sa_mask);
    for(int k=0;k<3;k++)sigaction(signals[k],&action,&previous[k]);
    if(tty&&tcgetattr(0,&old)==0) {password_termios=old;password_restore=1;t=old;t.c_lflag&=~(tcflag_t)ECHO;if(tcsetattr(0,TCSANOW,&t)==0){struct termios check;if(tcgetattr(0,&check)==0&&!(check.c_lflag&ECHO))changed=1;else tcsetattr(0,TCSANOW,&old);}}
    fprintf(stderr,"%s: administrator password: ",name);fflush(stderr);
    long n;
    if(from_stdin||changed){
        n=0;int overflow=0;char c;
        for(;;){ssize_t got=read(0,&c,1);if(got<0){if(errno==EINTR)continue;n=-1;break;}if(got==0){if(n==0)n=-1;break;}if(c=='\n')break;if(c==0||n+1>=(long)cap){overflow=1;continue;}pw[n++]=c;}
        if(changed)tcsetattr(0,TCSANOW,&old);
        if(n>=0){if(n>0&&pw[n-1]=='\r')n--;pw[n]=0;}if(overflow)n=-1;
    }
    else n=thos3(THOS_BASE+1,(long)pw,cap,0);
    password_restore=0;for(int k=0;k<3;k++)sigaction(signals[k],&previous[k],0);
    fputc('\n',stderr);return n>=0;
}
int main(int argc,char **argv) {
    name=strrchr(argv[0],'/');name=name?name+1:argv[0];int is_sudo=!strcmp(name,"sudo"),noninteractive=0,stdin_pw=0,shell=0,i=1;
    for(;i<argc&&argv[i][0]=='-';i++) {
        if(!strcmp(argv[i],"--")){i++;break;}
        if(!strcmp(argv[i],"-n"))noninteractive=1;
        else if(!strcmp(argv[i],"-S")&&is_sudo)stdin_pw=1;
        else if(!strcmp(argv[i],"-s"))shell=1;
        else if(!strcmp(argv[i],"-u")&&i+1<argc) {i++;if(strcmp(argv[i],"root")&&strcmp(argv[i],"0")){fputs("THOS elevation currently supports the root account only.\n",stderr);return 2;}}
        else return usage();
    }
    char *shell_argv[]={"/busybox","sh","-c","PS1='\033[1;31mroot\033[0m:\\w # '; export PS1; exec /busybox sh -i",0};char **command;
    if(shell){if(i!=argc)return usage();command=shell_argv;}else{if(i>=argc)return usage();command=&argv[i];}
    char path[1024];if(!resolve(command[0],path,sizeof path)){fprintf(stderr,"%s: command not found: %s\n",name,command[0]);return 127;}
    if(geteuid()==0){execv(path,command);perror(name);return 126;}
    if(noninteractive){fprintf(stderr,"%s: a password is required\n",name);return 1;}
    char pw[128]={0};if(!password(pw,sizeof pw,stdin_pw)){wipe_password(pw,sizeof pw);fputs("Cannot read administrator password.\n",stderr);return 1;}
    long pid=thos3(THOS_BASE,(long)path,(long)command,(long)pw);
    wipe_password(pw,sizeof pw);
    if(pid<0){fprintf(stderr,"%s: %s\n",name,pid==-13?"authentication failed":pid==-1?"administrator account required":"command could not be started");return 1;}
    int status;while(waitpid((pid_t)pid,&status,0)<0){if(errno!=EINTR){perror(name);return 1;}}
    return WIFEXITED(status)?WEXITSTATUS(status):WIFSIGNALED(status)?128+WTERMSIG(status):1;
}
