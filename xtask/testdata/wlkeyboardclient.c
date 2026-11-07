// SPDX-License-Identifier: GPL-2.0-or-later
// Real independent v5/v3 keyboard resources; uses only the guest's virtual input.
#define _GNU_SOURCE
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/mman.h>
struct state {int maps,enters,leaves,presses,releases,repeats,saw_shift,saw_ctrl,saw_altgr,held_count,held_a,held_shift,enter_mods;uint32_t last_code,last_state;uint32_t mods;struct wl_surface *focus;};
static struct state states[2];static struct wl_keyboard *keys[2];static struct wl_seat *seats[2];
static struct wl_compositor *comp;static struct wl_shm *shm;static struct xdg_wm_base *wm;static struct wl_surface *root;static int configured;
static void keymap(void*d,struct wl_keyboard*k,uint32_t format,int fd,uint32_t size){(void)k;struct state*q=d;assert(format==1&&size>1);char*p=mmap(0,size,PROT_READ,MAP_PRIVATE,fd,0);assert(p!=MAP_FAILED&&p[size-1]==0&&strstr(p,"xkb_keymap"));munmap(p,size);close(fd);q->maps++;}
static void enter(void*d,struct wl_keyboard*k,uint32_t serial,struct wl_surface*s,struct wl_array*a){(void)k;(void)serial;struct state*q=d;q->focus=s;q->enters++;q->enter_mods=1;q->held_count=q->held_a=q->held_shift=0;assert(a->size%4==0);uint32_t *code;wl_array_for_each(code,a){q->held_count++;if(*code==30)q->held_a=1;if(*code==42)q->held_shift=1;}}
static void leave(void*d,struct wl_keyboard*k,uint32_t serial,struct wl_surface*s){(void)k;(void)serial;struct state*q=d;assert(q->focus==s);q->focus=0;q->leaves++;}
static void key(void*d,struct wl_keyboard*k,uint32_t serial,uint32_t time,uint32_t code,uint32_t state){(void)k;(void)serial;(void)time;struct state*q=d;assert(q->focus==root);assert(code==30||code==48||code==46||code==42||code==29||code==100);printf("wlkbd-key client=%ld code=%u state=%u\n",(long)(q-states),code,state);q->last_code=code;q->last_state=state;if(state)q->presses++;else q->releases++;}
static void modifiers(void*d,struct wl_keyboard*k,uint32_t serial,uint32_t depressed,uint32_t latched,uint32_t locked,uint32_t group){(void)k;(void)serial;(void)latched;(void)locked;(void)group;struct state*q=d;if(!q->enter_mods && q->mods!=depressed){uint32_t mask=q->last_code==42?1:q->last_code==29?4:q->last_code==100?0x80:0;if(!mask||((depressed&mask)!=0)!=!!q->last_state){puts("wlkbd-FAIL modifiers-before-key");exit(2);}}q->enter_mods=0;q->mods=depressed;if(depressed&1)q->saw_shift=1;if(depressed&4)q->saw_ctrl=1;if(depressed&0x80)q->saw_altgr=1;}
static void repeat(void*d,struct wl_keyboard*k,int32_t rate,int32_t delay){(void)k;struct state*q=d;assert(rate==25&&delay==600);q->repeats++;}
static const struct wl_keyboard_listener keyboard_l={keymap,enter,leave,key,modifiers,repeat};
static void bind_keyboard(int i){keys[i]=wl_seat_get_keyboard(seats[i]);wl_keyboard_add_listener(keys[i],&keyboard_l,&states[i]);}
static void caps(void*d,struct wl_seat*s,uint32_t c){(void)s;int i=(int)(uintptr_t)d;if((c&WL_SEAT_CAPABILITY_KEYBOARD)&&!keys[i])bind_keyboard(i);}
static void seatname(void*d,struct wl_seat*s,const char*n){(void)d;(void)s;(void)n;}
static const struct wl_seat_listener seat_l={caps,seatname};
static void ping(void*d,struct xdg_wm_base*w,uint32_t s){(void)d;xdg_wm_base_pong(w,s);}
static const struct xdg_wm_base_listener wm_l={ping};
static void global(void*d,struct wl_registry*r,uint32_t name,const char*iface,uint32_t version){(void)d;
 if(!strcmp(iface,"wl_compositor"))comp=wl_registry_bind(r,name,&wl_compositor_interface,4);
 else if(!strcmp(iface,"wl_shm"))shm=wl_registry_bind(r,name,&wl_shm_interface,1);
 else if(!strcmp(iface,"xdg_wm_base")){wm=wl_registry_bind(r,name,&xdg_wm_base_interface,1);xdg_wm_base_add_listener(wm,&wm_l,0);}
 else if(!strcmp(iface,"wl_seat"))for(int i=0;i<2;i++){uint32_t v=i?3:5;assert(version>=v);seats[i]=wl_registry_bind(r,name,&wl_seat_interface,v);wl_seat_add_listener(seats[i],&seat_l,(void*)(uintptr_t)i);}}
