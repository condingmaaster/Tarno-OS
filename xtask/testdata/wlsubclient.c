// SPDX-License-Identifier: GPL-2.0-or-later
// Unmodified libwayland protocol calls; screenshots verify applied state, not just acknowledgements.
#define _GNU_SOURCE
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
static struct wl_compositor *comp;
static struct wl_subcompositor *subcomp;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static int configured, advance;
static void keymap(void*d,struct wl_keyboard*k,uint32_t f,int fd,uint32_t n){close(fd);}
static void enter(void*d,struct wl_keyboard*k,uint32_t n,struct wl_surface*s,struct wl_array*a){}
static void leave(void*d,struct wl_keyboard*k,uint32_t n,struct wl_surface*s){}
static void key(void*d,struct wl_keyboard*k,uint32_t n,uint32_t t,uint32_t code,uint32_t state){if(code==28&&state)advance=1;}
static void mods(void*d,struct wl_keyboard*k,uint32_t n,uint32_t a,uint32_t b,uint32_t c,uint32_t g){}
static void repeat(void*d,struct wl_keyboard*k,int32_t r,int32_t t){}
static const struct wl_keyboard_listener kbd_l={keymap,enter,leave,key,mods,repeat};
static void caps(void*d,struct wl_seat*s,uint32_t c){if(c&WL_SEAT_CAPABILITY_KEYBOARD)wl_keyboard_add_listener(wl_seat_get_keyboard(s),&kbd_l,0);}
static void seat_name(void*d,struct wl_seat*s,const char*n){}
static const struct wl_seat_listener seat_l={caps,seat_name};
static void ping(void *d,struct xdg_wm_base *w,uint32_t n){xdg_wm_base_pong(w,n);}
static const struct xdg_wm_base_listener wm_l={ping};
static void global(void *d,struct wl_registry *r,uint32_t n,const char *i,uint32_t v){
 if(!strcmp(i,"wl_compositor"))comp=wl_registry_bind(r,n,&wl_compositor_interface,4);
 else if(!strcmp(i,"wl_subcompositor"))subcomp=wl_registry_bind(r,n,&wl_subcompositor_interface,1);
 else if(!strcmp(i,"wl_seat")){struct wl_seat*s=wl_registry_bind(r,n,&wl_seat_interface,5);wl_seat_add_listener(s,&seat_l,0);}
 else if(!strcmp(i,"wl_shm"))shm=wl_registry_bind(r,n,&wl_shm_interface,1);
 else if(!strcmp(i,"xdg_wm_base")){wm=wl_registry_bind(r,n,&xdg_wm_base_interface,1);xdg_wm_base_add_listener(wm,&wm_l,0);}
}
static void removed(void*d,struct wl_registry*r,uint32_t n){}
static const struct wl_registry_listener reg_l={global,removed};
static void configure(void*d,struct xdg_surface*s,uint32_t n){xdg_surface_ack_configure(s,n);configured=1;}
static const struct xdg_surface_listener xs_l={configure};
static void top_configure(void*d,struct xdg_toplevel*t,int32_t w,int32_t h,struct wl_array*s){}
static void top_close(void*d,struct xdg_toplevel*t){}
static const struct xdg_toplevel_listener top_l={top_configure,top_close};
static struct wl_buffer *buffer(int w,int h,uint32_t color){
 int fd=memfd_create("subsurface-test",MFD_CLOEXEC|MFD_ALLOW_SEALING);if(fd<0||ftruncate(fd,w*h*4))exit(2);
 uint32_t *p=mmap(0,w*h*4,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(p==MAP_FAILED)exit(2);
 for(int i=0;i<w*h;i++)p[i]=color;
 munmap(p,w*h*4);
 if(fcntl(fd,F_ADD_SEALS,F_SEAL_WRITE|F_SEAL_SHRINK|F_SEAL_GROW))exit(2);
 char name[80];snprintf(name,sizeof name,"/proc/self/fd/%d",fd);
 int readonly=open(name,O_RDONLY|O_CLOEXEC);if(readonly<0)exit(2);close(fd);fd=readonly;
 struct wl_shm_pool *pool=wl_shm_create_pool(shm,fd,w*h*4);
 struct wl_buffer*b=wl_shm_pool_create_buffer(pool,0,w,h,w*4,WL_SHM_FORMAT_XRGB8888);
 wl_shm_pool_destroy(pool);close(fd);return b;
}
static void attach(struct wl_surface*s,struct wl_buffer*b,int w,int h){wl_surface_attach(s,b,0,0);wl_surface_damage_buffer(s,0,0,w,h);wl_surface_commit(s);}
static void phase(struct wl_display*d,int n){advance=0;if(wl_display_roundtrip(d)<0)exit(3);printf("wlsubclient phase:%d\n",n);fflush(stdout);while(!advance)if(wl_display_dispatch(d)<0)exit(3);}
int main(void){
 struct wl_display*d=wl_display_connect(NULL);if(!d)return 1;
 struct wl_registry*r=wl_display_get_registry(d);wl_registry_add_listener(r,&reg_l,0);wl_display_roundtrip(d);
 if(!comp||!subcomp||!shm||!wm)return 1;
 struct wl_surface*root=wl_compositor_create_surface(comp);
 struct xdg_surface*xs=xdg_wm_base_get_xdg_surface(wm,root);xdg_surface_add_listener(xs,&xs_l,0);
 struct xdg_toplevel*top=xdg_surface_get_toplevel(xs);xdg_toplevel_add_listener(top,&top_l,0);xdg_toplevel_set_title(top,"Subsurface test");
 wl_surface_commit(root);while(!configured)if(wl_display_dispatch(d)<0)return 1;
 attach(root,buffer(320,200,0x00010203),320,200);
 struct wl_surface*child=wl_compositor_create_surface(comp);
 struct wl_subsurface*sub=wl_subcompositor_get_subsurface(subcomp,child,root);
 wl_subsurface_set_position(sub,40,30);attach(child,buffer(100,80,0x00aa0011),100,80);phase(d,1);
 wl_surface_commit(root);phase(d,2);
 wl_subsurface_set_desync(sub);attach(child,buffer(100,80,0x0000cc44),100,80);phase(d,3);
 wl_subsurface_set_position(sub,140,50);wl_surface_commit(child);phase(d,4);
 wl_surface_commit(root);phase(d,5);
 struct wl_surface*grand=wl_compositor_create_surface(comp);
 struct wl_subsurface*gsub=wl_subcompositor_get_subsurface(subcomp,grand,child);
 wl_subsurface_set_position(gsub,10,10);attach(grand,buffer(40,30,0x00ffaa00),40,30);
 wl_surface_commit(child);phase(d,6);
 wl_subsurface_place_below(sub,root);wl_surface_commit(root);phase(d,7);
 wl_subsurface_place_above(sub,root);wl_surface_commit(root);phase(d,8);
 wl_subsurface_destroy(gsub);phase(d,9);
 wl_subsurface_destroy(sub);phase(d,10);
 wl_surface_destroy(grand);wl_surface_destroy(child);xdg_toplevel_destroy(top);xdg_surface_destroy(xs);wl_surface_destroy(root);
 wl_display_roundtrip(d);wl_display_disconnect(d);puts("wlsubclient-done");return 0;
}
