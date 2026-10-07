// SPDX-License-Identifier: GPL-2.0-or-later
// THOS text editor (a legacy-protocol desktop client): thosedit [file]. Plain text (ASCII), Hack font, line numbers, a status bar.
//   arrows/Home/End/PgUp/PgDn move, Ctrl+Home/End jump, Enter/Tab/Backspace/Delete edit, Ctrl+S saves (a notification confirms),
//   the mouse wheel scrolls smoothly, a click places the caret. The caret blinks while the window has the keyboard.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include "thoswl.h"
#include "thosgfx.h"
#include "thosui.h"
#include "thosnotify.h"

#define EW 760
#define EH 520
#define HEAD 44
#define STATUS 28
#define GUT 56
#define LH 20
#define FS 14
#define MAXLINES 200000
static uint32_t *px;
static int sock;
static uint32_t **ln;                /* the lines as code points, without newlines (German umlauts and the rest of Unicode just work) */
static int *ll, *lc;                 /* lengths and capacities */
static int nl;
static int cy, cx, want;             /* caret line, column, remembered column for up/down */
static int modified;
static char path[512], msg[160];
static struct spring scroll;         /* top line (fractional) */
static float top_t;
static int cw = 8, blink_on = 1;
static long long blink_t0;
#define VIS ((EH - HEAD - STATUS) / LH)