static void gone(void*d,struct wl_registry*r,uint32_t n){(void)d;(void)r;(void)n;}
static const struct wl_registry_listener registry_l={global,gone};
static void configured_cb(void*d,struct xdg_surface*s,uint32_t serial){(void)d;xdg_surface_ack_configure(s,serial);configured=1;}
static const struct xdg_surface_listener xs_l={configured_cb};
static void topconf(void*d,struct xdg_toplevel*t,int32_t w,int32_t h,struct wl_array*a){(void)d;(void)t;(void)w;(void)h;(void)a;}
static void topclose(void*d,struct xdg_toplevel*t){(void)d;(void)t;exit(4);}
static const struct xdg_toplevel_listener top_l={.configure=topconf,.close=topclose};
static long long now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static int receive_timeout(struct wl_display*d,int a,int b,int millis){long long deadline=now()+millis;while(states[0].releases<a||states[1].releases<b){assert(wl_display_dispatch_pending(d)>=0);wl_display_flush(d);if(now()>deadline){printf("wlkbd-FAIL releases=%d,%d expected=%d,%d enters=%d,%d\n",states[0].releases,states[1].releases,a,b,states[0].enters,states[1].enters);printf("wlkbd-timeout-last code=%u,%u mods=%u,%u\n",states[0].last_code,states[1].last_code,states[0].mods,states[1].mods);return 0;}struct pollfd p={.fd=wl_display_get_fd(d),.events=POLLIN};if(poll(&p,1,100)>0&&wl_display_dispatch(d)<0)return 0;}return 1;}
static int receive(struct wl_display*d,int a,int b){return receive_timeout(d,a,b,10000);}
int main(void){setbuf(stdout,0);struct wl_display*d=wl_display_connect(0);assert(d);struct wl_registry*r=wl_display_get_registry(d);wl_registry_add_listener(r,&registry_l,0);assert(wl_display_roundtrip(d)>=0&&wl_display_roundtrip(d)>=0);assert(comp&&shm&&wm&&keys[0]&&keys[1]);
 root=wl_compositor_create_surface(comp);struct xdg_surface*xs=xdg_wm_base_get_xdg_surface(wm,root);xdg_surface_add_listener(xs,&xs_l,0);struct xdg_toplevel*top=xdg_surface_get_toplevel(xs);xdg_toplevel_add_listener(top,&top_l,0);xdg_toplevel_set_title(top,"Wayland keyboard");wl_surface_commit(root);while(!configured)assert(wl_display_dispatch(d)>=0);
 int fd=memfd_create("wlkbd",0);assert(fd>=0&&ftruncate(fd,320*200*4)==0);uint32_t*p=mmap(0,320*200*4,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);assert(p!=MAP_FAILED);for(int i=0;i<320*200;i++)p[i]=0xff1db553;munmap(p,320*200*4);struct wl_shm_pool*pool=wl_shm_create_pool(shm,fd,320*200*4);struct wl_buffer*buf=wl_shm_pool_create_buffer(pool,0,320,200,320*4,WL_SHM_FORMAT_XRGB8888);wl_shm_pool_destroy(pool);close(fd);wl_surface_attach(root,buf,0,0);wl_surface_damage_buffer(root,0,0,320,200);wl_surface_commit(root);assert(wl_display_roundtrip(d)>=0);
 assert(states[0].maps==1&&states[1].maps==1&&states[0].repeats==1&&states[1].repeats==0);
 puts("wlkbd-ready:0");if(!receive(d,1,1))return 2;assert(states[0].presses==1&&states[1].presses==1);puts("wlkbd-ok:0");
 wl_keyboard_release(keys[0]);keys[0]=0;assert(wl_display_roundtrip(d)>=0);puts("wlkbd-ready:1");if(!receive(d,1,2))return 2;assert(states[0].presses==1&&states[1].presses==2);puts("wlkbd-ok:1");
 int enters=states[0].enters;bind_keyboard(0);assert(wl_display_roundtrip(d)>=0);if(states[0].enters!=enters+1||states[0].focus!=root){puts("wlkbd-FAIL rebind-current-focus");return 2;}assert(states[0].maps==2&&states[0].repeats==2);puts("wlkbd-ready:2");if(!receive(d,3,4))return 2;
 assert(states[0].presses==3&&states[1].presses==4&&states[0].saw_shift&&states[1].saw_shift&&!states[0].mods&&!states[1].mods);puts("wlkbd-ok:2");
 puts("wlkbd-ready:3");if(!receive(d,5,6))return 2;assert(states[0].presses==5&&states[1].presses==6&&states[0].saw_ctrl&&states[1].saw_ctrl&&!states[0].mods&&!states[1].mods);puts("wlkbd-ok:3");
 puts("wlkbd-ready:4");if(!receive(d,7,8))return 2;assert(states[0].presses==7&&states[1].presses==8&&states[0].saw_altgr&&states[1].saw_altgr&&!states[0].mods&&!states[1].mods);puts("wlkbd-ok:4");
 puts("wlkbd-ready:5");long long deadline=now()+10000;
 while(states[0].presses<9||states[1].presses<10){assert(wl_display_dispatch_pending(d)>=0);wl_display_flush(d);if(now()>deadline){puts("wlkbd-FAIL held-press-timeout");return 2;}struct pollfd f={.fd=wl_display_get_fd(d),.events=POLLIN};if(poll(&f,1,100)>0)assert(wl_display_dispatch(d)>=0);}
 enters=states[0].enters;wl_keyboard_release(keys[0]);keys[0]=0;assert(wl_display_roundtrip(d)>=0);bind_keyboard(0);assert(wl_display_roundtrip(d)>=0);
 printf("wlkbd-held-enter count=%d a=%d shift=%d mods=%u\n",states[0].held_count,states[0].held_a,states[0].held_shift,states[0].mods);
 if(states[0].enters!=enters+1||states[0].held_count!=2||!states[0].held_a||!states[0].held_shift||states[0].mods!=1){puts("wlkbd-FAIL held-enter-snapshot");return 2;}
 /* HMP hold time is host/QEMU time; the slow TCG guest deadline is a
    separate clock. Keep all state assertions, but allow its queued releases. */
 if(!receive_timeout(d,9,10,30000))return 2;
 if(states[0].presses!=9||states[1].presses!=10||states[0].mods||states[1].mods){puts("wlkbd-FAIL held-release-state");return 2;}
 wl_keyboard_release(keys[1]);keys[1]=0;assert(wl_display_roundtrip(d)>=0);bind_keyboard(1);assert(wl_display_roundtrip(d)>=0);
 if(states[1].held_count||states[1].mods){puts("wlkbd-FAIL released-enter-snapshot");return 2;}
 puts("wlkbd-ok:5");
 for(int stage=6;stage<8;stage++){
  printf("wlkbd-ready:%d\n",stage);deadline=now()+1500;
  while(now()<deadline){assert(wl_display_dispatch_pending(d)>=0);wl_display_flush(d);struct pollfd f={.fd=wl_display_get_fd(d),.events=POLLIN};if(poll(&f,1,100)>0)assert(wl_display_dispatch(d)>=0);}
  if(states[0].presses!=9||states[1].presses!=10||states[0].releases!=9||states[1].releases!=10){puts("wlkbd-FAIL consumed-shortcut-key");return 2;}
  printf("wlkbd-ok:%d\n",stage);
 }
 puts("wlkbd-done");wl_display_disconnect(d);return 0;}
