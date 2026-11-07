// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit Files/terminal launch of downloaded native programs. Never use a shell fallback.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned u16(const unsigned char *p){return p[0]|((unsigned)p[1]<<8);}
static uint32_t u32(const unsigned char *p){return u16(p)|((uint32_t)u16(p+2)<<16);}
static uint64_t u64(const unsigned char *p){return u32(p)|((uint64_t)u32(p+4)<<32);}
static int read_at(int fd,void *buf,size_t n,off_t off){
    size_t done=0;while(done<n){ssize_t got=pread(fd,(char*)buf+done,n-done,off+(off_t)done);if(got<0&&errno==EINTR)continue;if(got<=0)return 0;done+=(size_t)got;}return 1;
}
int main(int argc,char **argv){
    int index=1,windows=0;
    if(argc>1&&!strcmp(argv[1],"--windows")){windows=1;index++;}
    if(argc<=index){fprintf(stderr,"Usage: thosrun [--windows] program [arguments...]\n");return 2;}
    const char *path=argv[index];int fd=open(path,O_RDONLY|O_CLOEXEC|O_NONBLOCK);
    if(fd<0){fprintf(stderr,"Cannot open %s: %s\n",path,strerror(errno));return 1;}
    struct stat st;unsigned char header[64],pe[26];int format=0;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||!read_at(fd,header,sizeof header,0))goto unsupported;
    if(header[0]=='M'&&header[1]=='Z'){
        uint32_t pos=u32(header+60);
        if(pos<64||(uint64_t)pos+sizeof pe>(uint64_t)st.st_size||!read_at(fd,pe,sizeof pe,pos))goto unsupported;
        if(memcmp(pe,"PE\0\0",4)||u16(pe+4)!=0x8664||u16(pe+24)!=0x20b||u16(pe+20)<2||(u16(pe+22)&0x2000))goto unsupported;
        format=1;
    }else if(!windows&&!memcmp(header,"\177ELF",4)&&header[4]==2&&header[5]==1&&header[6]==1&&u16(header+18)==62&&(u16(header+16)==2||u16(header+16)==3)&&u64(header+24)!=0){format=2;}
    if(!format)goto unsupported;
    if(access(path,X_OK)&&fchmod(fd,(st.st_mode&07777)|S_IXUSR)){
        fprintf(stderr,"Cannot enable execution for %s: %s\n",path,strerror(errno));close(fd);return 1;
    }
    close(fd);
    // execv (not execvp or `sh -c`) returns on ENOEXEC; a broken EXE must never become a shell script.
    execv(path,&argv[index]);
    fprintf(stderr,"Cannot start %s: %s\n",path,strerror(errno));return 126;
unsupported:
    fprintf(stderr,"Cannot start %s: this is not a supported %sprogram.\n",path,windows?"64-bit Windows ":"64-bit Windows or Linux ");
    close(fd);return 1;
}
