// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded screen damage; include after declaring struct rect { x0, y0, x1, y1; }.
#pragma once
#define DAMAGE_MAX 32
struct damage_queue { struct rect r[DAMAGE_MAX]; int n; };
static long long damage_area(struct rect r) {
    return (long long)(r.x1 - r.x0) * (r.y1 - r.y0);
}
static struct rect damage_union(struct rect a, struct rect b) {
    return (struct rect){ a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0,
                         a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1 };
}
static void damage_add(struct damage_queue *q, struct rect r) {
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    /* Merge only when the bounding box costs no more pixels than drawing both.
       Revisit earlier entries after merging so contained damage is discarded. */
    for (int i = 0; i < q->n;) {
        struct rect u = damage_union(r, q->r[i]);
        if (damage_area(u) <= damage_area(r) + damage_area(q->r[i])) {
            r = u; q->r[i] = q->r[--q->n]; i = 0;
        } else i++;
    }
    if (q->n == DAMAGE_MAX) {
        for (int i = 0; i < q->n; i++) r = damage_union(r, q->r[i]);
        q->n = 0; // bounded fallback: preserve every dirty pixel
    }
    q->r[q->n++] = r;
}
