// SPDX-License-Identifier: GPL-2.0-or-later
// Real libwayland input events: subsurface targeting, region snapshots and grabs.
#define _GNU_SOURCE
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

static struct wl_compositor *comp;
static struct wl_subcompositor *subcomp;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_surface *root,*child,*grand;
struct pointer_state {struct wl_surface *focus;int px,py,pressed,clicks,axes,axis_value,discrete,frames,sources;};
static struct pointer_state states[2];
static struct wl_pointer *pointers[2];
static struct wl_seat *seats[2];
static int configured,stage,advance;
static const int target[]={1,2,3,2,1,2,2,1,3,1,1,2,1};
static const int expected_x[]={20,20,20,30,70,30,30,70,200,250,45,5,45};
static const int expected_y[]={20,20,20,30,60,30,30,60,110,150,35,5,35};
static int which(struct wl_surface *s){return s==root?1:s==child?2:s==grand?3:0;}
static void enter(void*d,struct wl_pointer*p,uint32_t serial,struct wl_surface*s,wl_fixed_t x,wl_fixed_t y){(void)p;(void)serial;struct pointer_state*q=d;q->focus=s;q->px=wl_fixed_to_int(x);q->py=wl_fixed_to_int(y);}
static void leave(void*d,struct wl_pointer*p,uint32_t serial,struct wl_surface*s){(void)p;(void)serial;struct pointer_state*q=d;if(q->focus==s)q->focus=0;}
static void motion(void*d,struct wl_pointer*p,uint32_t t,wl_fixed_t x,wl_fixed_t y){(void)p;(void)t;struct pointer_state*q=d;q->px=wl_fixed_to_int(x);q->py=wl_fixed_to_int(y);}
static void button(void*d,struct wl_pointer*p,uint32_t serial,uint32_t t,uint32_t code,uint32_t state){
    (void)p;(void)serial;(void)t;struct pointer_state*q=d;
    if(code!=0x110)return;
    int tx=expected_x[stage],ty=expected_y[stage];
    if(stage==8&&state){tx=20;ty=20;}
    if(which(q->focus)!=target[stage]||q->px!=tx||q->py!=ty){
        printf("wlinput-FAIL pointer=%ld stage=%d target=%d expected=%d xy=%d,%d expectedxy=%d,%d state=%u\n",(long)(q-states),stage,which(q->focus),target[stage],q->px,q->py,tx,ty,state);fflush(stdout);exit(2);
    }
    if(state)q->pressed=1;
    else {if(!q->pressed)exit(3);q->pressed=0;q->clicks++;advance=1;}
}
static void axis(void*d,struct wl_pointer*p,uint32_t t,uint32_t a,wl_fixed_t v){(void)p;(void)t;struct pointer_state*q=d;if(a==0){q->axes++;q->axis_value+=v;}}
static void frame(void*d,struct wl_pointer*p){(void)p;((struct pointer_state*)d)->frames++;}
static void axis_source(void*d,struct wl_pointer*p,uint32_t a){(void)p;if(a==0)((struct pointer_state*)d)->sources++;}
static void axis_stop(void*d,struct wl_pointer*p,uint32_t t,uint32_t a){(void)d;(void)p;(void)t;(void)a;}
static void axis_discrete(void*d,struct wl_pointer*p,uint32_t a,int32_t v){(void)p;if(a==0)((struct pointer_state*)d)->discrete+=v;}
static const struct wl_pointer_listener pointer_l={.enter=enter,.leave=leave,.motion=motion,.button=button,.axis=axis,.frame=frame,.axis_source=axis_source,.axis_stop=axis_stop,.axis_discrete=axis_discrete};
static void ping(void*d,struct xdg_wm_base*w,uint32_t serial){(void)d;xdg_wm_base_pong(w,serial);}
static const struct xdg_wm_base_listener wm_l={ping};
static void xs_configure(void*d,struct xdg_surface*s,uint32_t serial){(void)d;xdg_surface_ack_configure(s,serial);configured=1;}
static const struct xdg_surface_listener xs_l={xs_configure};
static void top_configure(void*d,struct xdg_toplevel*t,int32_t w,int32_t h,struct wl_array*a){(void)d;(void)t;(void)w;(void)h;(void)a;}
static void top_close(void*d,struct xdg_toplevel*t){(void)d;(void)t;exit(4);}
static const struct xdg_toplevel_listener top_l={.configure=top_configure,.close=top_close};
static void caps(void*d,struct wl_seat*s,uint32_t capabilities){int i=(int)(uintptr_t)d;if((capabilities&WL_SEAT_CAPABILITY_POINTER)&&!pointers[i]){pointers[i]=wl_seat_get_pointer(s);wl_pointer_add_listener(pointers[i],&pointer_l,&states[i]);}}
static void seat_name(void*d,struct wl_seat*s,const char*n){(void)d;(void)s;(void)n;}
static const struct wl_seat_listener seat_l={caps,seat_name};
static void global(void*d,struct wl_registry*r,uint32_t name,const char*iface,uint32_t version){
    (void)d;
    if(!strcmp(iface,"wl_compositor"))comp=wl_registry_bind(r,name,&wl_compositor_interface,4);
    else if(!strcmp(iface,"wl_subcompositor"))subcomp=wl_registry_bind(r,name,&wl_subcompositor_interface,1);
    else if(!strcmp(iface,"wl_shm"))shm=wl_registry_bind(r,name,&wl_shm_interface,1);
    else if(!strcmp(iface,"xdg_wm_base")){wm=wl_registry_bind(r,name,&xdg_wm_base_interface,1);xdg_wm_base_add_listener(wm,&wm_l,0);}
    else if(!strcmp(iface,"wl_seat")){for(int i=0;i<2;i++){seats[i]=wl_registry_bind(r,name,&wl_seat_interface,version<(i?3:5)?version:(i?3:5));wl_seat_add_listener(seats[i],&seat_l,(void*)(uintptr_t)i);}}
}
static void gone(void*d,struct wl_registry*r,uint32_t name){(void)d;(void)r;(void)name;}
static const struct wl_registry_listener registry_l={global,gone};
static struct wl_buffer *buffer(int w,int h,uint32_t color){
    int fd=memfd_create("wlinput",0);if(fd<0||ftruncate(fd,w*h*4))exit(5);
    uint32_t *p=mmap(0,w*h*4,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(p==MAP_FAILED)exit(5);
    for(int i=0;i<w*h;i++)p[i]=color;
    munmap(p,w*h*4);struct wl_shm_pool*pool=wl_shm_create_pool(shm,fd,w*h*4);
    struct wl_buffer*b=wl_shm_pool_create_buffer(pool,0,w,h,w*4,WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);close(fd);return b;
}
static void attach(struct wl_surface*s,int w,int h,uint32_t color){wl_surface_attach(s,buffer(w,h,color),0,0);wl_surface_damage_buffer(s,0,0,w,h);wl_surface_commit(s);}
static void empty_input(struct wl_surface*s){struct wl_region*r=wl_compositor_create_region(comp);wl_surface_set_input_region(s,r);wl_region_destroy(r);wl_surface_commit(s);}
int main(void){
    struct wl_display*d=wl_display_connect(0);if(!d)return 1;
    struct wl_registry*r=wl_display_get_registry(d);wl_registry_add_listener(r,&registry_l,0);wl_display_roundtrip(d);wl_display_roundtrip(d);
    if(!comp||!subcomp||!shm||!wm)return 1;
    root=wl_compositor_create_surface(comp);struct xdg_surface*xs=xdg_wm_base_get_xdg_surface(wm,root);xdg_surface_add_listener(xs,&xs_l,0);
    struct xdg_toplevel*top=xdg_surface_get_toplevel(xs);xdg_toplevel_add_listener(top,&top_l,0);xdg_toplevel_set_title(top,"Wayland input");
    wl_surface_commit(root);while(!configured)if(wl_display_dispatch(d)<0)return 1;
    attach(root,320,200,0x0017c535);
    child=wl_compositor_create_surface(comp);struct wl_subsurface*sub=wl_subcompositor_get_subsurface(subcomp,child,root);
    wl_subsurface_set_position(sub,40,30);attach(child,100,80,0x00c34a62);wl_surface_commit(root);wl_subsurface_set_desync(sub);
    grand=wl_compositor_create_surface(comp);struct wl_subsurface*gsub=wl_subcompositor_get_subsurface(subcomp,grand,child);
    wl_subsurface_set_position(gsub,10,10);wl_subsurface_set_desync(gsub);
    for(stage=0;stage<13;stage++){
        if(stage==2){attach(grand,40,30,0x0030bb55);wl_surface_commit(child);}
        if(stage==3)empty_input(grand);
        if(stage==4){
            struct wl_region*mask=wl_compositor_create_region(comp);wl_region_add(mask,0,0,100,80);wl_region_subtract(mask,10,10,30,30);
            wl_surface_set_input_region(child,mask);wl_region_add(mask,10,10,30,30);wl_region_destroy(mask);wl_surface_commit(child);
        }
        if(stage==5){wl_pointer_release(pointers[0]);pointers[0]=0;states[0].focus=0;wl_surface_set_input_region(child,0);wl_surface_commit(child);}
        if(stage==6){
            pointers[0]=wl_seat_get_pointer(seats[0]);wl_pointer_add_listener(pointers[0],&pointer_l,&states[0]);
            if(wl_display_roundtrip(d)<0)return 1;
            if(!states[0].focus||states[0].focus!=states[1].focus){puts("wlinput-FAIL new pointer lacks current enter");return 2;}
        }
        if(stage==6){wl_subsurface_set_sync(sub);empty_input(child);}
        if(stage==7)wl_surface_commit(root);
        if(stage==8){wl_surface_set_input_region(child,0);wl_surface_commit(child);wl_surface_set_input_region(grand,0);wl_surface_commit(grand);wl_surface_commit(root);}
        if(stage==10){empty_input(grand);wl_subsurface_place_below(sub,root);wl_surface_commit(root);}
        if(stage==11)empty_input(root);
        if(stage==12){wl_surface_set_input_region(root,0);wl_surface_attach(child,0,0,0);wl_surface_commit(child);wl_surface_commit(root);}
        if(wl_display_roundtrip(d)<0)return 1;
        advance=0;printf("wlinput-ready:%d\n",stage);fflush(stdout);
        while(!advance)if(wl_display_dispatch(d)<0)return 1;
        if(wl_display_roundtrip(d)<0)return 1;
        for(int i=0;i<2;i++){
            int expected=i?stage+1:stage==5?5:stage>5?stage:stage+1;
            if(states[i].clicks!=expected){printf("wlinput-FAIL pointer=%d stage=%d clicks=%d expected=%d\n",i,stage,states[i].clicks,expected);return 2;}
            if(stage==8&&states[i].focus!=root){puts("wlinput-FAIL grab release focus");return 2;}
        }
        printf("wlinput-ok:%d pointers=%d,%d\n",stage,states[0].clicks,states[1].clicks);fflush(stdout);
    }
    puts("wlinput-wheel-ready");fflush(stdout);
    while(!states[0].axes||!states[1].axes)if(wl_display_dispatch(d)<0)return 1;
    if(wl_display_roundtrip(d)<0)return 1;
    if(!states[0].axis_value||states[0].axis_value!=states[1].axis_value||!states[0].discrete||!states[0].sources||!states[0].frames||states[1].discrete||states[1].sources||states[1].frames){puts("wlinput-FAIL wheel/version events");return 2;}
    puts("wlinput-wheel-ok v5=source+discrete+axis+frame v3=axis");
    puts("wlinput-done");wl_display_disconnect(d);return 0;
}
