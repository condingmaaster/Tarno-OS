// SPDX-License-Identifier: GPL-2.0-or-later
// A plain libwayland-client program (nothing THOS-specific): an xdg_toplevel with a wl_shm buffer, frame
// callbacks, pointer and keyboard listeners. Prints what it sees so the test can check the whole path.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

#define W 320
#define H 200

static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct xdg_wm_base *wm;
static struct wl_surface *surf;
static struct xdg_surface *xs;
static struct xdg_toplevel *top;
static struct wl_buffer *buf;
static uint32_t *px;
static int configured, closing, frames, phase, nframes_max = 6;
static struct wl_callback *frame_cb;

static void draw(void) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t c = 0x00203080;                         /* blue background */
            if (x < 40 && y < 40) c = 0x00C82828;              /* red marker, top-left */
            if (y >= 180) c = (x / 10 == phase % 32) ? 0x00FFFF00 : 0x00101010;   /* moving bar */
            px[y * W + x] = c;
        }
}

static void frame_done(void *d, struct wl_callback *cb, uint32_t t);
static const struct wl_callback_listener frame_l = { frame_done };
static void next_frame(void) {
    frame_cb = wl_surface_frame(surf);
    wl_callback_add_listener(frame_cb, &frame_l, 0);
    draw();
    wl_surface_attach(surf, buf, 0, 0);
    wl_surface_damage_buffer(surf, 0, 0, W, H);
    wl_surface_commit(surf);
}
static void frame_done(void *d, struct wl_callback *cb, uint32_t t) {
    (void)d; (void)t;
    wl_callback_destroy(cb);
    frames++; phase++;
    if (frames == nframes_max) { printf("wlclient: frames %d\n", frames); fflush(stdout); }
    if (!closing) next_frame();
}

static void xs_configure(void *d, struct xdg_surface *s, uint32_t serial) {
    (void)d;
    xdg_surface_ack_configure(s, serial);
    if (!configured) {
        configured = 1;
        printf("wlclient: configured\n"); fflush(stdout);
        next_frame();
    }
}
static const struct xdg_surface_listener xs_l = { xs_configure };
static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st) { (void)d; (void)t; (void)w; (void)h; (void)st; }
static void top_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; closing = 1; printf("wlclient: close requested\n"); fflush(stdout); }
static const struct xdg_toplevel_listener top_l = { top_configure, top_close };
static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t s) { (void)d; xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener wm_l = { wm_ping };

static void p_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y) { (void)d; (void)p; (void)s; (void)sf; printf("wlclient: pointer enter %d,%d\n", wl_fixed_to_int(x), wl_fixed_to_int(y)); fflush(stdout); }
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf) { (void)d; (void)p; (void)s; (void)sf; printf("wlclient: pointer leave\n"); fflush(stdout); }
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) { (void)d; (void)p; (void)t; (void)x; (void)y; }
static void p_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st) { (void)d; (void)p; (void)s; (void)t; printf("wlclient: button %u %s\n", b, st ? "pressed" : "released"); fflush(stdout); }
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) { (void)d; (void)p; (void)t; (void)a; (void)v; }
static void p_frame(void *d, struct wl_pointer *p) { (void)d; (void)p; }
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t s) { (void)d; (void)p; (void)s; }
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) { (void)d; (void)p; (void)t; (void)a; }
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) { (void)d; (void)p; (void)a; (void)v; }
static const struct wl_pointer_listener ptr_l = { p_enter, p_leave, p_motion, p_button, p_axis, p_frame, p_axis_source, p_axis_stop, p_axis_discrete };

static void k_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int fd, uint32_t size) {
    (void)d; (void)k;
    char *m = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
    printf("wlclient: keymap format %u, %u bytes, %s\n", fmt, size, m != MAP_FAILED && !strncmp(m, "xkb_keymap", 10) ? "text ok" : "unreadable");
    fflush(stdout);
    if (m != MAP_FAILED) munmap(m, size);
    close(fd);
}
static void k_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf, struct wl_array *keys) { (void)d; (void)k; (void)s; (void)sf; (void)keys; printf("wlclient: keyboard enter\n"); fflush(stdout); }
static void k_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf) { (void)d; (void)k; (void)s; (void)sf; printf("wlclient: keyboard leave\n"); fflush(stdout); }
static void k_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t st) { (void)d; (void)k; (void)s; (void)t; printf("wlclient: key %u %s\n", key, st ? "down" : "up"); fflush(stdout); }
static void k_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp) { (void)d; (void)k; (void)s; (void)lat; (void)lock; (void)grp; printf("wlclient: mods %u\n", dep); fflush(stdout); }
static void k_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay) { (void)d; (void)k; (void)rate; (void)delay; }
static const struct wl_keyboard_listener kbd_l = { k_keymap, k_enter, k_leave, k_key, k_mods, k_repeat };

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps) {
    (void)d;
    printf("wlclient: seat capabilities %u\n", caps); fflush(stdout);
    if (caps & WL_SEAT_CAPABILITY_POINTER) wl_pointer_add_listener(wl_seat_get_pointer(s), &ptr_l, 0);
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) wl_keyboard_add_listener(wl_seat_get_keyboard(s), &kbd_l, 0);
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_l = { seat_caps, seat_name };

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver) {
    (void)d;
    if (!strcmp(iface, "wl_compositor")) comp = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
    else if (!strcmp(iface, "wl_shm")) shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, "wl_seat")) { seat = wl_registry_bind(r, name, &wl_seat_interface, ver < 5 ? ver : 5); wl_seat_add_listener(seat, &seat_l, 0); }
    else if (!strcmp(iface, "xdg_wm_base")) { wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1); xdg_wm_base_add_listener(wm, &wm_l, 0); }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) { (void)d; (void)r; (void)name; }
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };

int main(int argc, char **argv) {
    if (argc > 1) nframes_max = atoi(argv[1]);
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) { printf("wlclient FAIL: cannot connect\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_l, 0);
    wl_display_roundtrip(dpy);
    if (!comp || !shm || !wm) { printf("wlclient FAIL: missing globals comp=%p shm=%p wm=%p\n", (void *)comp, (void *)shm, (void *)wm); return 1; }
    printf("wlclient: globals ok\n"); fflush(stdout);

    int fd = memfd_create("wlclient-pool", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, W * H * 4) != 0) { printf("wlclient FAIL: memfd\n"); return 1; }
    px = mmap(0, W * H * 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) { printf("wlclient FAIL: mmap\n"); return 1; }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, W * H * 4);
    buf = wl_shm_pool_create_buffer(pool, 0, W, H, W * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    surf = wl_compositor_create_surface(comp);
    xs = xdg_wm_base_get_xdg_surface(wm, surf);
    xdg_surface_add_listener(xs, &xs_l, 0);
    top = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(top, &top_l, 0);
    xdg_toplevel_set_title(top, "wlclient");
    wl_surface_commit(surf);                                /* first commit: no buffer, asks for the configure */
    while (!closing && wl_display_dispatch(dpy) != -1) {}
    printf("wlclient: bye\n"); fflush(stdout);
    return 0;
}
