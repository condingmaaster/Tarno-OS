// SPDX-License-Identifier: GPL-2.0-or-later
// THOS desktop: a Wayland wire-protocol server (the real protocol, so unmodified libwayland clients work).
// Included into thosdesk.c, after the compositor's window list and compose(). Implements
//   wl_display, wl_registry, wl_callback, wl_compositor, wl_surface (buffers are copied into the window at
//   commit), wl_region input masks, wl_shm / wl_shm_pool / wl_buffer, wl_seat (pointer + keyboard with a
//   German xkb keymap), wl_output, xdg_wm_base / xdg_surface / xdg_toplevel,
//   wl_subcompositor / wl_subsurface (compound windows), data devices and xdg popups.
// Limitations: subsurfaces outside the root buffer bounds, touch; damage is a union rectangle.
#include <sys/uio.h>
#include <time.h>
#include <limits.h>

#ifndef WLS_PATH
#define WLS_PATH "/run/wayland-0"
#endif
#define WLS_MAXCL 8
#define WLS_INCAP 16384

enum { O_FREE = 0, O_DISPLAY, O_REGISTRY, O_COMPOSITOR, O_SHM, O_POOL, O_BUFFER, O_SURFACE, O_CALLBACK, O_SEAT,
       O_POINTER, O_KEYBOARD, O_XDGWM, O_XDGSURF, O_TOPLEVEL, O_OUTPUT, O_STUB, O_REGION, O_DDM, O_SUBCOMP, O_POSITIONER, O_POPUP, O_DSRC, O_DDEV, O_DOFFER, O_SUBSURF };

struct wlpointer { struct wlpointer *next; uint32_t id, ver; };
struct wlkeyboard { struct wlkeyboard *next; uint32_t id; };
struct wlpos { int w, h, ax, ay, aw, ah, anchor, gravity, ox, oy; };
struct wlpool { void *base; size_t size; int fd, refs, destroyed; };
struct wlbuf { struct wlpool *pool; int off, w, h, stride; uint32_t fmt; };
struct wlobj { int type; uint32_t ver; void *p; };
struct wlregionop { int x,y,w,h,add; };
struct wlregion { struct wlregionop *ops; size_t n; int infinite; };
static void region_clear(struct wlregion *r) { free(r->ops);memset(r,0,sizeof *r); }
static int region_copy(struct wlregion *dst,const struct wlregion *src) {
    struct wlregion next={0};
    if(!src)next.infinite=1;
    else {next=*src;next.ops=0;if(src->n){next.ops=malloc(src->n*sizeof *src->ops);if(!next.ops)return 0;memcpy(next.ops,src->ops,src->n*sizeof *src->ops);}}
    region_clear(dst);*dst=next;return 1;
}
static int region_contains(const struct wlregion *r,int64_t x,int64_t y) {
    int hit=r->infinite;
    for(size_t i=0;i<r->n;i++){const struct wlregionop *q=&r->ops[i];if(x>=q->x&&y>=q->y&&x<(int64_t)q->x+q->w&&y<(int64_t)q->y+q->h)hit=q->add;}
    return hit;
}

struct wlsurf {
    struct wlsurf *next;
    struct wlc *c;
    uint32_t id;
    uint32_t pend_buf; int buf_set;           /* attach() since the last commit (pend_buf 0 = detach) */
    uint32_t cb_pending[8]; int ncb_pending;  /* frame() since the last commit */
    uint32_t cb_ready[16]; int ncb_ready;     /* committed: fire at the next frame tick */
    int dx0, dy0, dx1, dy1, has_dmg;
    uint32_t xdg_id, top_id;                  /* the xdg_surface and xdg_toplevel objects */
    int role_top, configured, winid, close_sent;
    char title[17];
    int gx, gy, gw, gh, has_geo;               /* xdg_surface.set_window_geometry */
    int is_popup, popup_x, popup_y, popup_grab, popup_id; /* popup placement (parent window coordinates) */
    struct wlsurf *parent;
    uint32_t popup_obj;
    /* A subsurface is part of its parent's window, not a separate desktop window.
       Keep the original pixels: recomposition must never blend over an old composite. */
    uint32_t *pixels, *cached_pixels;
    int pixel_w, pixel_h, cached_w, cached_h, cached_set;
    int is_sub, sub_sync, sub_added, sub_x, sub_y, sub_px, sub_py, sub_z, sub_pz;
    uint32_t sub_id, cb_cached[16]; int ncb_cached;
    struct wlregion input, pending_input, cached_input;
    int input_pending, input_cached;
};

struct wlc {
    int fd, used;
    uint8_t in[WLS_INCAP]; size_t inlen;
    int infd[32]; int ninfd;
    uint8_t *out; size_t outlen, outcap;
    struct wlobj *objs; uint32_t nobjs;
    struct wlsurf *surfs;
    uint32_t serial, ddev_id, soff;
    struct wlpointer *pointers;
    struct wlkeyboard *keyboards;
};

static struct wlc wlcl[WLS_MAXCL];
static int wls_listen_fd = -1;
static struct wlsurf *wls_pfocus, *wls_kfocus, *wls_grab;
static uint32_t wls_mods;
static uint8_t wls_hid_down[256];
static char *wls_keymap; static size_t wls_keymap_len;
static unsigned long long wls_last_tick;
static int last_x = -1, last_y = -1;

static unsigned long long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---------- output ---------- */
struct ob { uint8_t b[640]; size_t n; };
static void ob_u32(struct ob *o, uint32_t v) { if (o->n + 4 <= sizeof o->b) { memcpy(o->b + o->n, &v, 4); o->n += 4; } }
static void ob_str(struct ob *o, const char *s) {
    uint32_t len = (uint32_t)strlen(s) + 1, pad = (len + 3) & ~3u;
    if (o->n + 4 + pad > sizeof o->b) return;
    ob_u32(o, len);
    memset(o->b + o->n, 0, pad); memcpy(o->b + o->n, s, len); o->n += pad;
}
static void wl_queue(struct wlc *c, const void *p, size_t n) {
    if (c->outlen + n > c->outcap) {
        size_t cap = c->outcap ? c->outcap * 2 : 4096;
        while (cap < c->outlen + n) cap *= 2;
        if (cap > (1u << 22)) { close(c->fd); c->fd = -1; return; }   /* a client that never reads: drop it */
        uint8_t *q = realloc(c->out, cap);
        if (!q) return;
        c->out = q; c->outcap = cap;
    }
    memcpy(c->out + c->outlen, p, n); c->outlen += n;
}
static void wl_flush(struct wlc *c) {
    while (c->fd >= 0 && c->outlen) {
        ssize_t n = send(c->fd, c->out, c->outlen, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n <= 0) break;
        memmove(c->out, c->out + n, c->outlen - (size_t)n); c->outlen -= (size_t)n;
    }
}
static void wl_emit(struct wlc *c, uint32_t id, uint32_t op, const struct ob *o) {
    uint32_t h[2] = { id, (uint32_t)((8 + (o ? o->n : 0)) << 16) | op };
    wl_queue(c, h, 8);
    if (o && o->n) wl_queue(c, o->b, o->n);
}
static void wl_ev(struct wlc *c, uint32_t id, uint32_t op, int n, ...) {
    struct ob o = { .n = 0 };
    __builtin_va_list ap; __builtin_va_start(ap, n);
    for (int i = 0; i < n; i++) ob_u32(&o, __builtin_va_arg(ap, uint32_t));
    __builtin_va_end(ap);
    wl_emit(c, id, op, &o);
}
/* A client may bind the same seat more than once or own several pointer objects.
   Keep resources separate; high-version frame/axis events must not reach v3. */
static void wl_pointer_ev(struct wlc *c, uint32_t op, int n, ...) {
    struct ob o = { .n = 0 };
    __builtin_va_list ap; __builtin_va_start(ap, n);
    for (int i = 0; i < n; i++) ob_u32(&o, __builtin_va_arg(ap, uint32_t));
    __builtin_va_end(ap);
    for (struct wlpointer *p = c->pointers; p; p = p->next)
        if (op < 5 || p->ver >= 5) wl_emit(c, p->id, op, &o);
}
static void wl_keyboard_ev(struct wlc *c, uint32_t op, int n, ...) {
    struct ob o = { .n = 0 };
    __builtin_va_list ap; __builtin_va_start(ap, n);
    for (int i = 0; i < n; i++) ob_u32(&o, __builtin_va_arg(ap, uint32_t));
    __builtin_va_end(ap);
    for (struct wlkeyboard *k = c->keyboards; k; k = k->next) wl_emit(c, k->id, op, &o);
}
static void wl_delete_id(struct wlc *c, uint32_t id) { wl_ev(c, 1, 1, 1, id); }
static void wl_error(struct wlc *c, uint32_t obj, uint32_t code, const char *msg) {
    struct ob o = { .n = 0 };
    ob_u32(&o, obj); ob_u32(&o, code); ob_str(&o, msg);
    wl_emit(c, 1, 0, &o);
    wl_flush(c);
    printf("desk: wayland protocol error %u on object %u: %s\n", code, obj, msg); fflush(stdout);
    if (c->fd >= 0) { close(c->fd); c->fd = -1; }
}

