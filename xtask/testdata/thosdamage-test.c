// SPDX-License-Identifier: GPL-2.0-or-later
// Host regression: batched damage must produce the same screen as a full repaint.
#define main thosdesk_main
#include "thosdesk.c"
#undef main
#include <assert.h>
static void check_queue(void) {
    struct damage_queue q = { 0 };
    damage_add(&q, (struct rect){ 0, 0, 100, 100 });
    for (int i = 0; i < 100; i++) damage_add(&q, (struct rect){ 20, 20, 30, 30 });
    assert(q.n == 1 && damage_area(q.r[0]) == 10000);
    q.n = 0;
    for (int i = 0; i < 100; i++) damage_add(&q, (struct rect){ i * 10, i * 10, i * 10 + 2, i * 10 + 2 });
    assert(q.n <= DAMAGE_MAX);
    for (int i = 0; i < 100; i++) {
        int covered = 0;
        for (int j = 0; j < q.n; j++)
            if (q.r[j].x0 <= i * 10 && q.r[j].y0 <= i * 10 && q.r[j].x1 >= i * 10 + 2 && q.r[j].y1 >= i * 10 + 2) covered = 1;
        assert(covered);
    }
}
static void reference_frame(void) {
    struct rect r = {0, 0, W, H};
    paint_scene_full(r, nwin - 1);
    draw_ghosts(r); draw_toasts(r); draw_switcher(r); draw_ctx(r);
    restore_backbuffer(r);
    int keep = compose_deferred; compose_deferred = 0; draw_cursor(); compose_deferred = keep;
}
static void check_equal(uint32_t *captured, size_t bytes, const char *name, int test) {
    memcpy(captured, fbmap, bytes);
    reference_frame();
    if (memcmp(captured, fbmap, bytes)) {
        for (int i = 0; i < W * H; i++) if (captured[i] != ((uint32_t *)fbmap)[i]) {
            fprintf(stderr, "%s differs at case %d, (%d,%d): %08x vs %08x\n", name, test, i % W, i / W, captured[i], ((uint32_t *)fbmap)[i]); break;
        }
        exit(1);
    }
}
int main(void) {
    check_queue();
    W = 320; H = 240; pitch = W * 4; rs = 16; gs = 8; bs = 0;
    size_t bytes = (size_t)W * H * 4;
    bb = calloc(W * H, 4); wall = calloc(W * H, 4); fbmap = calloc(W * H, 4);
    uint32_t *captured = malloc(bytes);
    assert(bb && wall && fbmap && captured);
    for (int i = 0; i < W * H; i++) wall[i] = (i * 313u) & 0xffffff;
    shapes_init(); cursor_init();
    nwin = 2;
    for (int i = 0; i < nwin; i++) {
        wins[i] = (struct win){ .x = 35 + i * 55, .y = 60 + i * 30, .w = 150, .h = 110, .deco = 1 };
        wins[i].px = calloc(150 * 110, 4);
        assert(wins[i].px);
        for (int p = 0; p < 150 * 110; p++) wins[i].px[p] = i ? 0x80502818 : 0x223344;
        wins[i].alpha = i;
        spring_init(&wins[i].foc, i, SP_SNAPPY);
    }
    mx = 40; my = 40;
    for (int test = 0; test < 80; test++) {
        compose_deferred = 0;
        compose((struct rect){ 0, 0, W, H });
        compose_deferred = 1;
        for (int i = 0; i < nwin; i++) compose(outer_of(&wins[i]));
        struct rect old_cursor = cur_rect(mx, my, cur_kind);
        wins[0].x = 8 + (test * 17) % 135; wins[0].y = 45 + (test * 11) % 75;
        wins[1].foc.p = (test % 10) / 9.0f;
        wins[1].minimized = test % 7 == 0;
        /* Snapshot construction borrows bb as scratch; batching must repair it. */
        snap_build(&wins[0]);
        if (test % 3 == 0) {
            wins[0].anim = 2; wins[0].sc.p = 0.85f; wins[0].ax = 0.5f; wins[0].ay = 0.5f;
            wins[0].opq = (struct tween){ .to = 0.7f };
        } else wins[0].anim = 0;
        compose(outer_of(&wins[0]));
        compose(outer_of(&wins[1]));
        compose(inflate_r(wins[0].snap_r, 14));
        mx = 15 + (test * 7) % 275; my = 15 + (test * 13) % 200;
        cur_kind = test % 3;
        compose(old_cursor); compose(cur_rect(mx, my, cur_kind));
        compose_flush();
        check_equal(captured, bytes, "Damage/occlusion", test);
    }
    wins[0].anim = wins[1].anim = 0;
    wins[0].minimized = wins[1].minimized = 0;
    int copied = 0, fallback = 0;
    compose_deferred = 0; compose((struct rect){0, 0, W, H}); compose_deferred = 1;
    for (int test = 0; test < 240; test++) {
        struct win *w = &wins[1];
        struct rect old = outer_of(w);
        int ox = w->x, oy = w->y;
        /* Concurrent client content changes must be applied before moving. */
        for (int p = 0; p < w->w * w->h; p++) w->px[p] = (p * 137u + test * 537u) & 0xffffff;
        w->alpha = test % 11 == 0;
        if (w->alpha) for (int p = 0; p < w->w * w->h; p++) w->px[p] = 0x80402010;
        w->anim = 0;
        if (test % 19 == 0) {
            snap_build(w); w->anim = 2; w->sc.p = 1.04f; w->ax = w->ay = 0.5f; w->opq = (struct tween){.to = 0.7f};
        }
        compose(inflate_r(old, 14));
        struct rect oldc = cur_rect(mx, my, cur_kind);
        mx = 2 + test * 7 % (W - 40); my = 3 + test * 9 % (H - 40); cur_kind = test % 3;
        restore_cursor(oldc);
        int nx = -20 + test * 23 % 190, ny = 40 + test * 13 % 120;
        /* Overlays force the correct full renderer; ghost alpha is separately covered above. */
        ctx_active = test % 17 == 0; ctx_x = 20; ctx_y = 30;
        ctx_pop = (struct spring){.p = 1}; ctx_ha = (struct spring){.p = 1};
        if (ctx_active) compose((struct rect){0, 0, W, H});
        struct rect oldsnap = w->snap_r;
        if (move_window(w, nx, ny)) copied++; else fallback++;
        if (w->anim == 2) {
            assert(w->snap_r.x0 == oldsnap.x0 + nx - ox && w->snap_r.y0 == oldsnap.y0 + ny - oy);
        }
        if (test % 9 == 0) {
            /* Snapshot scratch after the copy must invalidate cached pixels too. */
            snap_build(w);
        }
        if (test % 13 == 0 && w->snap) {
            ghosts[0] = (struct ghost){.active = 1, .snap = w->snap, .r = w->snap_r, .ax = 0.5f, .ay = 0.5f, .sc = {.p = 0.9f}, .opq = {.to = 0.6f}};
            compose((struct rect){0, 0, W, H});
        }
        compose_flush();
        check_equal(captured, bytes, "Move/cursor", test);
        if (ghosts[0].active) { ghosts[0].active = 0; compose((struct rect){0, 0, W, H}); compose_flush(); }

        if (ctx_active) { ctx_active = 0; compose((struct rect){0, 0, W, H}); compose_flush(); }
    }
    assert(copied > 20 && fallback > 20);
    anim_last = 100; wls_last_tick = 103;
    assert(frame_wait_ms(108, 1, 0) == MO_FRAME_MS - 8);
    assert(frame_wait_ms(140, 1, 1) == 0);
    assert(frame_wait_ms(108, 0, 1) == 11);
    assert(frame_wait_ms(108, 0, 0) == 100);
    printf("PASS: %d cached moves, %d fallbacks, 320 full-screen comparisons against original scene renderer; alpha, shadows, focus, snapshots, clipping, commits, cursor and deadlines\n", copied, fallback);

    free(bb); free(wall); free(fbmap); free(captured);
    W = 1280; H = 800; pitch = W * 4; bytes = (size_t)W * H * 4;
    bb = calloc(W * H, 4); wall = calloc(W * H, 4); fbmap = calloc(W * H, 4); captured = malloc(bytes);
    assert(bb && wall && fbmap && captured);
    for (int p = 0; p < W * H; p++) wall[p] = (p * 313u) & 0xffffff;
    for (int i = 0; i < nwin; i++) {
        free(wins[i].px); free(wins[i].snap);
        wins[i] = (struct win){.x = 90 + i * 100, .y = 90 + i * 40, .w = 780, .h = 500, .deco = 1};
        wins[i].px = calloc(780 * 500, 4); assert(wins[i].px);
        for (int p = 0; p < 780 * 500; p++) wins[i].px[p] = (p * 137u + i * 537u) & 0xffffff;
        spring_init(&wins[i].foc, i, SP_SNAPPY);
    }
    struct rect region = clip(inflate_r(outer_of(&wins[1]), 8));
    long long t0 = mo_us();
    for (int k = 0; k < 100; k++) paint_scene_full(region, nwin - 1);
    long long full_us = mo_us() - t0;
    memcpy(captured, bb, bytes);
    t0 = mo_us();
    for (int k = 0; k < 100; k++) { int budget = 128; paint_scene(region, nwin - 1, &budget); }
    long long visible_us = mo_us() - t0;
    assert(memcmp(captured, bb, bytes) == 0);
    printf("Scene benchmark: 100 repaints, full=%lld us visible=%lld us (CPU scene work only)\n", full_us, visible_us);
    for (int i = 0; i < nwin; i++) { free(wins[i].px); free(wins[i].snap); }
    free(bb); free(wall); free(fbmap); free(captured);
    return 0;
}
