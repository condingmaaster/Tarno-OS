// SPDX-License-Identifier: GPL-2.0-or-later
// THOS software centre (a legacy-protocol desktop client): search the Debian package index that `thos-pkg update`
// built (/opt/.index) and install or remove programs with `thos-pkg` in a terminal window.
//   type to search, Up/Down select, Enter or double-click installs (removes, when it is already installed), Esc clears the query.
#define _GNU_SOURCE
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
#include "thosnotify.h"

#define SW 760
#define SH 520
#define HEAD 78
#define ROW 58
#define FOOT 30
#define VIS ((SH - HEAD - FOOT) / ROW)
#define MAXR 300
#define LIST_Y (HEAD + 6)
#define LIST_H (SH - HEAD - FOOT - 12)
static uint32_t *px;
static int sock;
static char *index_buf;
static size_t index_len;
static char query[64];
static struct res { const char *line; size_t len; } res[MAXR];
static int nres;
static struct ui_list list;
static char status[160];

static int load_index(void) {
    FILE *f = fopen("/opt/.index", "r");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    index_buf = malloc((size_t)n + 1);
    if (!index_buf) { fclose(f); return 0; }
    index_len = fread(index_buf, 1, (size_t)n, f);
    index_buf[index_len] = 0;
    fclose(f);
    return 1;
}
static void field(const struct res *r, int k, char *out, size_t cap) {      /* k-th '|' separated field of the index line */
    const char *p = r->line, *end = r->line + r->len;
    for (int i = 0; i < k && p < end; i++) { while (p < end && *p != '|') p++; if (p < end) p++; }
    size_t n = 0;
    while (p + n < end && p[n] != '|' && n + 1 < cap) { out[n] = p[n]; n++; }
    out[n] = 0;
}
static int installed(const char *name) {
    char p[200];
    snprintf(p, sizeof p, "/opt/.db/%s.list", name);
    struct stat st;
    return stat(p, &st) == 0;
}
static void search(void) {
    nres = 0;
    if (!index_buf || !query[0]) return;
    size_t ql = strlen(query);
    for (int pass = 0; pass < 2 && nres < MAXR; pass++) {                 /* pass 0: names, pass 1: descriptions */
        const char *p = index_buf;
        while (*p && nres < MAXR) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (len > 0) {
                const char *bar = memchr(p, '|', len);
                size_t nlen = bar ? (size_t)(bar - p) : len;
                int in_name = memmem(p, nlen, query, ql) != 0;
                if (!in_name) {                                           /* names: do a case-insensitive compare below */
                    char tmp[96]; size_t k = nlen < 95 ? nlen : 95;
                    memcpy(tmp, p, k); tmp[k] = 0;
                    in_name = strcasestr(tmp, query) != 0;
                }
                int hit = 0;
                if (pass == 0) hit = in_name;
                else if (!in_name) {
                    const char *d = p; int bars = 0;
                    while (d < p + len && bars < 6) { if (*d == '|') bars++; d++; }
                    char desc[160]; size_t dl = (size_t)(p + len - d); if (dl > 159) dl = 159;
                    memcpy(desc, d, dl); desc[dl] = 0;
                    hit = strcasestr(desc, query) != 0;
                }
                if (hit) { res[nres].line = p; res[nres].len = len; nres++; }
            }
            if (!nl) break;
            p = nl + 1;
        }
    }
    snprintf(status, sizeof status, "%d result%s", nres, nres == 1 ? "" : "s");
    ui_list_reset(&list, nres);
}
static void commit(void) { struct wl_msg c = { WL_COMMIT, 0, 0, SW, SH }; (void)!write(sock, &c, sizeof c); }
static int hover_row = -1;
/* a running install/remove: thos-pkg's output arrives through a pipe and its last line is shown as progress */
static int job_fd = -1, job_pid, job_remove, job_ok_shown;
static char job_name[64], job_line[128];
static long long job_t0;
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
static void job_start(const char *name, int remove) {
    if (job_fd >= 0) return;
    int p[2];
    if (pipe(p) != 0) return;
    int pid = fork();
    if (pid == 0) {
        dup2(p[1], 1); dup2(p[1], 2); close(p[0]); close(p[1]);
        setenv("HOME", "/tmp", 1);
        const char *tp = getenv("THOS_PKG");
        if (tp) execl("/busybox", "sh", tp, remove ? "remove" : "install", name, (char *)0);
        else execl("/bin/thos-pkg", "thos-pkg", remove ? "remove" : "install", name, (char *)0);
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) { close(p[0]); return; }
    job_fd = p[0]; job_pid = pid; job_remove = remove; job_t0 = mo_now_ms();
    snprintf(job_name, sizeof job_name, "%s", name);
    snprintf(job_line, sizeof job_line, "%s", remove ? "removing ..." : "starting ...");
    snprintf(status, sizeof status, "%s %s ...", remove ? "removing" : "installing", name);
}
static void job_read(void) {                       /* output (a line at a time) while the job runs; its end finishes it */
    char buf[256];
    ssize_t n = read(job_fd, buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = 0;
        char *e = buf + n;
        while (e > buf && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        char *l = strrchr(buf, '\n');
        snprintf(job_line, sizeof job_line, "%s", l ? l + 1 : buf);
        return;
    }
    close(job_fd); job_fd = -1;
    int st = 0;
    waitpid(job_pid, &st, 0);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) snprintf(status, sizeof status, "%s %s", job_name, job_remove ? "removed" : "installed — find it in the Start menu");
    else snprintf(status, sizeof status, "%s %s failed: %s", job_remove ? "removing" : "installing", job_name, job_line);
    job_ok_shown = 1;
    char tn[120];
    int ok = strstr(status, "failed") == 0;
    snprintf(tn, sizeof tn, "%s|%s", ok ? (job_remove ? "Program removed" : "Program installed") : (job_remove ? "Removing failed" : "Installing failed"), ok ? job_name : job_line);
    thos_notify(ok ? 1 : 3, tn);
}
static void render(void) {
    for (int i = 0; i < SW * SH; i++) px[i] = UI_SURFACE;
    /* header with the search field */
    for (int y = 0; y < HEAD; y++) for (int x = 0; x < SW; x++) px[y * SW + x] = 0x1b202e;
    for (int x = 0; x < SW; x++) px[(HEAD - 1) * SW + x] = UI_LINE;
    ui_text(px, SW, SH, 20, 28, "Software", UI_BOLD, 17, UI_TEXT);
    ui_text(px, SW, SH, 20 + ui_text_width(UI_BOLD, 17, "Software") + 10, 28, "Debian archive", UI_REG, 12, UI_MUTED);
    ui_card(px, SW, SH, 16, 38, SW - 32, 32, 10, 0x131722, query[0] ? 0x4a639e : 0x2d3448);
    ui_icon(px, SW, SH, 34, 54, IC_SEARCH, 13, UI_MUTED);
    char q[90]; snprintf(q, sizeof q, "%s%s", query, "|");
    if (query[0]) ui_text(px, SW, SH, 52, 59, q, UI_REG, 14, UI_TEXT);
    else ui_text(px, SW, SH, 52, 59, index_buf ? "Search programs by name or description" : "No package index yet", UI_REG, 14, 0x657090);
    if (!index_buf) {
        ui_rrect_g(px, SW, SH, SW / 2 - 130, HEAD + 80, 260, 44, 12, UI_ACCENT_HI, 0x3f69d6, 255);
        ui_icon(px, SW, SH, SW / 2 - 100, HEAD + 102, IC_REFRESH, 15, 0xffffffu);
        ui_text(px, SW, SH, SW / 2 - 78, HEAD + 108, "Download the package list", UI_BOLD, 14, 0xffffffu);
        ui_text(px, SW, SH, SW / 2 - ui_text_width(UI_REG, 13, "Needs the network. Takes a few minutes the first time.") / 2, HEAD + 150, "Needs the network. Takes a few minutes the first time.", UI_REG, 13, UI_MUTED);
    } else if (!query[0]) {
        ui_text(px, SW, SH, SW / 2 - ui_text_width(UI_REG, 15, "Type a name to find a program") / 2, HEAD + 120, "Type a name to find a program", UI_REG, 15, UI_MUTED);
        ui_icon(px, SW, SH, SW / 2, HEAD + 80, IC_SEARCH, 30, 0x39425e);
    } else if (nres == 0) {
        ui_text(px, SW, SH, SW / 2 - ui_text_width(UI_REG, 15, "Nothing matches") / 2, HEAD + 120, "Nothing matches", UI_REG, 15, UI_MUTED);
    }
    ui_set_clip(0, HEAD, SW, SH - FOOT);
    ui_list_pills(px, SW, SH, &list, 8, LIST_Y, SW - 16, 0x222a40, 0x1b2132, 11);
    for (int i = 0; i < nres; i++) {
        int y = LIST_Y + ui_list_row_y(&list, i);
        if (y + ROW < HEAD || y > SH - FOOT) continue;
        char name[64], ver[40], desc[160], size[16];
        field(&res[i], 0, name, sizeof name); field(&res[i], 1, ver, sizeof ver); field(&res[i], 5, size, sizeof size); field(&res[i], 6, desc, sizeof desc);
        int inst = installed(name);
        ui_tile(px, SW, SH, 20, y + 4, 42, name);
        ui_text(px, SW, SH, 76, y + 20, name, UI_BOLD, 14, UI_TEXT);
        char meta[80]; snprintf(meta, sizeof meta, "%s   %ld kB", ver, atol(size) / 1024);
        ui_text(px, SW, SH, 76 + ui_text_width(UI_BOLD, 14, name) + 12, y + 20, meta, UI_REG, 11, UI_MUTED);
        char dd[160]; snprintf(dd, sizeof dd, "%s", desc);
        while (ui_text_width(UI_REG, 12, dd) > SW - 76 - 150 && strlen(dd) > 4) dd[strlen(dd) - 1] = 0;
        ui_text(px, SW, SH, 76, y + 40, dd, UI_REG, 12, 0xa8b0c8);
        int bx = SW - 112, by = y + 11, bw = 88;                                          /* the action pill */
        if (job_fd >= 0 && !strcmp(name, job_name)) {                                  /* busy: an indeterminate bar slides inside the pill */
            ui_card(px, SW, SH, bx - 8, by, bw + 8, 28, 14, 0x1d2333, 0x4a639e);
            float ph = (float)((mo_now_ms() - job_t0) % 1200) / 1200.0f;
            int segw = 28, bxx = bx + (int)((bw - 24) * ph);
            ui_set_clip(bx - 2, by + 2, bx + bw - 6, by + 26);
            ui_rrect_g(px, SW, SH, bxx - segw / 2, by + 22, segw, 3, 1, UI_ACCENT_HI, UI_ACCENT_HI, 220);
            ui_set_clip(0, HEAD, SW, SH - FOOT);
            const char *bt = job_remove ? "Removing" : "Installing";
            ui_text(px, SW, SH, bx - 8 + (bw + 8 - ui_text_width(UI_REG, 12, bt)) / 2, by + 18, bt, UI_REG, 12, UI_TEXT);
        } else if (inst) { ui_card(px, SW, SH, bx, by, bw, 28, 14, 0x1d2333, 0x3a4560); ui_text(px, SW, SH, bx + (bw - ui_text_width(UI_REG, 12, "Remove")) / 2, by + 19, "Remove", UI_REG, 12, 0xff9aa6); }
        else { ui_rrect_g(px, SW, SH, bx, by, bw, 28, 14, UI_ACCENT_HI, 0x3f69d6, 255); ui_text(px, SW, SH, bx + (bw - ui_text_width(UI_BOLD, 12, "Install")) / 2, by + 19, "Install", UI_BOLD, 12, 0xffffffu); }
        if (inst) ui_icon(px, SW, SH, bx - 16, by + 14, 0xf00c, 12, UI_GOOD);
    }
    ui_scrollbar(px, SW, SH, &list, SW - 9, LIST_Y);
    ui_set_clip(0, 0, 0x7fffffff, 0x7fffffff);
    for (int y = SH - FOOT; y < SW * SH / SW && y < SH; y++) for (int x = 0; x < SW; x++) px[y * SW + x] = 0x1b202e;
    for (int x = 0; x < SW; x++) px[(SH - FOOT) * SW + x] = UI_LINE;
    if (job_fd >= 0) {
        char ln[200]; snprintf(ln, sizeof ln, "%s %s: %s", job_remove ? "Removing" : "Installing", job_name, job_line);
        while (ui_text_width(UI_REG, 12, ln) > SW - 32 && strlen(ln) > 4) ln[strlen(ln) - 1] = 0;
        ui_text(px, SW, SH, 16, SH - 10, ln, UI_REG, 12, UI_TEXT);
    } else ui_text(px, SW, SH, 16, SH - 10, status, UI_REG, 12, job_ok_shown && strstr(status, "failed") ? 0xff9aa6 : UI_MUTED);
    commit();
}
static void in_terminal(const char *cmd) {
    if (fork() != 0) return;
    setenv("HOME", "/tmp", 1); setenv("XDG_RUNTIME_DIR", "/run", 1);
    execl("/thosterm", "thosterm", "-c", cmd, (char *)0);
    _exit(127);
}
static void act(void) {
    if (!index_buf) { in_terminal("/bin/thos-pkg update"); snprintf(status, sizeof status, "updating the index ... reopen the software centre afterwards"); return; }
    if (list.sel >= nres) return;
    char name[64], cmd[200];
    field(&res[list.sel], 0, name, sizeof name);
    for (char *c = name; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '.' || *c == '+')) return;
    snprintf(cmd, sizeof cmd, "/bin/thos-pkg %s %s", installed(name) ? "remove" : "install", name);
    (void)cmd;
    job_start(name, installed(name));
}
static void move(int to) { ui_list_select(&list, to); }