/* ---------- objects ---------- */
/* client ids live below 0xF000; ids the server allocates (data offers) start at 0xff000000 and are mapped just above */
static uint32_t oidx(uint32_t id) { return id >= 0xff000000u ? 0xF000u + (id - 0xff000000u) : id; }
static struct wlobj *obj_get(struct wlc *c, uint32_t id) { uint32_t i = oidx(id); return id && i < c->nobjs && c->objs[i].type ? &c->objs[i] : 0; }
static struct wlobj *obj_new(struct wlc *c, uint32_t id, int type, uint32_t ver, void *p) {
    uint32_t i = oidx(id);
    if (id == 0 || i >= 0x10000 || (id < 0xff000000u && id >= 0xF000u)) { wl_error(c, 1, 0, "bad new id"); return 0; }
    if (i >= c->nobjs) {
        uint32_t n = i + 64;
        struct wlobj *q = realloc(c->objs, n * sizeof *q);
        if (!q) return 0;
        memset(q + c->nobjs, 0, (n - c->nobjs) * sizeof *q);
        c->objs = q; c->nobjs = n;
    }
    if (c->objs[i].type) { wl_error(c, 1, 0, "object id already in use"); return 0; }
    c->objs[i] = (struct wlobj){ type, ver, p };
    return &c->objs[i];
}
static void obj_free(struct wlc *c, uint32_t id) { uint32_t i = oidx(id); if (i < c->nobjs) c->objs[i] = (struct wlobj){ 0, 0, 0 }; }

/* ---------- argument parsing ---------- */
struct msg { const uint8_t *p; size_t len, off; int bad; struct wlc *c; };
static uint32_t m_u32(struct msg *m) { uint32_t v = 0; if (m->off + 4 > m->len) { m->bad = 1; return 0; } memcpy(&v, m->p + m->off, 4); m->off += 4; return v; }
static int m_fd(struct msg *m) {
    if (m->c->ninfd == 0) { m->bad = 1; return -1; }
    int fd = m->c->infd[0];
    memmove(m->c->infd, m->c->infd + 1, (size_t)(m->c->ninfd - 1) * sizeof(int)); m->c->ninfd--;
    return fd;
}
static int m_str(struct msg *m, char *dst, size_t cap) {
    uint32_t n = m_u32(m);
    if (m->bad) return -1;
    uint32_t pad = (n + 3) & ~3u;
    if (n == 0 || m->off + pad > m->len || m->p[m->off + n - 1] != 0) { m->bad = 1; return -1; }
    size_t k = n - 1 < cap - 1 ? n - 1 : cap - 1;
    memcpy(dst, m->p + m->off, k); dst[k] = 0;
    m->off += pad;
    return (int)k;
}

/* ---------- surfaces and windows ---------- */
static struct win *win_by_id(int id) { for (int i = 0; i < nwin; i++) if (wins[i].id == id) return &wins[i]; return 0; }
static int win_index_by_id(int id) { for (int i = 0; i < nwin; i++) if (wins[i].id == id) return i; return -1; }
static void subs_render(struct wlsurf *s);
static void surf_unmap(struct wlsurf *s) {
    int i = s->winid ? win_index_by_id(s->winid) : -1;
    s->winid = 0;
    if (i >= 0) { wins[i].wl = 0; remove_win(i); }
}
static void surf_free(struct wlsurf *s) {
    struct wlsurf *parent = s->is_sub ? s->parent : 0;
    for (struct wlsurf *ch = s->c->surfs; ch; ch = ch->next)
        if (ch->parent == s) { ch->parent = 0; ch->sub_added = 0; }
    for (uint32_t i = 0; i < s->c->nobjs; i++)
        if (s->c->objs[i].type == O_SUBSURF && s->c->objs[i].p == s) s->c->objs[i].p = 0;
    surf_unmap(s);
    if (wls_pfocus == s) wls_pfocus = 0;
    if (wls_kfocus == s) wls_kfocus = 0;
    if (wls_grab == s) wls_grab = 0;
    for (struct wlsurf **pp = &s->c->surfs; *pp; pp = &(*pp)->next)
        if (*pp == s) { *pp = s->next; break; }
    free(s->pixels); free(s->cached_pixels);
    region_clear(&s->input);region_clear(&s->pending_input);region_clear(&s->cached_input);free(s);
    if (parent) subs_render(parent);
}
static void buf_release(struct wlc *c, uint32_t id) { wl_ev(c, id, 0, 0); }

static uint32_t swizzle(uint32_t px) {
    /* wl_shm ARGB/XRGB8888 is 0xAARRGGBB; the framebuffer wants the channels at rs/gs/bs */
    return px & 0x00FFFFFFu;                            /* the compositor works in canonical 0x00RRGGBB and swaps channels only when it blits */
}