static void line_ins(int at) {
    if (nl >= MAXLINES) return;
    ln = realloc(ln, sizeof *ln * (size_t)(nl + 1)); ll = realloc(ll, sizeof *ll * (size_t)(nl + 1)); lc = realloc(lc, sizeof *lc * (size_t)(nl + 1));
    memmove(&ln[at + 1], &ln[at], sizeof *ln * (size_t)(nl - at)); memmove(&ll[at + 1], &ll[at], sizeof *ll * (size_t)(nl - at)); memmove(&lc[at + 1], &lc[at], sizeof *lc * (size_t)(nl - at));
    lc[at] = 32; ll[at] = 0; ln[at] = malloc(32 * sizeof(uint32_t));
    nl++;
}
static void line_del(int at) {
    free(ln[at]);
    memmove(&ln[at], &ln[at + 1], sizeof *ln * (size_t)(nl - at - 1)); memmove(&ll[at], &ll[at + 1], sizeof *ll * (size_t)(nl - at - 1)); memmove(&lc[at], &lc[at + 1], sizeof *lc * (size_t)(nl - at - 1));
    nl--;
}
static void line_put(int at, int pos, const uint32_t *s, int n) {    /* insert n code points at pos in line at */
    if (ll[at] + n + 1 > lc[at]) { lc[at] = (ll[at] + n + 1) * 2; ln[at] = realloc(ln[at], (size_t)lc[at] * sizeof(uint32_t)); }
    memmove(ln[at] + pos + n, ln[at] + pos, (size_t)(ll[at] - pos) * sizeof(uint32_t));
    memcpy(ln[at] + pos, s, (size_t)n * sizeof(uint32_t));
    ll[at] += n;
}
static void put_cp(int at, uint32_t cp) { line_put(at, ll[at], &cp, 1); }
static void load(const char *p) {
    line_ins(0);
    FILE *f = fopen(p, "r");
    if (!f) { snprintf(msg, sizeof msg, "New file"); return; }
    char buf[4096];
    int cur = 0;
    size_t n, total = 0;
    uint32_t cp = 0; int need = 0;                                     /* UTF-8 decoder state */
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && total < (4u << 20)) {
        total += n;
        for (size_t i = 0; i < n; i++) {
            unsigned char b = (unsigned char)buf[i];
            if (b == '\n') { line_ins(cur + 1); cur++; need = 0; }
            else if (b == '\r') continue;
            else if (b == '\t') put_cp(cur, ' ');
            else if (need) { if ((b & 0xC0) == 0x80) { cp = (cp << 6) | (b & 0x3F); if (--need == 0) put_cp(cur, cp); } else { need = 0; put_cp(cur, 0xFFFD); } }
            else if (b < 0x80) put_cp(cur, b);
            else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; need = 1; }
            else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; need = 2; }
            else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; need = 3; }
            else put_cp(cur, 0xFFFD);
        }
    }
    fclose(f);
    if (nl > 1 && ll[nl - 1] == 0) line_del(nl - 1);                  /* the text ended with a newline */
    snprintf(msg, sizeof msg, "%d lines", nl);
}
static int save(void) {
    FILE *f = fopen(path, "w");
    if (!f) { snprintf(msg, sizeof msg, "Cannot save %s", path); thos_notify(3, "Not saved|cannot write the file"); return -1; }
    for (int i = 0; i < nl; i++) {
        for (int k = 0; k < ll[i]; k++) {
            uint32_t c = ln[i][k];
            char o[4]; int m = 0;
            if (c < 0x80) o[m++] = (char)c;
            else if (c < 0x800) { o[m++] = (char)(0xC0 | (c >> 6)); o[m++] = (char)(0x80 | (c & 0x3F)); }
            else if (c < 0x10000) { o[m++] = (char)(0xE0 | (c >> 12)); o[m++] = (char)(0x80 | ((c >> 6) & 0x3F)); o[m++] = (char)(0x80 | (c & 0x3F)); }
            else { o[m++] = (char)(0xF0 | (c >> 18)); o[m++] = (char)(0x80 | ((c >> 12) & 0x3F)); o[m++] = (char)(0x80 | ((c >> 6) & 0x3F)); o[m++] = (char)(0x80 | (c & 0x3F)); }
            fwrite(o, 1, (size_t)m, f);
        }
        fputc('\n', f);
    }
    fclose(f);
    modified = 0;
    snprintf(msg, sizeof msg, "Saved %s", path);
    char t[120]; snprintf(t, sizeof t, "Saved|%.100s", path);
    thos_notify(1, t);
    return 0;
}
static const char *base(void) { const char *b = strrchr(path, '/'); return b ? b + 1 : path; }
static void set_title(void) {
    struct wl_msg t = { WL_TITLE, 0, 0, 0, 0 };
    char name[17]; snprintf(name, sizeof name, "%.14s%s", base(), modified ? "*" : "");
    memcpy(&t.a, name, 16);
    (void)!write(sock, &t, sizeof t);
}
static void reveal(void) {                                           /* scroll so that the caret line is on screen */
    if (cy < top_t) top_t = (float)cy;
    if (cy > top_t + VIS - 2) top_t = (float)(cy - VIS + 2);
    if (top_t < 0) top_t = 0;
    spring_to(&scroll, top_t);
    blink_on = 1; blink_t0 = mo_now_ms();
}
static void render(void) {
    for (int i = 0; i < EW * EH; i++) px[i] = UI_SURFACE;
    /* toolbar: file name pill, modified dot, save pill */
    for (int y = 0; y < HEAD; y++) for (int x = 0; x < EW; x++) px[y * EW + x] = 0x1b202e;
    for (int x = 0; x < EW; x++) px[(HEAD - 1) * EW + x] = UI_LINE;
    ui_icon(px, EW, EH, 28, 22, IC_FILE, 15, UI_ACCENT_HI);
    ui_text(px, EW, EH, 46, 27, base(), UI_BOLD, 14, UI_TEXT);
    if (modified) ui_disc(px, EW, EH, 46 + ui_text_width(UI_BOLD, 14, base()) + 12, 21, 3, UI_WARN);
    int bx = EW - 104, by = 8;
    if (modified) { ui_rrect_g(px, EW, EH, bx, by, 88, 28, 14, UI_ACCENT_HI, UI_ACCENT_LO, 255); ui_text(px, EW, EH, bx + 44 - ui_text_width(UI_BOLD, 12, "Save") / 2, by + 19, "Save", UI_BOLD, 12, 0xffffffu); }
    else { ui_card(px, EW, EH, bx, by, 88, 28, 14, 0x1d2333, 0x2d3448); ui_text(px, EW, EH, bx + 44 - ui_text_width(UI_REG, 12, "Saved") / 2, by + 19, "Saved", UI_REG, 12, UI_MUTED); }
    /* the text area */
    ui_set_clip(0, HEAD, EW, EH - STATUS);
    for (int y = HEAD; y < EH - STATUS; y++) for (int x = 0; x < GUT; x++) px[y * EW + x] = 0x141823;
    float sc = scroll.p;
    int first = (int)sc;
    for (int i = first; i <= first + VIS + 1 && i < nl; i++) {
        int y = HEAD + (int)((i - sc) * LH + 0.5f);
        if (i == cy) { ui_rrect(px, EW, EH, GUT, y, EW - GUT, LH, 0, 0x1b2132); }
        char num[12]; snprintf(num, sizeof num, "%d", i + 1);
        ui_text(px, EW, EH, GUT - 12 - ui_text_width(UI_MONO, 12, num), y + 15, num, UI_MONO, 12, i == cy ? 0xb4bbd0u : 0x5b6580);
        char vis[2048]; int n = ll[i] < 400 ? ll[i] : 400, m = 0;
        for (int k = 0; k < n; k++) {
            uint32_t c = ln[i][k];
            if (c < 32) c = '?';
            if (c < 0x80) vis[m++] = (char)c;
            else if (c < 0x800) { vis[m++] = (char)(0xC0 | (c >> 6)); vis[m++] = (char)(0x80 | (c & 0x3F)); }
            else if (c < 0x10000) { vis[m++] = (char)(0xE0 | (c >> 12)); vis[m++] = (char)(0x80 | ((c >> 6) & 0x3F)); vis[m++] = (char)(0x80 | (c & 0x3F)); }
            else { vis[m++] = (char)(0xF0 | (c >> 18)); vis[m++] = (char)(0x80 | ((c >> 12) & 0x3F)); vis[m++] = (char)(0x80 | ((c >> 6) & 0x3F)); vis[m++] = (char)(0x80 | (c & 0x3F)); }
        }
        vis[m] = 0;
        if (m) ui_text(px, EW, EH, GUT + 12, y + 15, vis, UI_MONO, FS, UI_TEXT);
        if (i == cy && blink_on) ui_rrect(px, EW, EH, GUT + 12 + cx * cw, y + 2, 2, LH - 4, 1, UI_ACCENT_HI);
    }
    for (int y = HEAD; y < EH - STATUS; y++) px[y * EW + GUT] = UI_LINE;
    ui_set_clip(0, 0, 0x7fffffff, 0x7fffffff);
    /* status bar */
    for (int y = EH - STATUS; y < EH; y++) for (int x = 0; x < EW; x++) px[y * EW + x] = 0x1b202e;
    for (int x = 0; x < EW; x++) px[(EH - STATUS) * EW + x] = UI_LINE;
    char st[64]; snprintf(st, sizeof st, "Ln %d, Col %d", cy + 1, cx + 1);
    ui_text(px, EW, EH, 16, EH - 9, st, UI_REG, 12, UI_MUTED);
    ui_text(px, EW, EH, 150, EH - 9, msg, UI_REG, 12, 0x8e97ae);
    const char *hint = "Ctrl+S save   UTF-8 text";
    ui_text(px, EW, EH, EW - 16 - ui_text_width(UI_REG, 12, hint), EH - 9, hint, UI_REG, 12, 0x657090);
    struct wl_msg c = { WL_COMMIT, 0, 0, EW, EH };
    (void)!write(sock, &c, sizeof c);
}
static void clampx(void) { if (cx > ll[cy]) cx = ll[cy]; if (cx < 0) cx = 0; }
static void edit_key(const struct wl_msg *m) {
    unsigned a = m->a, c = m->c, mods = m->d;
    int ctrl = (mods & 0x11) != 0, was_mod = modified;
    if (ctrl && a == 0x16) { save(); set_title(); return; }                                  /* Ctrl+S */
    if (a == 0x52 && cy > 0) { cy--; cx = want < ll[cy] ? want : ll[cy]; }
    else if (a == 0x51 && cy + 1 < nl) { cy++; cx = want < ll[cy] ? want : ll[cy]; }
    else if (a == 0x50) { if (cx > 0) cx--; else if (cy > 0) { cy--; cx = ll[cy]; } want = cx; }
    else if (a == 0x4F) { if (cx < ll[cy]) cx++; else if (cy + 1 < nl) { cy++; cx = 0; } want = cx; }
    else if (a == 0x4A) { if (ctrl) cy = 0; cx = 0; want = 0; }
    else if (a == 0x4D) { if (ctrl) cy = nl - 1; cx = ll[cy]; want = cx; }
    else if (a == 0x4B) { cy -= VIS - 2; if (cy < 0) cy = 0; cx = want < ll[cy] ? want : ll[cy]; }
    else if (a == 0x4E) { cy += VIS - 2; if (cy >= nl) cy = nl - 1; cx = want < ll[cy] ? want : ll[cy]; }
    else if (a == 0x28) {                                                                      /* Enter: split the line */
        line_ins(cy + 1);
        line_put(cy + 1, 0, ln[cy] + cx, ll[cy] - cx);
        ll[cy] = cx;
        cy++; cx = 0; want = 0; modified = 1;
    } else if (a == 0x2A) {                                                                    /* Backspace */
        if (cx > 0) { memmove(ln[cy] + cx - 1, ln[cy] + cx, (size_t)(ll[cy] - cx) * sizeof(uint32_t)); ll[cy]--; cx--; modified = 1; }
        else if (cy > 0) { int px0 = ll[cy - 1]; line_put(cy - 1, px0, ln[cy], ll[cy]); line_del(cy); cy--; cx = px0; modified = 1; }
        want = cx;
    } else if (a == 0x4C) {                                                                    /* Delete */
        if (cx < ll[cy]) { memmove(ln[cy] + cx, ln[cy] + cx + 1, (size_t)(ll[cy] - cx - 1) * sizeof(uint32_t)); ll[cy]--; modified = 1; }
        else if (cy + 1 < nl) { line_put(cy, ll[cy], ln[cy + 1], ll[cy + 1]); line_del(cy + 1); modified = 1; }
    } else if (a == 0x2B) { static const uint32_t sp[4] = { ' ', ' ', ' ', ' ' }; line_put(cy, cx, sp, 4); cx += 4; want = cx; modified = 1; }
    else if (!ctrl && c >= 32 && c != 127 && c < 0x110000) { uint32_t ch = c; line_put(cy, cx, &ch, 1); cx++; want = cx; modified = 1; }
    else return;
    if (modified != was_mod) set_title();
    if (modified) snprintf(msg, sizeof msg, "Edited");
    reveal();
}

