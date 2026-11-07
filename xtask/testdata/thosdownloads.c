// SPDX-License-Identifier: GPL-2.0-or-later
// THOS downloads: a user-started transfer, with progress, cancel and Files integration.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "thoswl.h"
#include "thosresize.h"
#include "thosgfx.h"
#include "thosui.h"
static int DW=760;
static int DH=420;
static uint32_t *px;
static struct thos_resize resize;
static int sock, field, fd=-1, cancelling;
static pid_t job;
static char url[1024], name[181], status[200]="Enter a URL and choose a file name.", line[200];
static size_t used;
static void button(int x,int y,int w,const char *text,int active) {
    ui_rrect(px,DW,DH,x,y,w,36,10,active?UI_ACCENT:0x232b40);
    ui_text(px,DW,DH,x+14,y+24,text,UI_REG,13,UI_TEXT);
}
static void render(void) {
    for(int i=0;i<DW*DH;i++) px[i]=UI_SURFACE;
    ui_text(px,DW,DH,24,38,"Downloads",UI_BOLD,22,UI_TEXT);
    ui_text(px,DW,DH,24,67,"Save Linux packages, Windows programs and other files.",UI_REG,13,UI_MUTED);
    const char *labels[]={"HTTP / HTTPS URL","File name (optional; taken from URL when empty)"};
    for(int i=0;i<2;i++) {
        int y=100+i*83;
        ui_text(px,DW,DH,24,y,labels[i],UI_REG,12,UI_MUTED);
        ui_rrect(px,DW,DH,24,y+10,DW-48,38,9,field==i&&!job?0x283b63:0x1b2030);
        const char *value=i?name:url;
        size_t start=strlen(value); while(start && ui_text_width(UI_REG,13,value+start-1)<DW-78) start--;
        ui_text(px,DW,DH,36,y+35,value+start,UI_REG,13,UI_TEXT);
    }
    button(24,250,150,job?"Cancel download":"Download",!job);
    button(190,250,170,"Open Downloads",0);
    ui_set_clip(24,0,DW-24,DH);
    ui_text(px,DW,DH,24,317,status,UI_REG,13,UI_TEXT);
    ui_set_clip(0,0,0x7fffffff,0x7fffffff);
    ui_text(px,DW,DH,24,358,"Open Files to run an EXE or install a package. Nothing runs automatically.",UI_REG,12,UI_MUTED);
    FILE *b=fopen("/proc/bootdisk","r"); char disk[24]="";
    if(b) {fgets(disk,sizeof disk,b);fclose(b);}
    if(!strncmp(disk,"ram",3)) ui_text(px,DW,DH,24,386,"Live session: these files are lost on reboot until persistent storage is set up.",UI_REG,12,UI_MUTED);
    struct wl_msg m={WL_COMMIT,0,0,DW,DH}; (void)!write(sock,&m,sizeof m);
}
static void files(void) {
    if(fork()!=0) return;
    char path[512]; const char *h=getenv("HOME"); if(!h||!strcmp(h,"/")) h="/tmp";
    snprintf(path,sizeof path,"%s/Downloads",h);
    setenv("FILES_START",path,1); execl("/thosfiles","thosfiles",(char*)0); _exit(127);
}
static void start(void) {
    if(job) { cancelling=1; kill(job,SIGTERM); snprintf(status,sizeof status,"Cancelling download..."); return; }
    if(!url[0]) {snprintf(status,sizeof status,"Enter a URL first."); return;}
    int p[2]; if(pipe(p)) {snprintf(status,sizeof status,"Cannot start download.");return;}
    pid_t child=fork();
    if(child<0) {close(p[0]);close(p[1]);snprintf(status,sizeof status,"Cannot start download.");return;}
    if(child==0) {
        close(p[0]);dup2(p[1],1);dup2(p[1],2);close(p[1]);close(sock);
        execl("/busybox","sh","/bin/thos-fetch",url,name,(char*)0);_exit(127);
    }
    close(p[1]);fd=p[0];job=child;used=0;cancelling=0;
    printf("downloads started: %d\n",(int)job);fflush(stdout);
    snprintf(status,sizeof status,"Connecting...");
}
static int output(void) {
    char previous[sizeof status];memcpy(previous,status,sizeof status);
    char buf[256]; ssize_t n=read(fd,buf,sizeof buf);
    if(n>0) for(ssize_t i=0;i<n;i++) {
        unsigned char c=buf[i];
        if(c=='\r'||c=='\n') {
            line[used]=0;
            if(used) {
                char *percent=strrchr(line,'%');
                if(percent&&strchr(line,'#')) {
                    char *number=percent;
                    while(number>line&&((number[-1]>='0'&&number[-1]<='9')||number[-1]=='.')) number--;
                    char *end;float value=strtof(number,&end);
                    if(end==percent&&value>=0&&value<=100) snprintf(status,sizeof status,"Downloading: %.1f%%",(double)value);
                    else snprintf(status,sizeof status,"%s",line);
                } else snprintf(status,sizeof status,"%s",line);
            }
            used=0;
        } else if(c>=32 && c<127 && used+1<sizeof line) line[used++]=c;
    }
    if(n<=0) {
        int st=0;waitpid(job,&st,0);close(fd);fd=-1;job=0;
        if(WIFEXITED(st)&&WEXITSTATUS(st)==0) snprintf(status,sizeof status,"Download complete. Your file is in Downloads.");
        else if(cancelling) snprintf(status,sizeof status,"Download cancelled; temporary file removed.");
        else if(!strncmp(status,"Connecting",10)) snprintf(status,sizeof status,"Download failed. Check the network and URL.");
        printf("downloads result: %d\n",WIFEXITED(st)?WEXITSTATUS(st):-1);fflush(stdout);
        return 1;
    }
    return strcmp(previous,status)!=0;
}
int main(int argc,char **argv) {
    if(argc>1) snprintf(url,sizeof url,"%s",argv[1]);
    if(argc>2) snprintf(name,sizeof name,"%s",argv[2]);
    int key=0x9000+getpid(), id=shmget(key,DW*DH*4,IPC_CREAT|0600);
    px=id>=0?shmat(id,0,0):(void*)-1;
    if(px==(void*)-1||gfx_load_font()!=0||ui_init()!=0) return 1;
    sock=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path,WL_SOCK_NAME,WL_SOCK_LEN);
    if(connect(sock,(void*)&a,2+WL_SOCK_LEN)) return 1;
    struct wl_msg m={WL_CREATE,DW,DH,(uint32_t)key,WL_RESIZABLE};(void)!write(sock,&m,sizeof m);
    if(ui_read(sock,&m)!=sizeof m||m.op!=WL_CREATED) return 1;
    thos_resize_limits(sock,640,420);
    shmctl(id,IPC_RMID,0);struct wl_msg t={WL_TITLE,0,0,0,0};memcpy(&t.a,"Downloads",9);(void)!write(sock,&t,sizeof t);
    render();puts("downloads ready");fflush(stdout);
    for(;;) {
        struct pollfd p[2]={{sock,POLLIN,0},{fd,POLLIN,0}};
        poll(p,2,ui_have_held?0:-1);
        if(fd>=0&&(p[1].revents&(POLLIN|POLLHUP|POLLERR)) && output()) render();
        if(!(p[0].revents&(POLLIN|POLLHUP|POLLERR))&&!ui_have_held) continue;
        if(ui_read(sock,&m)!=sizeof m||m.op==WL_CLOSE) break;
        int resized=thos_resize_event(sock,&m,&resize,&px,&DW,&DH);
        if(resized) {if(resized==2) {render();thos_resize_continue(sock,&resize);}continue;}
        int redraw=0;
        if(m.op==WL_THEME) {ui_theme_load();redraw=1;}
        if(m.op==WL_POINTER && (m.c==0||m.c==2)) {
            int x=m.a,y=m.b;
            int overfield=x>=24&&x<DW-24&&((y>=110&&y<148)||(y>=193&&y<231));
            int overbutton=y>=250&&y<286&&((x>=24&&x<174)||(x>=190&&x<360));
            ui_cursor(sock,m.c==2?0:overbutton?2:overfield?1:0);
        }
        if(m.op==WL_POINTER&&m.c==1) {
            int x=m.a,y=m.b;
            if(x>=24&&x<DW-24&&y>=110&&y<148&&field!=0) {field=0;redraw=1;}
            if(x>=24&&x<DW-24&&y>=193&&y<231&&field!=1) {field=1;redraw=1;}
            if(y>=250&&y<286) {if(x>=24&&x<174) {start();redraw=1;}else if(x>=190&&x<360) files();}
        }
        if(m.op==WL_KEY&&m.b&&!job) {
            char *s=field?name:url;size_t cap=field?sizeof name:sizeof url,n=strlen(s);
            if(m.a==0x2b) {field^=1;redraw=1;}
            else if(m.a==0x2a&&n) {s[n-1]=0;redraw=1;}
            else if(m.a==0x28) {start();redraw=1;}
            else if(m.c>=32&&m.c<127&&n+1<cap) {s[n]=m.c;s[n+1]=0;redraw=1;}
        }
        if(redraw) render();
    }
    if(job) {kill(job,SIGTERM);waitpid(job,0,0);}
    if(fd>=0) close(fd);
    thos_resize_dispose(&resize);shmdt(px);close(sock);return 0;
}