static void wls_focus_keyboard(struct wlsurf *s);
struct wlc;
static void wls_send_selection(struct wlc *c);
static void desk_begin_move(int winid);
static void desk_begin_resize(int winid, int edges);
static void desk_set_maximized(int winid, int on);
static void desk_set_fullscreen(int winid, int on);
/* Subsurface buffers are copied before release, including synchronized cached state. */
static int subs_synchronized(struct wlsurf *s) {
    for (; s && s->is_sub; s = s->parent) if (s->sub_sync) return 1;
    return 0;
}
static int subs_apply_cached(struct wlsurf *s) {
    int changed = s->cached_set;
    if(s->input_cached){region_clear(&s->input);s->input=s->cached_input;memset(&s->cached_input,0,sizeof s->cached_input);s->input_cached=0;}
    if (s->cached_set) {
        free(s->pixels); s->pixels = s->cached_pixels; s->cached_pixels = 0;
        s->pixel_w = s->cached_w; s->pixel_h = s->cached_h; s->cached_set = 0;
    }
    for (int i = 0; i < s->ncb_cached && s->ncb_ready < 16; i++) s->cb_ready[s->ncb_ready++] = s->cb_cached[i];
    s->ncb_cached = 0;
    return changed;
}
static int subs_apply_children(struct wlsurf *s, int ancestor_sync) {
    int changed = 0;
    for (struct wlsurf *ch = s->c->surfs; ch; ch = ch->next) if (ch->is_sub && ch->parent == s) {
        changed |= !ch->sub_added || ch->sub_x != ch->sub_px || ch->sub_y != ch->sub_py || ch->sub_z != ch->sub_pz;
        ch->sub_added = 1; ch->sub_x = ch->sub_px; ch->sub_y = ch->sub_py; ch->sub_z = ch->sub_pz;
        if (ancestor_sync || ch->sub_sync) {
            changed |= subs_apply_cached(ch); changed |= subs_apply_children(ch, 1);
        }
    }
    return changed;
}
static void subs_paint(struct wlsurf *s, struct win *w, int64_t ox, int64_t oy, int depth);
static void subs_paint_children(struct wlsurf *s, struct win *w, int64_t ox, int64_t oy, int depth, int above) {
    int last = INT_MIN;
    for (;;) {
        struct wlsurf *pick = 0;
        for (struct wlsurf *ch = s->c->surfs; ch; ch = ch->next)
            if (ch->is_sub && ch->parent == s && ch->sub_added && ch->pixels &&
                (ch->sub_z > 0) == above && ch->sub_z > last && (!pick || ch->sub_z < pick->sub_z)) pick = ch;
        if (!pick) break;
        last = pick->sub_z;
        subs_paint(pick, w, ox + pick->sub_x, oy + pick->sub_y, depth + 1);
    }
}
static void subs_paint(struct wlsurf *s, struct win *w, int64_t ox, int64_t oy, int depth) {
    if (!s->pixels || depth > 32) return;
    subs_paint_children(s, w, ox, oy, depth, 0);
    int x0 = ox < -s->pixel_w ? s->pixel_w : ox < 0 ? (int)-ox : 0;
    int y0 = oy < -s->pixel_h ? s->pixel_h : oy < 0 ? (int)-oy : 0;
    int x1 = ox >= w->w ? 0 : w->w - ox < s->pixel_w ? (int)(w->w - ox) : s->pixel_w;
    int y1 = oy >= w->h ? 0 : w->h - oy < s->pixel_h ? (int)(w->h - oy) : s->pixel_h;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        uint32_t src = s->pixels[(size_t)y * s->pixel_w + x];
        uint32_t *dst = &w->px[(size_t)(oy + y) * w->w + ox + x];
        unsigned a = src >> 24, inv = 255 - a;
        if (a == 255) *dst = src;
        else if (a) {
            unsigned da = a + ((*dst >> 24) * inv + 127) / 255;
            unsigned r = ((src >> 16) & 255) + (((*dst >> 16) & 255) * inv + 127) / 255;
            unsigned g = ((src >> 8) & 255) + (((*dst >> 8) & 255) * inv + 127) / 255;
            unsigned b = (src & 255) + ((*dst & 255) * inv + 127) / 255;
            *dst = (da << 24) | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
        }
    }
    subs_paint_children(s, w, ox, oy, depth, 1);
}
static void subs_render(struct wlsurf *s) {
    while (s && s->is_sub) s = s->parent;
    if (!s) return;
    struct win *w = s->winid ? win_by_id(s->winid) : 0;
    if (!w || !w->px || !s->pixels) return;
    memset(w->px, 0, (size_t)w->w * w->h * 4);
    subs_paint(s, w, 0, 0, 0);
    compose(outer_of(w));
}
static void subs_commit(struct wlc *c, struct wlsurf *s) {
    if(s->input_pending){region_clear(&s->cached_input);s->cached_input=s->pending_input;memset(&s->pending_input,0,sizeof s->pending_input);s->input_pending=0;s->input_cached=1;}
    if (s->buf_set) {
        struct wlobj *o = obj_get(c, s->pend_buf);
        struct wlbuf *b = o && o->type == O_BUFFER ? o->p : 0;
        if (s->pend_buf && !b) { wl_error(c, s->id, 0, "invalid subsurface buffer"); return; }
        uint32_t *px = 0;
        if (b) {
            if (!b->pool->base || b->off < 0 || b->w <= 0 || b->h <= 0 || b->w > 2000 || b->h > 2000 || b->stride < b->w * 4 ||
                (size_t)b->off + (size_t)b->stride * b->h > b->pool->size) { wl_error(c, s->pend_buf, 0, "buffer outside the pool"); return; }
            px = malloc((size_t)b->w * b->h * 4);
            if (!px) { wl_error(c, s->id, 0, "cannot copy subsurface buffer"); return; }
            for (int y = 0; y < b->h; y++) for (int x = 0; x < b->w; x++) {
                uint32_t v; memcpy(&v, (uint8_t *)b->pool->base + b->off + (size_t)y * b->stride + x * 4, 4);
                px[(size_t)y * b->w + x] = swizzle(v) | (b->fmt == 0 ? v & 0xff000000u : 0xff000000u);
            }
        }
        free(s->cached_pixels); s->cached_pixels = px; s->cached_set = 1;
        s->cached_w = b ? b->w : 0; s->cached_h = b ? b->h : 0;
        s->buf_set = 0; s->has_dmg = 0;
        if (b) buf_release(c, s->pend_buf);
    }
    for (int i = 0; i < s->ncb_pending && s->ncb_cached < 16; i++) s->cb_cached[s->ncb_cached++] = s->cb_pending[i];
    s->ncb_pending = 0;
    if (!subs_synchronized(s)) { int changed = subs_apply_cached(s); changed |= subs_apply_children(s, 0); if (changed) subs_render(s); }
}
static void surf_commit(struct wlc *c, struct wlsurf *s) {
    if (s->is_sub) { subs_commit(c, s); return; }
    if(s->input_pending){region_clear(&s->input);s->input=s->pending_input;memset(&s->pending_input,0,sizeof s->pending_input);s->input_pending=0;}
    int child_changed = subs_apply_children(s, 0);
    for (int i = 0; i < s->ncb_pending && s->ncb_ready < 16; i++) s->cb_ready[s->ncb_ready++] = s->cb_pending[i];
    s->ncb_pending = 0;
    if (!s->buf_set) { if (child_changed) subs_render(s); return; }
    s->buf_set = 0;
    struct wlobj *bo = obj_get(c, s->pend_buf);
    struct wlbuf *b = bo && bo->type == O_BUFFER ? bo->p : 0;
    if (!b) {                                         /* detached: the window disappears */
        if (s->winid) surf_unmap(s);
        return;
    }
    if ((!s->role_top && !s->is_popup) || !s->configured || !b->pool->base) { buf_release(c, s->pend_buf); return; }
    if (b->w <= 0 || b->h <= 0 || b->w > 2000 || b->h > 2000 || b->stride < b->w * 4 ||
        (size_t)b->off + (size_t)b->stride * b->h > b->pool->size) { wl_error(c, s->pend_buf, 0, "buffer outside the pool"); return; }
    struct win *w = s->winid ? win_by_id(s->winid) : 0;
    struct rect old = { 0, 0, 0, 0 };
    int fresh = 0;
    if (!w) {
        fresh = 1;
        if (nwin >= MAXWIN) { buf_release(c, s->pend_buf); return; }
        w = &wins[nwin++];
        int k = next_id - 1;
        *w = (struct win){ -1, next_id++, 100 + 80 * (k % 6), 80 + 60 * (k % 6), b->w, b->h, calloc((size_t)b->w * b->h, 4) };
        w->deco = 1; w->wl = s; w->is_wl = 1;
        if (s->is_popup) {                                /* menus: no frame, placed relative to the parent window */
            struct win *pw = s->parent && s->parent->winid ? win_by_id(s->parent->winid) : 0;
            w->deco = 0; w->popup = 1;
            w->x = (pw ? pw->x + (pw->csd ? pw->gx : 0) : 100) + s->popup_x;
            w->y = (pw ? pw->y + (pw->csd ? pw->gy : 0) : 80) + s->popup_y;
        }
        /* a translucent buffer with a smaller window geometry = the client draws its own frame and shadow (GTK) */
        if (b->fmt == 0 && s->has_geo && (s->gw < b->w || s->gh < b->h)) {
            w->deco = 0; w->csd = 1; w->gx = s->gx; w->gy = s->gy; w->gw = s->gw; w->gh = s->gh;
            w->x -= s->gx; w->y -= s->gy;
        }
        memcpy(w->title, s->title, sizeof w->title);
        winlist_dirty = 1;
        if (!s->is_popup) place_window(w);                  /* centred in the free area, big windows (a browser) never hang off the screen */
        s->winid = w->id;
        s->has_dmg = 0;
        printf("desk: wayland window %d mapped %dx%d\n", w->id, b->w, b->h); fflush(stdout);
    } else if (w->w != b->w || w->h != b->h) {
        old = outer_of(w);
        free(w->px);
        w->px = calloc((size_t)b->w * b->h, 4);
        w->w = b->w; w->h = b->h;
        s->has_dmg = 0;
    }
    if (!w->px) { buf_release(c, s->pend_buf); return; }
    if (!s->pixels || s->pixel_w != b->w || s->pixel_h != b->h) {
        uint32_t *base = calloc((size_t)b->w * b->h, 4);
        if (!base) { buf_release(c, s->pend_buf); return; }
        free(s->pixels); s->pixels = base; s->pixel_w = b->w; s->pixel_h = b->h;
        s->has_dmg = 0;
    }
    int x0 = 0, y0 = 0, x1 = b->w, y1 = b->h;
    if (s->has_dmg) {
        x0 = s->dx0 < 0 ? 0 : s->dx0; y0 = s->dy0 < 0 ? 0 : s->dy0;
        x1 = s->dx1 > b->w ? b->w : s->dx1; y1 = s->dy1 > b->h ? b->h : s->dy1;
        if (x1 <= x0 || y1 <= y0) { x0 = 0; y0 = 0; x1 = b->w; y1 = b->h; }
    }
    if (w->csd && s->has_geo) { w->gx = s->gx; w->gy = s->gy; w->gw = s->gw; w->gh = s->gh; }
    if (!fresh && (w->maxed || w->fullscreen) && !w->popup) {         /* the client's frame (shadow margins) changes with the state: keep its visible part on the screen origin */
        int nx = w->csd ? -w->gx : (w->fullscreen ? 0 : w->x), ny = w->csd ? -w->gy : (w->fullscreen ? 0 : w->y);
        if (nx != w->x || ny != w->y) {
            struct rect before = outer_of(w);
            w->x = nx; w->y = ny;
            old = old.x1 > old.x0 ? uni(old, before) : before;
        }
    }
    int fast = rs == 16 && gs == 8 && bs == 0;
    w->alpha = b->fmt == 0;                           /* ARGB8888 (premultiplied); XRGB8888 is opaque */
    for (int y = y0; y < y1; y++) {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)b->pool->base + b->off + (size_t)y * b->stride) + x0;
        uint32_t *dst = s->pixels + (size_t)y * s->pixel_w + x0;
        if (w->alpha) {
            if (fast) for (int x = x0; x < x1; x++) *dst++ = *src++;
            else for (int x = x0; x < x1; x++, src++) *dst++ = swizzle(*src) | (*src & 0xFF000000u);
        } else if (fast) for (int x = x0; x < x1; x++) *dst++ = (*src++ & 0x00FFFFFFu) | 0xff000000u;
        else for (int x = x0; x < x1; x++) *dst++ = swizzle(*src++) | 0xff000000u;
    }
    s->has_dmg = 0;
    int children = 0;
    for (struct wlsurf *ch = c->surfs; ch; ch = ch->next)
        if (ch->is_sub && ch->parent == s && ch->sub_added && ch->pixels) { children = 1; break; }
    if (children) {
        memset(w->px, 0, (size_t)w->w * w->h * 4); subs_paint(s, w, 0, 0, 0);
    } else {
        // Ordinary Wayland clients keep their partial-damage path. No full repaint for small updates.
        for (int y = y0; y < y1; y++) memcpy(w->px + (size_t)y * w->w + x0,
            s->pixels + (size_t)y * s->pixel_w + x0, (size_t)(x1-x0) * 4);
    }
    buf_release(c, s->pend_buf);
    if (fresh) { win_anim_start(w); w->ready = 1; compose(outer_of(w)); wls_focus_keyboard(s); }   /* titlebar, border and shadow too; the new window takes the keyboard */
    else if (old.x1 > old.x0) compose(uni(old, outer_of(w)));
    else compose(isect(content_of(w), rect_xywh(w->x + x0, w->y + y0, x1 - x0, y1 - y0)));
}

/* ---------- registry ---------- */
static const struct { const char *name; uint32_t ver; } wls_globals[] = {
    { "wl_compositor", 4 }, { "wl_shm", 1 }, { "wl_seat", 5 }, { "wl_output", 2 }, { "xdg_wm_base", 3 },
    { "wl_data_device_manager", 3 }, { "wl_subcompositor", 1 },
};
#define WLS_NGLOBALS (int)(sizeof wls_globals / sizeof wls_globals[0])

