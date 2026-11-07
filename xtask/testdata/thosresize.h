// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include "thoswl.h"
/* At most one pending allocation. New configure events coalesce while the
   compositor acknowledges the old one; no input events are consumed here. */
struct thos_resize { int id; uint32_t activate; uint32_t *pending; struct wl_msg desired, sent; };
static void thos_resize_limits(int sock,int w,int h) {
    struct wl_msg m={WL_LIMITS,w,h,0,0};(void)!write(sock,&m,sizeof m);
}
static void thos_resize_request(int sock,struct thos_resize *r) {
    if(r->pending || !r->desired.op) return;
    struct wl_msg c=r->desired;r->desired.op=0;
    if(!c.a||!c.b||c.a>8192||c.b>8192||(uint64_t)c.a*c.b*4>64u*1024*1024) return;
    int id=shmget(IPC_PRIVATE,(size_t)c.a*c.b*4,IPC_CREAT|0600);
    uint32_t *p=id>=0?shmat(id,0,0):(void*)-1;
    if(p==(void*)-1) {if(id>=0) shmctl(id,IPC_RMID,0);return;}
    r->id=id;r->pending=p;r->sent=c;
    struct wl_msg m={WL_BUFFER,c.a,c.b,(uint32_t)id,c.c};
    if(write(sock,&m,sizeof m)!=sizeof m) {shmdt(p);shmctl(id,IPC_RMID,0);r->pending=0;}
}
/* 0 unrelated, 1 handled, 2 new pixels/dimensions: caller renders + COMMITs. */
static int thos_resize_event(int sock,const struct wl_msg *m,struct thos_resize *r,uint32_t **pixels,int *w,int *h) {
    if(m->op==WL_CONFIGURE) {r->desired=*m;thos_resize_request(sock,r);return 1;}
    if(m->op!=WL_BUFFERED) return 0;
    if(!r->pending||m->a!=(uint32_t)r->id||m->b!=r->sent.c) return 1;
    int changed=0;
    if(m->c) {
        uint32_t *old=*pixels;*pixels=r->pending;*w=r->sent.a;*h=r->sent.b;
        if(old) shmdt(old);
        r->activate=r->sent.c;
        changed=1;
    } else shmdt(r->pending);
    shmctl(r->id,IPC_RMID,0);r->pending=0;
    /* Caller sends its COMMIT before servicing the next queued configure. */
    if(!changed) thos_resize_request(sock,r);
    return changed?2:1;
}
static void thos_resize_continue(int sock,struct thos_resize *r) {
    if(r->activate) {
        struct wl_msg m={WL_BUFFER_COMMIT,r->activate,0,0,0};(void)!write(sock,&m,sizeof m);r->activate=0;
    }
    thos_resize_request(sock,r);
}
static void thos_resize_dispose(struct thos_resize *r) {
    if(r->pending) {shmdt(r->pending);shmctl(r->id,IPC_RMID,0);r->pending=0;}
}
