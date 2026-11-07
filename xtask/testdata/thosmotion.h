// SPDX-License-Identifier: GPL-2.0-or-later
// THOS motion system: every animation on the desktop is built from these few pieces so that timing feels like one design.
//
//   Springs   position/velocity integrated with a fixed sub-step, used for anything that should settle physically
//             (the dock stretching when a window opens, a hovered launcher lifting, windows popping open).
//   Tweens    a value over a fixed time with an easing curve, used for fades and colour changes.
//
// The numbers below are the motion tokens (see docs/thos/design-system.md): change them here and the whole desktop follows.
#ifndef THOSMOTION_H
#define THOSMOTION_H
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>

/* ---- time ---- */
/* THOS_SLOMO=n slows the animation clock down n times: the test harness films animations by taking screenshots in slow motion */
static inline int mo_slomo(void) { static int n; if (!n) { const char *e = getenv("THOS_SLOMO"); n = e && atoi(e) > 1 ? atoi(e) : 1; } return n; }
static inline long long mo_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return ((long long)t.tv_sec * 1000 + t.tv_nsec / 1000000) / mo_slomo();
}

/* ---- motion tokens ---- */
#define MO_FAST 120          /* hover, press, focus: the interface answers at once */
#define MO_BASE 200          /* fades and colour changes */
#define MO_SLOW 320          /* panels sliding in */
#define MO_FRAME_MS 16       /* the animation clock (60 Hz) */

/* spring presets: stiffness k, damping c, mass 1. damping ratio = c / (2 sqrt k):
   SNAPPY 0.72 (almost no overshoot), SOFT 0.85 (a gentle settle), DOCK 0.50 (visibly springs back), POP 0.62 (windows) */
#define SP_SNAPPY 520.0f, 33.0f
#define SP_SOFT 260.0f, 27.0f
#define SP_DOCK 340.0f, 18.5f
#define SP_POP 430.0f, 26.0f
#define SP_HOVER 700.0f, 38.0f

/* ---- springs ---- */
struct spring { float p, v, t, k, c; };
static inline void spring_init(struct spring *s, float p, float k, float c) { s->p = s->t = p; s->v = 0; s->k = k; s->c = c; }
static inline void spring_to(struct spring *s, float target) { s->t = target; }
static inline int spring_busy(const struct spring *s) { return fabsf(s->t - s->p) > 0.004f || fabsf(s->v) > 0.02f; }
static inline void spring_step(struct spring *s, float dt) {         /* dt in seconds, integrated in 4 ms slices (semi-implicit Euler) */
    if (dt > 0.1f) dt = 0.1f;
    while (dt > 0) {
        float h = dt > 0.004f ? 0.004f : dt;
        s->v += (s->k * (s->t - s->p) - s->c * s->v) * h;
        s->p += s->v * h;
        dt -= h;
    }
    if (!spring_busy(s)) { s->p = s->t; s->v = 0; }
}

/* ---- easing curves, t in 0..1 ---- */
static inline float ease_out(float t) { t = 1 - t; return 1 - t * t * t; }                    /* cubic out: things arriving */
static inline float ease_in(float t) { return t * t * t; }                                     /* cubic in: things leaving */
static inline float ease_in_out(float t) { return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }
static inline float ease_out_back(float t) { const float c1 = 1.70158f, c3 = c1 + 1; t -= 1; return 1 + c3 * t * t * t + c1 * t * t; }   /* a small overshoot */

/* ---- tweens ---- */
struct tween { long long t0; int dur; float from, to; };
static inline void tween_start(struct tween *w, float from, float to, int dur) { w->t0 = mo_now_ms(); w->from = from; w->to = to; w->dur = dur; }
static inline int tween_busy(const struct tween *w) { return w->dur > 0 && mo_now_ms() - w->t0 < w->dur; }
static inline float tween_value(const struct tween *w, float (*ease)(float)) {
    if (w->dur <= 0) return w->to;
    float t = (float)(mo_now_ms() - w->t0) / (float)w->dur;
    if (t >= 1) return w->to;
    if (t < 0) t = 0;
    return w->from + (w->to - w->from) * ease(t);
}

/* ---- frame-time probes: `touch /tmp/thos-perf` before starting the desktop and every probe prints "perf name: n= avg= max=" every 2 s ---- */
struct mo_perf { const char *name; long long t0, tot, max, n, shown; };
static inline long long mo_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
static inline int mo_perf_on(void) { static int on = -1; if (on < 0) on = access("/tmp/thos-perf", 0) == 0; return on; }
static inline void mo_perf_begin(struct mo_perf *p) { if (mo_perf_on()) p->t0 = mo_us(); }
static inline void mo_perf_end(struct mo_perf *p) {
    if (!mo_perf_on()) return;
    long long now = mo_us(), d = now - p->t0;
    p->tot += d; p->n++; if (d > p->max) p->max = d;
    if (p->shown == 0) p->shown = now;
    if (now - p->shown > 2000000) { printf("perf %s: n=%lld avg=%lldus max=%lldus\n", p->name, p->n, p->n ? p->tot / p->n : 0, p->max); fflush(stdout); p->tot = p->max = p->n = 0; p->shown = now; }
}
#endif