static void send_output(struct wlc *c, uint32_t id, uint32_t ver) {
    struct ob o = { .n = 0 };
    ob_u32(&o, 0); ob_u32(&o, 0); ob_u32(&o, 340); ob_u32(&o, 190); ob_u32(&o, 0);
    ob_str(&o, "THOS"); ob_str(&o, "framebuffer"); ob_u32(&o, 0);
    wl_emit(c, id, 0, &o);
    wl_ev(c, id, 1, 4, 3u, (uint32_t)W, (uint32_t)H, 60000u);
    if (ver >= 2) { wl_ev(c, id, 3, 1, 1u); wl_ev(c, id, 2, 0); }
}

static void registry_bind(struct wlc *c, struct msg *m) {
    uint32_t name = m_u32(m);
    char iface[64];
    if (m_str(m, iface, sizeof iface) < 0) return;
    uint32_t ver = m_u32(m), id = m_u32(m);
    if (m->bad || name < 1 || name > (uint32_t)WLS_NGLOBALS || strcmp(iface, wls_globals[name - 1].name) != 0) { wl_error(c, 2, 0, "unknown global"); return; }
    if (ver > wls_globals[name - 1].ver) ver = wls_globals[name - 1].ver;
    static const int types[] = { O_COMPOSITOR, O_SHM, O_SEAT, O_OUTPUT, O_XDGWM, O_DDM, O_SUBCOMP };
    if (!obj_new(c, id, types[name - 1], ver, 0)) return;
    if (name == 2) { wl_ev(c, id, 0, 1, 0u); wl_ev(c, id, 0, 1, 1u); }           /* shm formats: ARGB8888, XRGB8888 */
    else if (name == 3) {
        wl_ev(c, id, 0, 1, 3u);                                                  /* capabilities: pointer | keyboard */
        if (ver >= 2) { struct ob o = { .n = 0 }; ob_str(&o, "seat0"); wl_emit(c, id, 1, &o); }
    } else if (name == 4) send_output(c, id, ver);
}

/* ---------- input ---------- */
static const uint8_t hid2evdev[256] = {
    [0x04]=30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44,   /* a..z */
    [0x1E]=2,3,4,5,6,7,8,9,10,11,                                                            /* 1..9 0 */
    [0x28]=28,1,14,15,57,12,13,26,27,43,43,39,40,41,51,52,53,58,                             /* Enter Esc Bksp Tab Space - = [ ] \ # ; ' ` , . / Caps */
    [0x3A]=59,60,61,62,63,64,65,66,67,68,87,88,99,70,119,110,102,104,111,107,109,106,105,108,103,69,   /* F1..F12 PrtSc Scroll Pause Ins Home PgUp Del End PgDn Right Left Down Up NumLock */
    [0x54]=98,55,74,78,96,79,80,81,75,76,77,71,72,73,82,83,86,127,                           /* KP / * - + Enter 1..9 0 . , <> Menu */
    [0xE0]=29,42,56,125,97,54,100,126,
};

static uint32_t xkb_mods(unsigned hidmods) {
    uint32_t m = 0;
    if (hidmods & 0x22) m |= 1;      /* Shift */
    if (hidmods & 0x11) m |= 4;      /* Control */
    if (hidmods & 0x04) m |= 8;      /* Alt (Mod1) */
    if (hidmods & 0x40) m |= 0x80;   /* AltGr = ISO_Level3_Shift (Mod5) */
    if (hidmods & 0x88) m |= 0x40;   /* Super (Mod4) */
    return m;
}

/* Observe all raw releases, including keys consumed by compositor shortcuts.
   Snapshot only ordinary delivered keys and shared modifiers, so a consumed
   shortcut never introduces a stray press/release pair into a Wayland client. */
static void wls_track_key(unsigned hid,int press,unsigned hidmods,int eligible) {
    if(hid<256) {if(!press)wls_hid_down[hid]=0;else if(eligible)wls_hid_down[hid]=1;}
    wls_mods=xkb_mods(hidmods);
}

/* Every new resource/focus starts with the seat's actual logically held keys.
   The following modifiers event completes the initial keyboard state. */
static void wls_keyboard_enter(struct wlc *c, uint32_t id, struct wlsurf *s) {
    struct ob o = { .n = 0 };uint8_t seen[256]={0};uint32_t keys[256];size_t count=0;
    for(unsigned hid=0;hid<256;hid++) {
        unsigned code=hid2evdev[hid];
        if(wls_hid_down[hid]&&code&&!seen[code]) {seen[code]=1;keys[count++]=code;}
    }
    if(12+4*count>sizeof o.b) {wl_error(c,id,0,"keyboard snapshot too large");return;}
    ob_u32(&o, ++c->serial); ob_u32(&o, s->id); ob_u32(&o, (uint32_t)(count*4));
    for(size_t i=0;i<count;i++)ob_u32(&o,keys[i]);
    wl_emit(c, id, 1, &o);
    wl_ev(c, id, 4, 5, ++c->serial, wls_mods, 0u, 0u, 0u);
}
static void wls_focus_keyboard(struct wlsurf *s) {
    if (wls_kfocus == s) return;
    if (wls_kfocus && wls_kfocus->c->keyboards) {
        struct wlc *c = wls_kfocus->c;
        wl_keyboard_ev(c, 2, 2, ++c->serial, wls_kfocus->id);
        wl_flush(c);
    }
    wls_kfocus = s;
    if (s && s->c->keyboards) {
        struct wlc *c = s->c;
        for (struct wlkeyboard *k = c->keyboards; k; k = k->next) wls_keyboard_enter(c, k->id, s);
        wl_flush(c);
        wls_send_selection(c);
    }
}

/* a key event for the focused window (HID usage, press/release, modifier byte) */
static void wls_key(struct wlsurf *s, unsigned hid, int press, unsigned hidmods) {
    struct wlc *c = s->c;
    wls_focus_keyboard(s);
    if (!c->keyboards) return;
    uint32_t m = xkb_mods(hidmods);
    unsigned code = hid < 256 ? hid2evdev[hid] : 0;
    int down=hid<256&&wls_hid_down[hid];
    /* Repeat is generated by the client from repeat_info; unmatched shortcut
       releases must not be sent for keys absent from its logical state. */
    if(code && ((press&&!down)||(!press&&down)))
        wl_keyboard_ev(c, 3, 4, ++c->serial, (uint32_t)now_ms(), code, press ? 1u : 0u);
    if (m != wls_mods) { wls_mods = m; wl_keyboard_ev(c, 4, 5, ++c->serial, m, 0u, 0u, 0u); }
    wl_flush(c);
}

static struct wlsurf *wls_hit_surface(struct wlsurf *s,int64_t x,int64_t y,int depth);
static struct wlsurf *wls_hit_children(struct wlsurf *s,int64_t x,int64_t y,int depth,int above) {
    int last=INT_MAX;
    for(;;){
        struct wlsurf *pick=0;
        for(struct wlsurf *ch=s->c->surfs;ch;ch=ch->next)
            if(ch->is_sub&&ch->parent==s&&ch->sub_added&&ch->pixels&&(ch->sub_z>0)==above&&ch->sub_z<last&&(!pick||ch->sub_z>pick->sub_z))pick=ch;
        if(!pick)return 0;
        last=pick->sub_z;
        struct wlsurf *hit=wls_hit_surface(pick,x-pick->sub_x,y-pick->sub_y,depth+1);
        if(hit)return hit;
    }
}
static struct wlsurf *wls_hit_surface(struct wlsurf *s,int64_t x,int64_t y,int depth) {
    if(!s||!s->pixels||depth>32)return 0;
    struct wlsurf *hit=wls_hit_children(s,x,y,depth,1);if(hit)return hit;
    if(x>=0&&y>=0&&x<s->pixel_w&&y<s->pixel_h&&region_contains(&s->input,x,y))return s;
    return wls_hit_children(s,x,y,depth,0);
}
static int wls_surface_origin(struct wlsurf *s,int64_t *x,int64_t *y) {
    int64_t dx=0,dy=0;int depth=0;
    while(s&&s->is_sub&&depth++<32){dx+=s->sub_x;dy+=s->sub_y;s=s->parent;}
    struct win *w=s?win_by_id(s->winid):0;if(!w)return 0;
    *x=w->x+dx;*y=w->y+dy;return 1;
}
static int wls_pointer_coord(int64_t value) {
    if(value<INT_MIN/256)return INT_MIN/256;
    if(value>INT_MAX/256)return INT_MAX/256;
    return (int)value;
}

