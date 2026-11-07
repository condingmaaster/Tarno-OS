// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <asm/prctl.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
static uint64_t parent_value=0xabcdef0123456789ULL;
static uint64_t get_base(void) {uint64_t base=~0ULL;assert(syscall(SYS_arch_prctl,ARCH_GET_GS,&base)==0);return base;}
static uint64_t read_gs(void) {uint64_t value;__asm__ volatile("movq %%gs:0,%0":"=r"(value));return value;}
static void *worker(void *arg) {
    assert(get_base()==(uintptr_t)&parent_value&&read_gs()==parent_value);
    uint64_t value=0x12340000+(uintptr_t)arg;
    assert(syscall(SYS_arch_prctl,ARCH_SET_GS,&value)==0);
    for(int i=0;i<64;i++) {sched_yield();usleep(1000);assert(get_base()==(uintptr_t)&value&&read_gs()==value);}
    assert(syscall(SYS_arch_prctl,ARCH_SET_GS,0)==0&&get_base()==0);return arg;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"exec-check")) {assert(get_base()==0);return 0;}
    assert(get_base()==0);
    assert(syscall(SYS_arch_prctl,ARCH_SET_GS,&parent_value)==0&&read_gs()==parent_value);
    pthread_t threads[8];for(uintptr_t i=0;i<8;i++)assert(pthread_create(&threads[i],0,worker,(void*)i)==0);
    for(int i=0;i<64;i++){sched_yield();usleep(1000);assert(read_gs()==parent_value);}
    for(uintptr_t i=0;i<8;i++){void *v=0;assert(pthread_join(threads[i],&v)==0&&v==(void*)i);}
    pid_t child=fork();assert(child>=0);
    if(!child){assert(get_base()==(uintptr_t)&parent_value&&read_gs()==parent_value);execl("/gsbase-test","gsbase-test","exec-check",(char*)0);_exit(90);}
    int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    errno=0;assert(syscall(SYS_arch_prctl,ARCH_SET_GS,0xffff800000000000ULL)==-1&&errno==EPERM);
    errno=0;assert(syscall(SYS_arch_prctl,ARCH_SET_FS,0xffff800000000000ULL)==-1&&errno==EPERM);
    errno=0;assert(syscall(SYS_arch_prctl,ARCH_GET_GS,0)==-1&&errno==EFAULT);
    assert(read_gs()==parent_value);assert(syscall(SYS_arch_prctl,ARCH_SET_GS,0)==0&&get_base()==0);
    puts("gsbase-test-done: real GS reads, thread isolation, scheduling, fork inheritance, exec reset and invalid pointers");return 0;
}
