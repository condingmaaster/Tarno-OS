// SPDX-License-Identifier: GPL-2.0-or-later
// THOS file manager (a legacy-protocol desktop client): places on the left, the folder on the right.
//   click / Up / Down / PgUp / PgDn / Home / End select, double-click or Enter opens, Backspace goes up,
//   Delete (twice) removes the selected file, R refreshes.
//   Opening a .deb/.rpm/.zip/.tar.* runs `thos-pkg install` in a terminal window, a .exe or an executable runs it there,
//   images open in the viewer, text files in `less`.
#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "thoswl.h"
#include "thosgfx.h"
#include "thosui.h"

#define FW 780
#define FH 500
#define SIDE 176
#define TOP 52
#define FOOT 30
#define ROW 36
#define LIST_Y (TOP + 6)
#define LIST_H (FH - TOP - FOOT - 12)
#define MAXE 512
static uint32_t *px;
static int sock;
static char cwd[512] = "/";
static struct ent { char name[96]; int dir, exec; long long size; } ent[MAXE];
static int nent, side_hover = -1, side_hot = -1;
static struct ui_list list;
static struct spring side_hl;                  /* the sidebar's hover highlight: a pill that glides between places */
static char status[140];
static int armed = -1;                         /* Delete pressed once on this entry */
static const struct { const char *label, *path; uint32_t icon; } places[] = {
    { "Home", "/tmp", IC_HOME }, { "Programs", "/opt", IC_BOX }, { "Computer", "/", IC_DESKTOP }, { "System", "/etc", IC_COG } };
#define NPLACES 4

static int has_suffix(const char *n, const char *suf) {
    size_t a = strlen(n), b = strlen(suf);
    return a >= b && !strcasecmp(n + a - b, suf);
}
static int cmp_ent(const void *a, const void *b) {
    const struct ent *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}