/* Deliver input to the topmost eligible surface, matching composition order. */
static void wls_pointer(int mx_, int my_, int btn, int prev_btn) {
    struct wlsurf *over = 0; struct win *ow = 0;
    for (int i = nwin - 1; i >= 0; i--) {
        if (!in(frame_of(&wins[i]), mx_, my_)) continue;
        if (wins[i].wl && in(content_of(&wins[i]), mx_, my_)) { over = wls_hit_surface(wins[i].wl,(int64_t)mx_-wins[i].x,(int64_t)my_-wins[i].y,0); ow = &wins[i]; }
        break;
    }
    struct wlsurf *tgt = wls_grab ? wls_grab : over;
    if (tgt != wls_pfocus) {
        if (wls_pfocus && wls_pfocus->c->pointers) {
            struct wlc *c = wls_pfocus->c;
            wl_pointer_ev(c, 1, 2, ++c->serial, wls_pfocus->id);
            wl_pointer_ev(c, 5, 0);
            wl_flush(c);
        }
        wls_pfocus = tgt;
        if (tgt && tgt->c->pointers) {
            int64_t ox,oy;
            if (wls_surface_origin(tgt,&ox,&oy)) {
                struct wlc *c = tgt->c;
                int sx=wls_pointer_coord((int64_t)mx_-ox),sy=wls_pointer_coord((int64_t)my_-oy);
                wl_pointer_ev(c, 0, 4, ++c->serial, tgt->id, (uint32_t)(sx*256), (uint32_t)(sy*256));
                wl_pointer_ev(c, 5, 0);
                wl_flush(c);
            }
        }
    }
    if (!tgt || !tgt->c->pointers) { if (!btn) wls_grab = 0; return; }
    int64_t ox,oy;if(!wls_surface_origin(tgt,&ox,&oy)){wls_grab=0;return;}
    struct wlc *c = tgt->c;
    int sx=wls_pointer_coord((int64_t)mx_-ox),sy=wls_pointer_coord((int64_t)my_-oy);
    if (sx != last_x || sy != last_y) {
        wl_pointer_ev(c, 2, 3, (uint32_t)now_ms(), (uint32_t)(sx * 256), (uint32_t)(sy * 256));
        last_x = sx; last_y = sy;
    }
    if (btn != prev_btn) {
        if (btn && ow) wls_grab = tgt;
        wl_pointer_ev(c, 3, 4, ++c->serial, (uint32_t)now_ms(), 0x110u, btn ? 1u : 0u);   /* BTN_LEFT */
        if(getenv("WLS_INPUT_LOG"))fprintf(stderr,"wl-pointer surface=%u x=%d y=%d button=%d\n",tgt->id,sx,sy,btn);
        if (!btn) wls_grab = 0;
    }
    wl_pointer_ev(c, 5, 0);
    wl_flush(c);
    if(!btn&&prev_btn&&over!=tgt)wls_pointer(mx_,my_,0,0);
}

/* ask the client to use a new size (and tell it whether it is maximized) */
static void wls_configure(struct wlsurf *s, int w, int h, int maximized) {
    struct wlc *c = s->c;
    if (!s->top_id || !s->xdg_id) return;
    struct ob e = { .n = 0 };
    ob_u32(&e, (uint32_t)w); ob_u32(&e, (uint32_t)h);
    ob_u32(&e, maximized ? 8 : 4);   /* states array: activated [, maximized | fullscreen] */
    ob_u32(&e, 4);
    if (maximized == 2) ob_u32(&e, 2);                /* XDG_TOPLEVEL_STATE_FULLSCREEN */
    else if (maximized) ob_u32(&e, 1);
    wl_emit(c, s->top_id, 0, &e);
    wl_ev(c, s->xdg_id, 0, 1, ++c->serial);
    wl_flush(c);
}

/* a click outside every grabbing popup dismisses them (xdg_popup.popup_done) */
static void wls_dismiss_popups(struct wlsurf *except) {
    for (int i = 0; i < WLS_MAXCL; i++) {
        if (!wlcl[i].used) continue;
        for (struct wlsurf *s = wlcl[i].surfs; s; s = s->next)
            if (s->is_popup && s->popup_grab && s != except && s->winid) {
                wl_ev(s->c, s->popup_obj, 1, 0);          /* popup_done */
                wl_flush(s->c);
                surf_unmap(s);
                s->popup_grab = 0;
            }
    }
}

/* scroll wheel: z clicks (positive = down) for the window under the pointer */
static void wls_wheel(int z) {
    struct wlsurf *t = wls_pfocus;
    if (!t || !t->c->pointers) return;
    struct wlc *c = t->c;
    wl_pointer_ev(c, 6, 1, 0u);                                         /* axis_source: wheel */
    wl_pointer_ev(c, 8, 2, 0u, (uint32_t)z);                            /* axis_discrete(vertical, clicks) */
    wl_pointer_ev(c, 4, 3, (uint32_t)now_ms(), 0u, (uint32_t)(z * 15 * 256));   /* axis(vertical, 15 px per click) */
    wl_pointer_ev(c, 5, 0);
    wl_flush(c);
}

static void wls_request_close(struct wlsurf *s) {
    struct wlc *c = s->c;
    if (s->close_sent) {                                  /* the client ignored the first request: hang up */
        if (c->fd >= 0) { close(c->fd); c->fd = -1; }
        return;
    }
    s->close_sent = 1;
    if (s->top_id) { wl_ev(c, s->top_id, 1, 0); wl_flush(c); }
}


/* ---------- clipboard (wl_data_device selection) ---------- */
struct wlsrc { struct wlc *c; uint32_t id; char *mimes[16]; int nm; };
static struct wlsrc *wls_sel;

/* an event that carries a file descriptor */
static void wl_emit_fd(struct wlc *c, uint32_t id, uint32_t op, const struct ob *o, int fd) {
    wl_flush(c);
    uint8_t buf[8 + 640];
    uint32_t h[2] = { id, (uint32_t)((8 + o->n) << 16) | op };
    memcpy(buf, h, 8); memcpy(buf + 8, o->b, o->n);
    struct iovec iv = { buf, 8 + o->n };
    union { char b[CMSG_SPACE(sizeof(int))]; struct cmsghdr al; } cu;
    memset(&cu, 0, sizeof cu);
    struct msghdr mh = { 0 };
    mh.msg_iov = &iv; mh.msg_iovlen = 1; mh.msg_control = cu.b; mh.msg_controllen = sizeof cu.b;
    struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
    cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_RIGHTS; cm->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cm), &fd, sizeof fd);
    (void)!sendmsg(c->fd, &mh, MSG_NOSIGNAL);
}

static void wls_send_selection(struct wlc *c) {
    if (!c->ddev_id) return;
    if (!wls_sel) { wl_ev(c, c->ddev_id, 5, 1, 0u); wl_flush(c); return; }
    if (c->soff >= 0xF00) return;                          /* (server-side ids are handed out in sequence) */
    uint32_t oid = 0xff000000u + c->soff++;
    if (!obj_new(c, oid, O_DOFFER, 3, wls_sel)) return;
    wl_ev(c, c->ddev_id, 0, 1, oid);                       /* data_offer(new object) */
    for (int i = 0; i < wls_sel->nm; i++) {
        struct ob e = { .n = 0 };
        ob_str(&e, wls_sel->mimes[i]);
        wl_emit(c, oid, 0, &e);                            /* offer(mime) */
    }
    wl_ev(c, c->ddev_id, 5, 1, oid);                       /* selection(offer) */
    wl_flush(c);
}
static void wls_selection_to_focus(void) { if (wls_kfocus) wls_send_selection(wls_kfocus->c); }

static void src_free(struct wlsrc *src) {
    if (wls_sel == src) { wls_sel = 0; wls_selection_to_focus(); }
    for (int i = 0; i < WLS_MAXCL; i++)                    /* offers made from it can no longer be received */
        if (wlcl[i].used) for (uint32_t k = 0; k < wlcl[i].nobjs; k++)
            if (wlcl[i].objs[k].type == O_DOFFER && wlcl[i].objs[k].p == src) wlcl[i].objs[k].p = 0;
    for (int i = 0; i < src->nm; i++) free(src->mimes[i]);
    free(src);
}

/* ---------- request dispatch ---------- */
static struct wlsurf *surf_of(struct wlc *c, uint32_t id) {
    struct wlobj *o = obj_get(c, id);
    return o && o->type == O_SURFACE ? o->p : 0;
}

static void pool_unref(struct wlpool *p) {
    if (--p->refs <= 0 && p->destroyed) { if (p->base) munmap(p->base, p->size); if (p->fd >= 0) close(p->fd); free(p); }
}

