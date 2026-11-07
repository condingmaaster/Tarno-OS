// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
static void *worker(void *p) {
    struct sched_param v={.sched_priority=99};
    assert(sched_getscheduler(0)==SCHED_OTHER);
    assert(sched_getparam(0,&v)==0&&v.sched_priority==0);
    assert(sched_setscheduler(syscall(SYS_gettid),SCHED_OTHER,&v)==0);
    *(int*)p=73;return p;
}
int main(void) {
    assert(sched_get_priority_min(SCHED_OTHER)==0&&sched_get_priority_max(SCHED_OTHER)==0);
    assert(sched_get_priority_min(SCHED_FIFO)==1&&sched_get_priority_max(SCHED_FIFO)==99);
    errno=0;assert(sched_get_priority_max(123456)==-1&&errno==EINVAL);
    struct sched_param p={.sched_priority=0};
    assert(sched_setparam(0,&p)==0&&sched_setscheduler(0,SCHED_OTHER,&p)==0);
    p.sched_priority=1;errno=0;assert(sched_setparam(0,&p)==-1&&errno==EINVAL);
    errno=0;assert(sched_setscheduler(0,SCHED_FIFO,&p)==-1&&errno==EOPNOTSUPP);
    errno=0;assert(sched_getscheduler(2147483647)==-1&&errno==ESRCH);
    pthread_attr_t a;assert(pthread_attr_init(&a)==0);
    assert(pthread_attr_setinheritsched(&a,PTHREAD_EXPLICIT_SCHED)==0);
    assert(pthread_attr_setschedpolicy(&a,SCHED_OTHER)==0);p.sched_priority=0;
    assert(pthread_attr_setschedparam(&a,&p)==0);
    for(int i=0;i<16;i++){int value=0;pthread_t t;void *ret;assert(pthread_create(&t,&a,worker,&value)==0);assert(pthread_join(t,&ret)==0&&ret==&value&&value==73);}
    pthread_attr_destroy(&a);puts("thread-sched-test-done: explicit normal-policy pthread creation/join, priority ranges and error semantics");return 0;
}