static void load(void) {
    nent = 0; armed = -1;
    DIR *d = opendir(cwd);
    if (!d) { snprintf(status, sizeof status, "Cannot read %s", cwd); return; }
    struct dirent *e;
    while ((e = readdir(d)) && nent < MAXE) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || e->d_name[0] == '.') continue;
        struct ent *n = &ent[nent];
        snprintf(n->name, sizeof n->name, "%s", e->d_name);
        char full[700];
        snprintf(full, sizeof full, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", n->name);
        struct stat st;
        n->dir = 0; n->exec = 0; n->size = 0;
        if (stat(full, &st) == 0) { n->dir = S_ISDIR(st.st_mode); n->exec = !n->dir && (st.st_mode & 0111); n->size = st.st_size; }
        nent++;
    }
    closedir(d);
    qsort(ent, (size_t)nent, sizeof ent[0], cmp_ent);
    snprintf(status, sizeof status, "%d item%s", nent, nent == 1 ? "" : "s");
    ui_list_reset(&list, nent);
}
static void commit(void) { struct wl_msg c = { WL_COMMIT, 0, 0, FW, FH }; (void)!write(sock, &c, sizeof c); }
static void size_str(long long v, char *out) {
    if (v < 1024) snprintf(out, 20, "%lld B", v);
    else if (v < 1024 * 1024) snprintf(out, 20, "%lld kB", v / 1024);
    else snprintf(out, 20, "%.1f MB", (double)v / (1024.0 * 1024.0));
}
/* the glyph and colour of an entry by its type */
static void kind_of(const struct ent *e, uint32_t *glyph, uint32_t *color) {
    *glyph = IC_FILE; *color = 0x6b7794;
    if (e->dir) { *glyph = IC_FOLDER; *color = 0xe0a23c; }
    else if (has_suffix(e->name, ".png") || has_suffix(e->name, ".jpg") || has_suffix(e->name, ".jpeg")) { *glyph = IC_IMAGE; *color = 0xe06c9f; }
    else if (has_suffix(e->name, ".deb") || has_suffix(e->name, ".rpm") || has_suffix(e->name, ".zip") || has_suffix(e->name, ".tar.gz") || has_suffix(e->name, ".tgz") || has_suffix(e->name, ".tar.xz") || has_suffix(e->name, ".msi")) { *glyph = IC_ARCHIVE; *color = 0xf0a040; }
    else if (has_suffix(e->name, ".exe")) { *glyph = IC_WIN; *color = 0x4aa8ee; }
    else if (e->exec) { *glyph = IC_TERMINAL; *color = 0x4cc38a; }
    else if (has_suffix(e->name, ".sh")) { *glyph = IC_TERMINAL; *color = 0x4cc38a; }
}
static void render(void) {
    for (int i = 0; i < FW * FH; i++) px[i] = UI_SURFACE;
    /* sidebar */
    for (int y = 0; y < FH; y++) for (int x = 0; x < SIDE; x++) px[y * FW + x] = 0x121620;
    for (int y = 0; y < FH; y++) px[y * FW + SIDE - 1] = UI_LINE;
    ui_text(px, FW, FH, 20, 36, "Places", UI_BOLD, 11, UI_MUTED);
    if (side_hover >= 0) ui_rrect_g(px, FW, FH, 10, 52 + (int)side_hl.p, SIDE - 20, 32, 9, 0x1c2336, 0x1c2336, 255);
    for (int i = 0; i < NPLACES; i++) {
        int y = 52 + i * 38, here = !strcmp(cwd, places[i].path) || (i == 2 && !strcmp(cwd, "/"));
        if (here) ui_rrect(px, FW, FH, 10, y, SIDE - 20, 32, 9, 0x232b42);
        ui_icon(px, FW, FH, 32, y + 16, places[i].icon, 14, here ? UI_ACCENT_HI : 0x8e97ae);
        ui_text(px, FW, FH, 52, y + 21, places[i].label, here ? UI_BOLD : UI_REG, 14, here ? UI_TEXT : 0xc2c9dc);
    }
    /* toolbar */
    for (int y = 0; y < TOP; y++) for (int x = SIDE; x < FW; x++) px[y * FW + x] = 0x1b202e;
    for (int x = SIDE; x < FW; x++) px[(TOP - 1) * FW + x] = UI_LINE;
    ui_rrect(px, FW, FH, SIDE + 12, 10, 34, 32, 9, side_hot == 100 ? 0x2a3248 : 0x232a3c);
    ui_icon(px, FW, FH, SIDE + 29, 26, IC_UP, 14, UI_TEXT);
    ui_rrect(px, FW, FH, SIDE + 56, 10, FW - SIDE - 70, 32, 9, 0x131722);
    ui_icon(px, FW, FH, SIDE + 74, 26, IC_FOLDER, 13, 0xe0a23c);
    ui_set_clip(SIDE + 90, 0, FW - 24, FH);
    ui_text(px, FW, FH, SIDE + 90, 31, cwd, UI_REG, 14, UI_TEXT);
    ui_set_clip(0, 0, 0x7fffffff, 0x7fffffff);
    /* list */
    ui_set_clip(SIDE, TOP, FW, FH - FOOT);
    if (nent == 0) ui_text(px, FW, FH, SIDE + 28, TOP + 56, "This folder is empty", UI_REG, 15, UI_MUTED);
    ui_list_pills(px, FW, FH, &list, SIDE + 8, LIST_Y, FW - SIDE - 16, 0x26304a, 0x1d2437, 9);
    for (int i = 0; i < nent; i++) {
        int y = LIST_Y + ui_list_row_y(&list, i);
        if (y + ROW < TOP || y > FH - FOOT) continue;
        struct ent *e = &ent[i];
        uint32_t glyph, color;
        kind_of(e, &glyph, &color);
        ui_rrect_g(px, FW, FH, SIDE + 20, y + 4, 26, 26, 8, ui_shade(color, 12), ui_shade(color, -22), 255);
        ui_icon(px, FW, FH, SIDE + 33, y + 17, glyph, 12, 0xffffffu);
        char nm[80]; snprintf(nm, sizeof nm, "%s", e->name);
        while (ui_text_width(UI_REG, 14, nm) > FW - SIDE - 190 && strlen(nm) > 4) nm[strlen(nm) - 1] = 0;
        ui_text(px, FW, FH, SIDE + 58, y + 23, nm, e->dir ? UI_BOLD : UI_REG, 14, UI_TEXT);
        if (!e->dir) { char sz[20]; size_str(e->size, sz); ui_text(px, FW, FH, FW - 26 - ui_text_width(UI_REG, 12, sz), y + 22, sz, UI_REG, 12, UI_MUTED); }
        else ui_icon(px, FW, FH, FW - 30, y + 17, IC_CHEV, 10, 0x5d6782);
    }
    ui_scrollbar(px, FW, FH, &list, FW - 9, LIST_Y);
    ui_set_clip(0, 0, 0x7fffffff, 0x7fffffff);
    /* footer */
    for (int y = FH - FOOT; y < FH; y++) for (int x = SIDE; x < FW; x++) px[y * FW + x] = 0x1b202e;
    for (int x = SIDE; x < FW; x++) px[(FH - FOOT) * FW + x] = UI_LINE;
    ui_text(px, FW, FH, SIDE + 16, FH - 10, status, UI_REG, 12, UI_MUTED);
    commit();
}
static void go_path(const char *p) {
    DIR *d = opendir(p);
    if (!d) { snprintf(status, sizeof status, "Cannot open %s", p); return; }
    closedir(d);
    snprintf(cwd, sizeof cwd, "%s", p);
    load();
}
static void go(const char *to) {
    char p[512];
    if (!strcmp(to, "..")) {
        snprintf(p, sizeof p, "%s", cwd);
        char *s = strrchr(p, '/');
        if (s == p) p[1] = 0; else if (s) *s = 0;
    } else snprintf(p, sizeof p, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", to);
    go_path(p);
}
static void in_terminal(const char *cmd) {
    if (fork() != 0) return;
    setenv("HOME", "/tmp", 1); setenv("XDG_RUNTIME_DIR", "/run", 1); setenv("LC_ALL", "C.UTF-8", 1);
    if (getenv("FILES_LOG")) execl("/thosterm", "thosterm", "-l", "-c", cmd, (char *)0);   /* -l: terminal lines also to the serial log */
    execl("/thosterm", "thosterm", "-c", cmd, (char *)0);
    _exit(127);
}
static void open_entry(struct ent *e) {
    if (e->dir) { go(e->name); return; }
    if (strchr(e->name, '\'')) { snprintf(status, sizeof status, "That name has a quote in it"); return; }
    char full[700], cmd[900];
    snprintf(full, sizeof full, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", e->name);
    static const char *pkg[] = { ".deb", ".rpm", ".zip", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".msi" };
    for (unsigned k = 0; k < sizeof pkg / sizeof pkg[0]; k++)
        if (has_suffix(e->name, pkg[k])) {
            snprintf(cmd, sizeof cmd, "/bin/thos-pkg install '%s'", full);
            in_terminal(cmd);
            snprintf(status, sizeof status, "Installing %s ...", e->name);
            return;
        }
    if (has_suffix(e->name, ".png") || has_suffix(e->name, ".jpg") || has_suffix(e->name, ".jpeg")) {      /* images open in the viewer */
        if (fork() == 0) { execl("/thosview", "thosview", full, (char *)0); _exit(127); }
        snprintf(status, sizeof status, "Opening %s ...", e->name);
        return;
    }
    if (has_suffix(e->name, ".sh")) snprintf(cmd, sizeof cmd, "/busybox sh '%s'", full);
    else if (has_suffix(e->name, ".exe") || e->exec) snprintf(cmd, sizeof cmd, "'%s'", full);
    else snprintf(cmd, sizeof cmd, "/busybox less '%s'", full);
    in_terminal(cmd);
}
static void select_to(int i) { armed = -1; ui_list_select(&list, i); }

int main(void) {
    int key = 0x5000 + getpid();
    int id = shmget(key, FW * FH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1 || gfx_load_font() != 0 || ui_init() != 0) { puts("files FAIL: shm/font"); return 1; }
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(sock, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("files FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, FW, FH, (uint32_t)key, 0 };
    (void)!write(sock, &m, sizeof m);
    if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("files FAIL: no CREATED"); return 1; }
    shmctl(id, IPC_RMID, 0);
    struct wl_msg t = { WL_TITLE, 0, 0, 0, 0 };
    memcpy(&t.a, "Files", 5);
    (void)!write(sock, &t, sizeof t);
    ui_list_init(&list, ROW, LIST_H);
    spring_init(&side_hl, 0, SP_HOVER);
    char *home = getenv("FILES_START");
    if (home) snprintf(cwd, sizeof cwd, "%s", home);
    load();
    render();
    printf("files ready\n");
    fflush(stdout);
    int last_row = -1;
    long long last_click = 0, clk = mo_now_ms();
    for (;;) {
        int busy = ui_list_busy(&list) || spring_busy(&side_hl);
        int pr = ui_wait(sock, busy);
        long long nowt = mo_now_ms();
        float dt = (float)(nowt - clk) / 1000.0f;
        clk = nowt;
        if (busy) { ui_list_step(&list, dt); spring_step(&side_hl, dt); render(); }
        if (pr <= 0) continue;
        if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op == WL_CLOSE) break;
        if (m.op == WL_POINTER && m.c == 1) {
            long long now = mo_now_ms();
            int x = (int)m.a, y = (int)m.b;
            if (x < SIDE) {
                int i = (y - 52) / 38;
                if (y >= 52 && i >= 0 && i < NPLACES) go_path(places[i].path);
            } else if (y < TOP) {
                if (x >= SIDE + 12 && x < SIDE + 46) go("..");
            } else {
                int i = ui_list_at(&list, y - LIST_Y);
                if (i >= 0 && i < nent) {
                    if (i == last_row && now - last_click < 800 * mo_slomo()) { select_to(i); open_entry(&ent[i]); last_row = -1; }
                    else { select_to(i); last_row = i; last_click = now; }
                }
            }
            render();
        } else if (m.op == WL_POINTER && m.c == 3) {                          /* wheel */
            ui_list_wheel(&list, (int)(int32_t)m.d);
        } else if (m.op == WL_POINTER && m.c == 2) {
            ui_cursor(sock, 0); side_hover = -1; side_hot = -1; ui_list_hover(&list, -1); spring_to(&side_hl, side_hl.p);
        } else if (m.op == WL_POINTER && m.c == 0) {                          /* motion: highlights glide to what is under the pointer */
            int x = (int)m.a, y = (int)m.b;
            int h = x < SIDE && y >= 52 && (y - 52) / 38 < NPLACES ? (y - 52) / 38 : -1;
            int hot = y < TOP && x >= SIDE + 12 && x < SIDE + 46 ? 100 : -1;
            if (h >= 0 && side_hover < 0) spring_init(&side_hl, (float)(h * 38), SP_HOVER);
            if (h >= 0) spring_to(&side_hl, (float)(h * 38));
            side_hover = h; side_hot = hot;
            ui_list_hover(&list, x >= SIDE && y >= LIST_Y ? ui_list_at(&list, y - LIST_Y) : -1);
            ui_cursor(sock, h >= 0 || hot >= 0 || (x >= SIDE && y >= LIST_Y && ui_list_at(&list, y - LIST_Y) >= 0) ? 2 : 0);
            render();
        } else if (m.op == WL_KEY && m.b) {
            switch (m.a) {
                case 0x52: select_to(list.sel - 1); break;
                case 0x51: select_to(list.sel + 1); break;
                case 0x4B: select_to(list.sel - LIST_H / ROW); break;
                case 0x4E: select_to(list.sel + LIST_H / ROW); break;
                case 0x4A: select_to(0); break;
                case 0x4D: select_to(nent - 1); break;
                case 0x28: if (nent) open_entry(&ent[list.sel]); break;
                case 0x2A: go(".."); break;
                case 0x4C:                                    /* Delete, twice */
                    if (!nent) break;
                    if (armed != list.sel) { armed = list.sel; snprintf(status, sizeof status, "Press Delete again to remove %.60s", ent[list.sel].name); }
                    else {
                        char full[700];
                        snprintf(full, sizeof full, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", ent[list.sel].name);
                        int rc = ent[list.sel].dir ? rmdir(full) : unlink(full);
                        snprintf(status, sizeof status, rc == 0 ? "Removed %.60s" : "Could not remove %.60s", ent[list.sel].name);
                        int keep = list.sel;
                        load();
                        select_to(keep);
                    }
                    break;
                default:
                    if (m.c == 'r' || m.c == 'R') { load(); }
                    else continue;
            }
            render();
        }
    }
    printf("files done\n");
    return 0;
}