int main(void) {
    int key = 0x5000 + getpid();
    int id = shmget(key, SW * SH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1 || gfx_load_font() != 0 || ui_init() != 0) { puts("soft FAIL: shm/font"); return 1; }
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(sock, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("soft FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, SW, SH, (uint32_t)key, 0 };
    (void)!write(sock, &m, sizeof m);
    if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("soft FAIL: no CREATED"); return 1; }
    shmctl(id, IPC_RMID, 0);
    struct wl_msg t = { WL_TITLE, 0, 0, 0, 0 };
    memcpy(&t.a, "Software", 8);
    (void)!write(sock, &t, sizeof t);
    ui_list_init(&list, ROW, LIST_H);
    load_index();
    const char *q0 = getenv("SOFT_QUERY");
    if (q0) { snprintf(query, sizeof query, "%s", q0); search(); }
    else snprintf(status, sizeof status, index_buf ? "Type a name or a word from the description" : "No package index");
    render();
    if (getenv("SOFT_AUTO") && nres) { list.sel = 0; act(); }          /* test hook: start the install of the first result */
    printf("soft ready\n");
    fflush(stdout);
    int last_row = -1;
    long long last_click = 0, clk = mo_now_ms();
    for (;;) {
        int busy = ui_list_busy(&list) || job_fd >= 0;
        struct pollfd pf[2] = { { sock, POLLIN, 0 }, { job_fd, POLLIN, 0 } };
        poll(pf, 2, busy ? MO_FRAME_MS : -1);
        if (job_fd >= 0 && (pf[1].revents & (POLLIN | POLLHUP))) { job_read(); render(); }
        int pr = (pf[0].revents & POLLIN) ? 1 : 0;
        long long nowt = mo_now_ms();
        float dt = (float)(nowt - clk) / 1000.0f;
        clk = nowt;
        if (busy) { ui_list_step(&list, dt); render(); }
        if (!pr && pf[0].revents & (POLLHUP | POLLERR)) break;
        if (pr <= 0) continue;
        if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op == WL_CLOSE) break;
        if (m.op == WL_POINTER && m.c == 1) {
            long long now = mo_now_ms();
            if (!index_buf && (int)m.b > HEAD + 70 && (int)m.b < HEAD + 130) act();
            else if ((int)m.b >= HEAD) {
                int i = ui_list_at(&list, (int)m.b - LIST_Y);
                if (i >= 0 && i < nres) {
                    move(i);
                    if ((int)m.a >= SW - 112 || (i == last_row && now - last_click < 800 * mo_slomo())) { act(); last_row = -1; } else { last_row = i; last_click = now; }
                }
            }
            render();
        } else if (m.op == WL_POINTER && m.c == 3) {
            ui_list_wheel(&list, (int)(int32_t)m.d);
        } else if (m.op == WL_POINTER) {
            int row = m.c == 0 && (int)m.b >= LIST_Y ? ui_list_at(&list, (int)m.b - LIST_Y) : -1;
            ui_list_hover(&list, row);
            ui_cursor(sock, m.c != 0 ? 0 : (int)m.b > 36 && (int)m.b < 72 ? 1 : (row >= 0 && row < nres) || (!index_buf && (int)m.b > HEAD + 70 && (int)m.b < HEAD + 130) ? 2 : 0);
        } else if (m.op == WL_KEY && m.b) {
            size_t ql = strlen(query);
            if (m.a == 0x52) move(list.sel - 1);
            else if (m.a == 0x51) move(list.sel + 1);
            else if (m.a == 0x4B) move(list.sel - LIST_H / ROW);
            else if (m.a == 0x4E) move(list.sel + LIST_H / ROW);
            else if (m.a == 0x28) act();
            else if (m.a == 0x29) { query[0] = 0; search(); }
            else if (m.a == 0x2A) { if (ql) { query[ql - 1] = 0; search(); } }
            else if (m.c >= 32 && m.c < 127 && ql + 1 < sizeof query) { query[ql] = (char)m.c; query[ql + 1] = 0; search(); }
            else continue;
            render();
        }
    }
    printf("soft done\n");
    return 0;
}