int main(int argc, char **argv) {
    int key = 0x5000 + getpid();
    int id = shmget(key, EW * EH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1 || gfx_load_font() != 0 || ui_init() != 0) { puts("edit FAIL: shm/font"); return 1; }
    if (argc > 1) snprintf(path, sizeof path, "%s", argv[1]);
    else { const char *h = getenv("HOME"); snprintf(path, sizeof path, "%s/untitled.txt", h && h[1] ? h : "/tmp"); }
    load(path);
    cw = ui_text_width(UI_MONO, FS, "MMMMMMMMMM") / 10;
    if (cw < 4) cw = 8;
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(sock, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("edit FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, EW, EH, (uint32_t)key, 0 };
    (void)!write(sock, &m, sizeof m);
    if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("edit FAIL: no CREATED"); return 1; }
    shmctl(id, IPC_RMID, 0);
    set_title();
    ui_cursor(sock, 1);
    spring_init(&scroll, 0, SP_SOFT);
    blink_t0 = mo_now_ms();
    render();
    printf("edit ready\n");
    fflush(stdout);
    long long clk = mo_now_ms();
    for (;;) {
        int busy = spring_busy(&scroll);
        struct pollfd pf = { sock, POLLIN, 0 };
        poll(&pf, 1, busy ? MO_FRAME_MS : 250);
        long long now = mo_now_ms();
        float dt = (float)(now - clk) / 1000.0f;
        clk = now;
        int on = ((now - blink_t0) / 530) % 2 == 0;
        int redraw = 0;
        if (on != blink_on) { blink_on = on; redraw = 1; }
        if (busy) { spring_step(&scroll, dt); redraw = 1; }
        if (!(pf.revents & POLLIN)) { if (redraw) render(); continue; }
        if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op == WL_CLOSE) break;
        if (m.op == WL_THEME) { ui_theme_load(); render(); continue; }
        if (m.op == WL_KEY && m.b) edit_key(&m);
        else if (m.op == WL_POINTER && m.c == 3) { top_t -= (float)(int32_t)m.d * 3; if (top_t > nl - 1) top_t = (float)(nl - 1); if (top_t < 0) top_t = 0; spring_to(&scroll, top_t); }
        else if (m.op == WL_POINTER && m.c == 1) {
            int x = (int)m.a, y = (int)m.b;
            if (y >= HEAD && y < EH - STATUS && x > GUT) {
                int l = (int)(scroll.p + (float)(y - HEAD) / LH);
                if (l >= nl) l = nl - 1;
                cy = l < 0 ? 0 : l; cx = (x - GUT - 12 + cw / 2) / cw; clampx(); want = cx; blink_on = 1; blink_t0 = now;
            } else if (y < HEAD && x >= EW - 104 && modified) { save(); set_title(); }
        } else continue;
        render();
    }
    printf("edit done\n");
    return 0;
}