static void dispatch(struct wlc *c, uint32_t id, struct wlobj *o, uint32_t op, struct msg *m) {
    switch (o->type) {
    case O_DISPLAY:
        if (op == 0) {                                    /* sync(callback) */
            uint32_t cb = m_u32(m);
            if (m->bad || !obj_new(c, cb, O_CALLBACK, 1, 0)) return;
            wl_ev(c, cb, 0, 1, (uint32_t)now_ms());
            wl_delete_id(c, cb); obj_free(c, cb);
        } else if (op == 1) {                             /* get_registry(registry) */
            uint32_t r = m_u32(m);
            if (m->bad || !obj_new(c, r, O_REGISTRY, 1, 0)) return;
            for (int i = 0; i < WLS_NGLOBALS; i++) {
                struct ob e = { .n = 0 };
                ob_u32(&e, (uint32_t)i + 1); ob_str(&e, wls_globals[i].name); ob_u32(&e, wls_globals[i].ver);
                wl_emit(c, r, 0, &e);
            }
        }
        break;
    case O_REGISTRY:
        if (op == 0) registry_bind(c, m);
        break;
    case O_COMPOSITOR:
        if (op == 0) {                                    /* create_surface */
            uint32_t sid = m_u32(m);
            struct wlsurf *s = calloc(1, sizeof *s);
            if (m->bad || !s || !obj_new(c, sid, O_SURFACE, o->ver, s)) { free(s); return; }
            s->c = c; s->id = sid;s->input.infinite=1; s->next = c->surfs; c->surfs = s;
        } else if (op == 1) {                             /* create_region */
            uint32_t rid = m_u32(m);
            struct wlregion *r=calloc(1,sizeof *r);
            if(m->bad||!r||!obj_new(c,rid,O_REGION,1,r)){free(r);return;}
        }
        break;
    case O_REGION: {
        struct wlregion *r=o->p;
        if(op==0){region_clear(r);free(r);wl_delete_id(c,id);obj_free(c,id);}
        else if(op==1||op==2){
            int x=(int)m_u32(m),y=(int)m_u32(m),w=(int)m_u32(m),h=(int)m_u32(m);
            if(m->bad)return;
            if(w<0||h<0){wl_error(c,1,1,"negative region rectangle");break;}
            if(!w||!h)break;
            if(r->n>=4096){wl_error(c,1,2,"region resource limit");break;}
            struct wlregionop *next=realloc(r->ops,(r->n+1)*sizeof *next);
            if(!next){wl_error(c,1,2,"cannot allocate region");break;}
            r->ops=next;r->ops[r->n++]=(struct wlregionop){x,y,w,h,op==1};
        }
        break; }
    case O_SHM:
        if (op == 0) {                                    /* create_pool(pool, fd, size) */
            uint32_t pid = m_u32(m); int fd = m_fd(m); uint32_t size = m_u32(m);
            if (m->bad || size == 0 || size > (1u << 30)) { if (fd >= 0) close(fd); wl_error(c, id, 0, "bad pool"); return; }
            struct wlpool *p = calloc(1, sizeof *p);
            if (!p) { close(fd); return; }
            p->base = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
            if (p->base == MAP_FAILED) p->base = 0;
            p->size = size; p->fd = fd; p->refs = 1;
            if (!p->base) { close(fd); free(p); wl_error(c, id, 0, "cannot map pool"); return; }
            if (!obj_new(c, pid, O_POOL, 1, p)) { munmap(p->base, size); close(fd); free(p); return; }
        }
        break;
    case O_POOL: {
        struct wlpool *p = o->p;
        if (op == 0) {                                    /* create_buffer(buffer, offset, w, h, stride, format) */
            uint32_t bid = m_u32(m), off = m_u32(m), w = m_u32(m), h = m_u32(m), stride = m_u32(m), fmt = m_u32(m);
            if (m->bad) return;
            if (fmt != 0 && fmt != 1) { wl_error(c, id, 0, "unsupported format"); return; }
            struct wlbuf *b = calloc(1, sizeof *b);
            if (!b) return;
            *b = (struct wlbuf){ p, (int)off, (int)w, (int)h, (int)stride, fmt };
            if (!obj_new(c, bid, O_BUFFER, 1, b)) { free(b); return; }
            p->refs++;
        } else if (op == 1) {
            p->destroyed = 1; wl_delete_id(c, id); obj_free(c, id); pool_unref(p);
        } else if (op == 2) {                             /* resize(size) */
            uint32_t size = m_u32(m);
            if (m->bad || size < p->size || size > (1u << 30)) return;
            void *nb = mmap(0, size, PROT_READ, MAP_SHARED, p->fd, 0);
            if (nb == MAP_FAILED) return;
            if (p->base) munmap(p->base, p->size);
            p->base = nb; p->size = size;
        }
        break; }
    case O_BUFFER:
        if (op == 0) { struct wlbuf *b = o->p; pool_unref(b->pool); free(b); wl_delete_id(c, id); obj_free(c, id); }
        break;
    case O_SURFACE: {
        struct wlsurf *s = o->p;
        switch (op) {
        case 0: surf_free(s); wl_delete_id(c, id); obj_free(c, id); break;
        case 1: { uint32_t b = m_u32(m); m_u32(m); m_u32(m); s->pend_buf = b; s->buf_set = 1; break; }
        case 2: case 9: {                                 /* damage / damage_buffer: union rectangle */
            int x = (int)m_u32(m), y = (int)m_u32(m), w = (int)m_u32(m), h = (int)m_u32(m);
            if (m->bad || w <= 0 || h <= 0) break;
            if (!s->has_dmg) { s->dx0 = x; s->dy0 = y; s->dx1 = x + w; s->dy1 = y + h; s->has_dmg = 1; }
            else {
                if (x < s->dx0) s->dx0 = x;
                if (y < s->dy0) s->dy0 = y;
                if (x + w > s->dx1) s->dx1 = x + w;
                if (y + h > s->dy1) s->dy1 = y + h;
            }
            break; }
        case 3: {                                         /* frame(callback) */
            uint32_t cb = m_u32(m);
            if (m->bad || !obj_new(c, cb, O_CALLBACK, 1, 0)) break;
            if (s->ncb_pending < 8) s->cb_pending[s->ncb_pending++] = cb;
            break; }
        case 5: {
            uint32_t rid=m_u32(m);struct wlobj *ro=rid?obj_get(c,rid):0;
            if(m->bad)break;
            if(rid&&(!ro||ro->type!=O_REGION)){wl_error(c,1,0,"invalid input region");break;}
            if(!region_copy(&s->pending_input,ro?ro->p:0)){wl_error(c,1,2,"cannot copy input region");break;}
            s->input_pending=1;break;
        }
        case 6: surf_commit(c, s); break;
        default: break;                                   /* opaque region, transform, scale, offset: ignored */
        }
        break; }
    case O_SEAT:
        if (op == 0 || op == 1) {                         /* get_pointer / get_keyboard */
            uint32_t nid = m_u32(m), version = o->ver;
            if (m->bad) return;
            if (op == 0) {
                struct wlpointer *p = calloc(1, sizeof *p);
                if (!p) { wl_error(c, 1, 2, "cannot allocate pointer"); return; }
                if (!obj_new(c, nid, O_POINTER, version, p)) { free(p); return; }
                p->id = nid; p->ver = version; p->next = c->pointers; c->pointers = p;
                // A newly created resource needs its own initial focus, even
                // when the physical pointer has not moved since get_pointer.
                int64_t ox, oy;
                if (wls_pfocus && wls_pfocus->c == c && wls_surface_origin(wls_pfocus, &ox, &oy)) {
                    int x = wls_pointer_coord((int64_t)mx - ox), y = wls_pointer_coord((int64_t)my - oy);
                    wl_ev(c, nid, 0, 4, ++c->serial, wls_pfocus->id, (uint32_t)(x*256), (uint32_t)(y*256));
                    if (version >= 5) wl_ev(c, nid, 5, 0);
                    wl_flush(c);
                }
                if (getenv("WLS_INPUT_LOG")) fprintf(stderr, "wl-pointer-created id=%u version=%u\n", nid, version);
            } else {
                // obj_new can resize the object array: use the saved version,
                // never dereference the old object pointer after that call.
                struct wlkeyboard *k = calloc(1, sizeof *k);
                if (!k) { wl_error(c, 1, 2, "cannot allocate keyboard"); return; }
                if (!obj_new(c, nid, O_KEYBOARD, version, k)) { free(k); return; }
                k->id = nid; k->next = c->keyboards; c->keyboards = k;
                if (wls_keymap) {
                    /* keymap(format = xkb_v1, fd, size): the fd rides on this one message */
                    int mf = memfd_create("keymap", 0);
                    if (mf >= 0 && ftruncate(mf, (off_t)wls_keymap_len + 1) == 0) {
                        char *mm = mmap(0, wls_keymap_len + 1, PROT_READ | PROT_WRITE, MAP_SHARED, mf, 0);
                        if (mm != MAP_FAILED) {
                            memcpy(mm, wls_keymap, wls_keymap_len); mm[wls_keymap_len] = 0; munmap(mm, wls_keymap_len + 1);
                            wl_flush(c);
                            uint32_t msgb[4] = { nid, (uint32_t)(16 << 16) | 0, 1, (uint32_t)wls_keymap_len + 1 };
                            struct iovec iv = { msgb, 16 };
                            union { char b[CMSG_SPACE(sizeof(int))]; struct cmsghdr al; } cu;
                            memset(&cu, 0, sizeof cu);
                            struct msghdr mh = { 0 };
                            mh.msg_iov = &iv; mh.msg_iovlen = 1; mh.msg_control = cu.b; mh.msg_controllen = sizeof cu.b;
                            struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
                            cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_RIGHTS; cm->cmsg_len = CMSG_LEN(sizeof(int));
                            memcpy(CMSG_DATA(cm), &mf, sizeof mf);
                            (void)!sendmsg(c->fd, &mh, MSG_NOSIGNAL);
                        }
                    }
                    if (mf >= 0) close(mf);
                }                                         /* no keymap file: say nothing (a keymap event needs an fd) */
                if (version >= 4) wl_ev(c, nid, 5, 2, 25u, 600u);
                if (wls_kfocus && wls_kfocus->c == c) wls_keyboard_enter(c, nid, wls_kfocus);
            }
        } else if (op == 2) {
            uint32_t nid = m_u32(m);
            if (!m->bad) obj_new(c, nid, O_STUB, 1, 0);
        }
        break;
    case O_POINTER:
        if (op == 1) {
            struct wlpointer **link = &c->pointers;
            while (*link && (*link)->id != id) link = &(*link)->next;
            if (*link) { struct wlpointer *p = *link; *link = p->next; free(p); }
            wl_delete_id(c, id); obj_free(c, id);
        }
        break;
    case O_KEYBOARD:
        if (op == 0) {
            struct wlkeyboard **link = &c->keyboards;
            while (*link && (*link)->id != id) link = &(*link)->next;
            if (*link) { struct wlkeyboard *k = *link; *link = k->next; free(k); }
            wl_delete_id(c, id); obj_free(c, id);
        }
        break;
    case O_OUTPUT:
        if (op == 0) { wl_delete_id(c, id); obj_free(c, id); }
        break;
    case O_DDM:
        if (op == 0) {                                    /* create_data_source */
            uint32_t nid = m_u32(m);
            struct wlsrc *src = calloc(1, sizeof *src);
            if (m->bad || !src || !obj_new(c, nid, O_DSRC, o->ver, src)) { free(src); return; }
            src->c = c; src->id = nid;
        } else if (op == 1) {                             /* get_data_device */
            uint32_t nid = m_u32(m);
            if (!m->bad && obj_new(c, nid, O_DDEV, o->ver, 0)) { c->ddev_id = nid; if (wls_kfocus && wls_kfocus->c == c) wls_send_selection(c); }
        }
        break;
    case O_DSRC: {
        struct wlsrc *src = o->p;
        if (op == 0) {                                    /* offer(mime) */
            char t[128];
            if (m_str(m, t, sizeof t) >= 0 && src->nm < 16) src->mimes[src->nm++] = strdup(t);
        } else if (op == 1) { src_free(src); wl_delete_id(c, id); obj_free(c, id); }
        break; }
    case O_DDEV:
        if (op == 1) {                                    /* set_selection(source, serial) */
            uint32_t sid = m_u32(m);
            struct wlobj *so = obj_get(c, sid);
            struct wlsrc *src = so && so->type == O_DSRC ? so->p : 0;
            if (wls_sel && wls_sel != src) { wl_ev(wls_sel->c, wls_sel->id, 2, 0); wl_flush(wls_sel->c); }   /* cancelled */
            wls_sel = src;
            wls_selection_to_focus();
        } else if (op == 2) { c->ddev_id = 0; wl_delete_id(c, id); obj_free(c, id); }
        break;
    case O_DOFFER:
        if (op == 1) {                                    /* receive(mime, fd): ask the source to write into the pipe */
            char t[128];
            int ok = m_str(m, t, sizeof t) >= 0;
            int fd = m_fd(m);
            struct wlsrc *src = o->p;
            if (ok && src && fd >= 0) {
                struct ob e = { .n = 0 };
                ob_str(&e, t);
                wl_emit_fd(src->c, src->id, 1, &e, fd);   /* data_source.send(mime, fd) */
            }
            if (fd >= 0) close(fd);
        } else if (op == 2) obj_free(c, id);
        break;
    case O_SUBCOMP:
        if (op == 0) { wl_delete_id(c, id); obj_free(c, id); }
        else if (op == 1) {
            uint32_t nid = m_u32(m), sid = m_u32(m), pid = m_u32(m);
            struct wlsurf *s = surf_of(c, sid), *p = surf_of(c, pid);
            if (m->bad || !s || s->sub_id || s->xdg_id || s->role_top || s->is_popup) { wl_error(c, id, 0, "invalid subsurface role"); break; }
            int depth = 0, bad = !p;
            for (struct wlsurf *a = p; a; a = a->is_sub ? a->parent : 0)
                if (a == s || ++depth > 32) { bad = 1; break; }
            if (bad) { wl_error(c, id, 1, "invalid subsurface parent"); break; }
            if (!obj_new(c, nid, O_SUBSURF, 1, s)) break;
            int z = 0;
            for (struct wlsurf *a = c->surfs; a; a = a->next)
                if (a->is_sub && a->parent == p && a->sub_pz > z) z = a->sub_pz;
            s->is_sub = 1; s->sub_id = nid; s->parent = p; s->sub_sync = 1;
            s->sub_added = 0; s->sub_x = s->sub_y = s->sub_px = s->sub_py = 0;
            s->sub_z = s->sub_pz = z + 1;
        }
        break;
    case O_SUBSURF: {
        struct wlsurf *s = o->p;
        if (!s) { if (op == 0) { wl_delete_id(c, id); obj_free(c, id); } break; }
        if (op == 0) {
            struct wlsurf *p = s->parent;
            s->parent = 0; s->sub_id = 0; s->sub_added = 0;
            free(s->pixels); s->pixels = 0; free(s->cached_pixels); s->cached_pixels = 0; s->cached_set = 0;
            wl_delete_id(c, id); obj_free(c, id); if (p) subs_render(p);
        } else if (op == 1) { s->sub_px = (int)m_u32(m); s->sub_py = (int)m_u32(m); }
        else if (op == 2 || op == 3) {
            struct wlsurf *ref = surf_of(c, m_u32(m));
            if (!ref || ref == s || !s->parent || (ref != s->parent && (!ref->is_sub || ref->parent != s->parent))) {
                wl_error(c, id, 0, "invalid subsurface sibling"); break;
            }
            int z = ref == s->parent ? 0 : ref->sub_pz;
            if (op == 2) {
                for (struct wlsurf *a = c->surfs; a; a = a->next)
                    if (a != s && a->is_sub && a->parent == s->parent && a->sub_pz > z) a->sub_pz++;
                s->sub_pz = z + 1;
            } else {
                for (struct wlsurf *a = c->surfs; a; a = a->next)
                    if (a != s && a->is_sub && a->parent == s->parent && a->sub_pz < z) a->sub_pz--;
                s->sub_pz = z - 1;
            }
        } else if (op == 4) s->sub_sync = 1;
        else if (op == 5) {
            s->sub_sync = 0;
            if (!subs_synchronized(s)) { int changed = subs_apply_cached(s); changed |= subs_apply_children(s, 0); if (changed) subs_render(s); }
        }
        }
        break;
    case O_XDGWM:
        if (op == 1) {                                    /* create_positioner */
            uint32_t nid = m_u32(m);
            struct wlpos *ps = calloc(1, sizeof *ps);
            if (m->bad || !ps || !obj_new(c, nid, O_POSITIONER, o->ver, ps)) free(ps);
        } else if (op == 2) {                             /* get_xdg_surface(id, surface) */
            uint32_t nid = m_u32(m), sid = m_u32(m);
            struct wlsurf *s = surf_of(c, sid);
            if (m->bad || !s || s->is_sub) { wl_error(c, id, 1, "bad surface"); return; }
            if (obj_new(c, nid, O_XDGSURF, o->ver, s)) s->xdg_id = nid;
        } else if (op == 0) { wl_delete_id(c, id); obj_free(c, id); }
        break;
    case O_POSITIONER: {
        struct wlpos *ps = o->p;
        switch (op) {
        case 0: free(ps); wl_delete_id(c, id); obj_free(c, id); break;
        case 1: ps->w = (int)m_u32(m); ps->h = (int)m_u32(m); break;
        case 2: ps->ax = (int)m_u32(m); ps->ay = (int)m_u32(m); ps->aw = (int)m_u32(m); ps->ah = (int)m_u32(m); break;
        case 3: ps->anchor = (int)m_u32(m); break;
        case 4: ps->gravity = (int)m_u32(m); break;
        case 6: ps->ox = (int)m_u32(m); ps->oy = (int)m_u32(m); break;
        default: break;
        }
        break; }
    case O_POPUP: {
        struct wlsurf *s = o->p;
        if (op == 1) s->popup_grab = 1;                    /* grab(seat, serial) */
        else if (op == 0) { surf_unmap(s); s->is_popup = 0; wl_delete_id(c, id); obj_free(c, id); }
        break; }
    case O_XDGSURF: {
        struct wlsurf *s = o->p;
        if (op == 1) {                                    /* get_toplevel */
            uint32_t nid = m_u32(m);
            if (m->bad || !obj_new(c, nid, O_TOPLEVEL, o->ver, s)) return;
            s->top_id = nid; s->role_top = 1;
            struct ob e = { .n = 0 };
            ob_u32(&e, 0); ob_u32(&e, 0); ob_u32(&e, 0);     /* configure(0, 0, empty states): the client picks its size */
            wl_emit(c, nid, 0, &e);
            wl_ev(c, id, 0, 1, ++c->serial);                 /* xdg_surface.configure(serial) */
        } else if (op == 2) {                             /* get_popup(id, parent xdg_surface, positioner) */
            uint32_t nid = m_u32(m), par = m_u32(m), pos = m_u32(m);
            struct wlobj *po = obj_get(c, pos), *pa = obj_get(c, par);
            if (m->bad || !po || po->type != O_POSITIONER || !obj_new(c, nid, O_POPUP, o->ver, s)) return;
            struct wlpos *ps = po->p;
            s->is_popup = 1; s->popup_obj = nid;
            s->parent = pa && pa->type == O_XDGSURF ? pa->p : 0;
            /* anchor point on the rectangle, then the popup extends from it in the gravity direction */
            int a = ps->anchor, g = ps->gravity;
            int ax = ps->ax + ((a == 4 || a == 7 || a == 8) ? ps->aw : (a == 3 || a == 5 || a == 6) ? 0 : ps->aw / 2);
            int ay = ps->ay + ((a == 2 || a == 6 || a == 8) ? ps->ah : (a == 1 || a == 5 || a == 7) ? 0 : ps->ah / 2);
            int px = (g == 4 || g == 7 || g == 8) ? ax : (g == 3 || g == 5 || g == 6) ? ax - ps->w : ax - ps->w / 2;
            int py = (g == 2 || g == 6 || g == 8) ? ay : (g == 1 || g == 5 || g == 7) ? ay - ps->h : ay - ps->h / 2;
            s->popup_x = px + ps->ox; s->popup_y = py + ps->oy;
            wl_ev(c, nid, 0, 4, (uint32_t)s->popup_x, (uint32_t)s->popup_y, (uint32_t)ps->w, (uint32_t)ps->h);   /* xdg_popup.configure */
            wl_ev(c, id, 0, 1, ++c->serial);               /* xdg_surface.configure */
        } else if (op == 3) {                             /* set_window_geometry */
            s->gx = (int)m_u32(m); s->gy = (int)m_u32(m); s->gw = (int)m_u32(m); s->gh = (int)m_u32(m);
            s->has_geo = !m->bad && s->gw > 0 && s->gh > 0;
        } else if (op == 4) {                             /* ack_configure */
            s->configured = 1;
        } else if (op == 0) {
            s->xdg_id = 0; wl_delete_id(c, id); obj_free(c, id);
        }
        break; }
    case O_TOPLEVEL: {
        struct wlsurf *s = o->p;
        if (op == 2) {                                    /* set_title */
            char t[64];
            if (m_str(m, t, sizeof t) < 0) break;
            memset(s->title, 0, sizeof s->title); memcpy(s->title, t, 16);
            struct win *w = s->winid ? win_by_id(s->winid) : 0;
            if (w) { memcpy(w->title, s->title, sizeof w->title); compose(frame_of(w)); winlist_dirty = 1; }
        } else if (op == 5) {                             /* move(seat, serial) */
            if (s->winid) desk_begin_move(s->winid);
        } else if (op == 6) {                             /* resize(seat, serial, edges) */
            m_u32(m); m_u32(m); uint32_t edges = m_u32(m);
            if (s->winid) desk_begin_resize(s->winid, (int)edges);
        } else if (op == 9 || op == 10) {                 /* set_maximized / unset_maximized */
            if (s->winid) desk_set_maximized(s->winid, op == 9);
        } else if (op == 11 || op == 12) {                /* set_fullscreen(output) / unset_fullscreen: the whole screen, as wl_output reports it */
            if (s->winid) desk_set_fullscreen(s->winid, op == 11);
        } else if (op == 0) {
            surf_unmap(s); s->top_id = 0; s->role_top = 0; wl_delete_id(c, id); obj_free(c, id);
        }
        break; }
    default: break;                                       /* O_STUB, O_CALLBACK: nothing to do */
    }
}

