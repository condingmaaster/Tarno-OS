// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
static void fault_child(volatile unsigned char *p,int write_it) {
    pid_t child=fork();assert(child>=0);
    if(!child){if(write_it)*p=99;else{volatile unsigned char x=*p;(void)x;}_exit(1);}
    int st;assert(waitpid(child,&st,0)==child&&WIFSIGNALED(st)&&WTERMSIG(st)==SIGSEGV);
}
int main(void) {
    int raw=syscall(SYS_memfd_create,"upper-register-bits",0x100000003ULL);
    assert(raw>=0&&fcntl(raw,F_GET_SEALS)==0&&(fcntl(raw,F_GETFD)&FD_CLOEXEC));close(raw);
    errno=0;assert(syscall(SYS_memfd_create,"invalid-low-flags",0x100000008ULL)==-1&&errno==EINVAL);
    int f=memfd_create("freeze-test",MFD_CLOEXEC|MFD_ALLOW_SEALING);assert(f>=0&&ftruncate(f,12288)==0);
    unsigned char *rw=mmap(0,12288,PROT_READ|PROT_WRITE,MAP_SHARED,f,0);assert(rw!=MAP_FAILED);rw[0]='A';
    char path[80];snprintf(path,sizeof path,"/proc/self/fd/%d",f);int ro=open(path,O_RDONLY|O_CLOEXEC);assert(ro>=0);
    assert((fcntl(ro,F_GETFL)&O_ACCMODE)==O_RDONLY&&fcntl(ro,F_GETFD)&FD_CLOEXEC);
    char c;assert(lseek(f,4,SEEK_SET)==4&&read(ro,&c,1)==1&&c=='A'&&lseek(f,0,SEEK_CUR)==4);
    errno=0;assert(write(ro,"x",1)==-1&&errno==EBADF);errno=0;assert(ftruncate(ro,0)==-1&&errno==EBADF);
    unsigned char *r=mmap(0,12288,PROT_READ,MAP_SHARED,ro,0);assert(r!=MAP_FAILED&&r[0]=='A');
    errno=0;assert(mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,ro,0)==MAP_FAILED&&errno==EACCES);
    pid_t child=fork();assert(child>=0);if(!child){rw[8192]=0x55;_exit(0);}
    int st;assert(waitpid(child,&st,0)==child&&WIFEXITED(st)&&WEXITSTATUS(st)==0&&rw[8192]==0x55&&r[8192]==0x55);
    assert(fcntl(f,F_ADD_SEALS,F_SEAL_SHRINK|F_SEAL_GROW)==0);
    errno=0;assert(ftruncate(f,4096)==-1&&errno==EPERM);errno=0;assert(ftruncate(f,16384)==-1&&errno==EPERM);
    errno=0;assert(fcntl(f,F_ADD_SEALS,F_SEAL_WRITE)==-1&&errno==EBUSY);
    assert(fcntl(f,F_ADD_SEALS,F_SEAL_FUTURE_WRITE)==0);errno=0;assert(pwrite(f,"x",1,0)==-1&&errno==EPERM);
    rw[0]='B';assert(r[0]=='B');errno=0;assert(mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,f,0)==MAP_FAILED&&errno==EACCES);
    errno=0;assert(mprotect(r,4096,PROT_READ|PROT_WRITE)==-1&&errno==EACCES);fault_child(r,1);assert(r[0]=='B');
    assert(munmap(r+4096,4096)==0);fault_child(r+4096,0);assert(r[8192]==0x55);
    assert(munmap(r,4096)==0&&munmap(r+8192,4096)==0&&munmap(rw,12288)==0);
    assert(fcntl(f,F_ADD_SEALS,F_SEAL_WRITE)==0&&fcntl(f,F_GET_SEALS)&F_SEAL_WRITE);
    assert(fcntl(f,F_ADD_SEALS,F_SEAL_SEAL)==0);errno=0;assert(fcntl(f,F_ADD_SEALS,F_SEAL_GROW)==-1&&errno==EPERM);
    close(ro);close(f);puts("memfd-freeze-test-done: independent read-only reopen, real protections/seals, shared fork and partial unmap");return 0;
}