static void client_close(struct wlc *c) {
    struct wlsurf *s;
    while ((s = c->surfs)) {                              /* surf_free unlinks the head */
        surf_free(s);
    }
    for (uint32_t i = 0; i < c->nobjs; i++) {
        if (c->objs[i].type == O_POINTER || c->objs[i].type == O_KEYBOARD) free(c->objs[i].p);
        else if (c->objs[i].type == O_DSRC) src_free(c->objs[i].p);
        else if (c->objs[i].type == O_BUFFER) { struct wlbuf *b = c->objs[i].p; pool_unref(b->pool); free(b); }
        else if (c->objs[i].type == O_POOL) { struct wlpool *p = c->objs[i].p; p->destroyed = 1; pool_unref(p); }
        else if (c->objs[i].type == O_REGION) {struct wlregion *r=c->objs[i].p;region_clear(r);free(r);}
    }
    for (int i = 0; i < c->ninfd; i++) close(c->infd[i]);
    free(c->objs); free(c->out);
    if (c->fd >= 0) close(c->fd);
    memset(c, 0, sizeof *c);
    c->fd = -1;
}

static void client_read(struct wlc *c) {
    struct iovec iv = { c->in + c->inlen, WLS_INCAP - c->inlen };
    union { char b[CMSG_SPACE(28 * sizeof(int))]; struct cmsghdr al; } cu;
    struct msghdr mh = { 0 };
    mh.msg_iov = &iv; mh.msg_iovlen = 1; mh.msg_control = cu.b; mh.msg_controllen = sizeof cu.b;
    ssize_t n = recvmsg(c->fd, &mh, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) { client_close(c); return; }
    if (n < 0) return;
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm))
        if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS) {
            int cnt = (int)((cm->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            for (int i = 0; i < cnt; i++) {
                int fd; memcpy(&fd, CMSG_DATA(cm) + i * sizeof(int), sizeof fd);
                if (c->ninfd < 32) c->infd[c->ninfd++] = fd; else close(fd);
            }
        }
    c->inlen += (size_t)n;
    size_t off = 0;
    while (c->fd >= 0 && c->inlen - off >= 8) {
        uint32_t id, w1;
        memcpy(&id, c->in + off, 4); memcpy(&w1, c->in + off + 4, 4);
        size_t size = w1 >> 16; uint32_t op = w1 & 0xFFFF;
        if (size < 8 || (size & 3) || size > WLS_INCAP) { wl_error(c, 1, 0, "bad message size"); break; }
        if (c->inlen - off < size) break;
        struct msg m = { c->in + off + 8, size - 8, 0, 0, c };
        struct wlobj *o = id == 1 ? &(struct wlobj){ O_DISPLAY, 1, 0 } : obj_get(c, id);
        if (!o) { wl_error(c, 1, 0, "request on a dead object"); break; }
        dispatch(c, id, o, op, &m);
        off += size;
        if (m.bad && c->fd >= 0) { wl_error(c, id, 0, "malformed request"); break; }
    }
    if (c->fd < 0) { client_close(c); return; }
    memmove(c->in, c->in + off, c->inlen - off); c->inlen -= off;
    wl_flush(c);
}

static void wls_accept(void) {
    int fd = accept(wls_listen_fd, 0, 0);
    if (fd < 0) return;
    for (int i = 0; i < WLS_MAXCL; i++)
        if (!wlcl[i].used) {
            memset(&wlcl[i], 0, sizeof wlcl[i]);
            wlcl[i].used = 1; wlcl[i].fd = fd;
            return;
        }
    close(fd);
}

static int wls_init(void) {
    FILE *f = fopen(getenv("WLS_KEYMAP") ? getenv("WLS_KEYMAP") : "/de.xkb", "rb");
    if (f) {
        wls_keymap = malloc(200000);
        wls_keymap_len = wls_keymap ? fread(wls_keymap, 1, 199999, f) : 0;
        fclose(f);
        if (!wls_keymap_len) { free(wls_keymap); wls_keymap = 0; }
    }
    wls_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    strcpy(a.sun_path, WLS_PATH);
    if (wls_listen_fd < 0 || bind(wls_listen_fd, (void *)&a, sizeof a) != 0 || listen(wls_listen_fd, 8) != 0) {
        puts("desk: wayland socket unavailable"); wls_listen_fd = -1; return -1;
    }
    return 0;
}

/* poll plumbing: the compositor's loop adds our descriptors after its own */
static int wls_pollfds(struct pollfd *p) {
    int n = 0;
    if (wls_listen_fd >= 0) p[n++] = (struct pollfd){ wls_listen_fd, POLLIN, 0 };
    for (int i = 0; i < WLS_MAXCL; i++)
        if (wlcl[i].used) p[n++] = (struct pollfd){ wlcl[i].fd, (short)(POLLIN | (wlcl[i].outlen ? POLLOUT : 0)), 0 };
    return n;
}
static void wls_dispatch_poll(struct pollfd *p, int n) {
    int k = 0;
    if (wls_listen_fd >= 0) { if (p[k].revents & POLLIN) wls_accept(); k++; }
    for (int i = 0; i < WLS_MAXCL && k < n; i++) {
        if (!wlcl[i].used) continue;
        struct pollfd *q = &p[k++];
        if (q->fd != wlcl[i].fd) continue;
        if (q->revents & POLLOUT) wl_flush(&wlcl[i]);
        if (q->revents & (POLLIN | POLLHUP | POLLERR)) client_read(&wlcl[i]);
        if (wlcl[i].used && wlcl[i].fd < 0) client_close(&wlcl[i]);
    }
}
static int wls_have_callbacks(void) {
    for (int i = 0; i < WLS_MAXCL; i++)
        if (wlcl[i].used) for (struct wlsurf *s = wlcl[i].surfs; s; s = s->next) if (s->ncb_ready) return 1;
    return 0;
}
/* About 60 times a second: frame-throttling hints, not presentation feedback. */
static void wls_tick(void) {
    unsigned long long t = now_ms();
    if (t - wls_last_tick < 16) return;
    wls_last_tick = t;
    for (int i = 0; i < WLS_MAXCL; i++) {
        if (!wlcl[i].used) continue;
        struct wlc *c = &wlcl[i];
        for (struct wlsurf *s = c->surfs; s; s = s->next) {
            for (int k = 0; k < s->ncb_ready; k++) {
                wl_ev(c, s->cb_ready[k], 0, 1, (uint32_t)t);
                wl_delete_id(c, s->cb_ready[k]);
                obj_free(c, s->cb_ready[k]);
            }
            s->ncb_ready = 0;
        }
        wl_flush(c);
    }
}
