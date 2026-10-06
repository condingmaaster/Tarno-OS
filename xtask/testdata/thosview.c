// SPDX-License-Identifier: GPL-2.0-or-later
// THOS image viewer (a legacy-protocol desktop client): PNG and JPEG, scaled to fit the window.
//   thosview <file>      Left/Right = previous/next image in the same folder, Esc = close.
#define _GNU_SOURCE
#include <dirent.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <png.h>
#include <string.h>
#include <strings.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "thoswl.h"
#include "thosgfx.h"
#include "thosui.h"

#define VW 760
#define VH 520
static uint32_t *px;
static int sock;
static char dir[512], files[256][96], cur_name[96];
static int nfiles, cur;
static unsigned char *img;          /* RGB */
static int iw, ih;
static char errmsg[200];

static int is_img(const char *n) {
    size_t l = strlen(n);
    return (l > 4 && !strcasecmp(n + l - 4, ".png")) || (l > 4 && !strcasecmp(n + l - 4, ".jpg")) || (l > 5 && !strcasecmp(n + l - 5, ".jpeg"));
}
static int cmp(const void *a, const void *b) { return strcasecmp(a, b); }
static void list_dir(void) {
    nfiles = 0;
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d)) && nfiles < 256) if (is_img(e->d_name) && strlen(e->d_name) < 96) snprintf(files[nfiles++], 96, "%s", e->d_name);
    if (d) closedir(d);
    qsort(files, (size_t)nfiles, 96, cmp);
}
static int load_png(const char *path) {
    png_image im; memset(&im, 0, sizeof im); im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&im, path)) { snprintf(errmsg, sizeof errmsg, "%s", im.message); return 0; }
    im.format = PNG_FORMAT_RGB;
    unsigned char *buf = malloc(PNG_IMAGE_SIZE(im));
    if (!buf || !png_image_finish_read(&im, 0, buf, 0, 0)) { snprintf(errmsg, sizeof errmsg, "%s", im.message); free(buf); png_image_free(&im); return 0; }
    img = buf; iw = (int)im.width; ih = (int)im.height;
    return 1;
}
struct jerr { struct jpeg_error_mgr pub; jmp_buf jb; };
static void jfail(j_common_ptr c) { longjmp(((struct jerr *)c->err)->jb, 1); }
static int load_jpeg(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    struct jpeg_decompress_struct c; struct jerr je;
    c.err = jpeg_std_error(&je.pub); je.pub.error_exit = jfail;
    if (setjmp(je.jb)) { jpeg_destroy_decompress(&c); fclose(f); free(img); img = 0; return 0; }
    jpeg_create_decompress(&c);
    jpeg_stdio_src(&c, f);
    jpeg_read_header(&c, TRUE);
    c.out_color_space = JCS_RGB;
    c.scale_num = 1; c.scale_denom = c.image_width > 2400 ? 4 : c.image_width > 1200 ? 2 : 1;    /* big photos are decoded at a fraction */
    jpeg_start_decompress(&c);
    iw = (int)c.output_width; ih = (int)c.output_height;
    img = malloc((size_t)iw * ih * 3);
    while (img && c.output_scanline < c.output_height) { unsigned char *row = img + (size_t)c.output_scanline * iw * 3; jpeg_read_scanlines(&c, &row, 1); }
    jpeg_finish_decompress(&c); jpeg_destroy_decompress(&c); fclose(f);
    return img != 0;
}
static void commit(void) { struct wl_msg c = { WL_COMMIT, 0, 0, VW, VH }; (void)!write(sock, &c, sizeof c); }
static void show(void) {
    for (int i = 0; i < VW * VH; i++) px[i] = 0x0c0e14;
    if (!img) {
        ui_icon(px, VW, VH, VW / 2, VH / 2 - 24, IC_IMAGE, 40, 0x39425e);
        const char *msg = nfiles || errmsg[0] ? "This image cannot be read" : "Open an image from the file manager";
        ui_text(px, VW, VH, (VW - ui_text_width(UI_REG, 15, msg)) / 2, VH / 2 + 30, msg, UI_REG, 15, UI_MUTED);
        if (errmsg[0]) ui_text(px, VW, VH, (VW - ui_text_width(UI_REG, 12, errmsg)) / 2, VH / 2 + 54, errmsg, UI_REG, 12, 0x8a6670);
    } else {
        int avail_h = VH - 56;
        int dw = VW - 40, dh = ih * dw / iw;                      /* scale to fit, keep the aspect ratio */
        if (dh > avail_h - 24) { dh = avail_h - 24; dw = iw * dh / ih; }
        if (dw > iw && dh > ih) { dw = iw; dh = ih; }          /* never enlarge */
        int ox = (VW - dw) / 2, oy = (avail_h - dh) / 2;
        ui_rrect(px, VW, VH, ox - 1, oy - 1, dw + 2, dh + 2, 6, 0x2b3142);           /* a hairline frame */
        for (int y = 0; y < dh; y++) {
            int sy = y * ih / dh;
            for (int x = 0; x < dw; x++) {
                int sx = x * iw / dw;
                const unsigned char *p = img + ((size_t)sy * iw + sx) * 3;
                px[(oy + y) * VW + ox + x] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
            }
        }
    }
    /* a floating info pill at the bottom */
    char info[200];
    snprintf(info, sizeof info, "%s", cur_name);
    char dims[60]; snprintf(dims, sizeof dims, "%d x %d   %d / %d", iw, ih, nfiles ? cur + 1 : 0, nfiles);
    int w1 = ui_text_width(UI_BOLD, 13, info), w2 = ui_text_width(UI_REG, 12, dims), pw = w1 + w2 + 56;
    ui_card(px, VW, VH, (VW - pw) / 2, VH - 46, pw, 32, 16, 0x1a1f2c, 0x303850);
    ui_text(px, VW, VH, (VW - pw) / 2 + 18, VH - 25, info, UI_BOLD, 13, UI_TEXT);
    ui_text(px, VW, VH, (VW - pw) / 2 + 18 + w1 + 20, VH - 25, dims, UI_REG, 12, UI_MUTED);
    commit();
}
static void open_file(const char *name_in) {
    char name[96]; snprintf(name, sizeof name, "%s", name_in);          /* name_in may be cur_name itself */
    free(img); img = 0; iw = ih = 0; errmsg[0] = 0;
    snprintf(cur_name, sizeof cur_name, "%s", name);
    char path[700]; snprintf(path, sizeof path, "%s/%s", dir, name);
    size_t l = strlen(name);
    int ok = (l > 4 && !strcasecmp(name + l - 4, ".png")) ? load_png(path) : load_jpeg(path);
    if (!ok && !errmsg[0]) snprintf(errmsg, sizeof errmsg, "load failed: %s", path);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        char *slash = strrchr(argv[1], '/');
        if (slash) { snprintf(dir, sizeof dir, "%.*s", (int)(slash - argv[1]) ? (int)(slash - argv[1]) : 1, argv[1]); snprintf(cur_name, sizeof cur_name, "%s", slash + 1); }
        else { snprintf(dir, sizeof dir, "."); snprintf(cur_name, sizeof cur_name, "%s", argv[1]); }
        list_dir();
        for (int i = 0; i < nfiles; i++) if (!strcmp(files[i], cur_name)) cur = i;
    }
    int key = 0x5000 + getpid();
    int id = shmget(key, VW * VH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1 || gfx_load_font() != 0 || ui_init() != 0) { puts("view FAIL: shm/font"); return 1; }
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(sock, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("view FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, VW, VH, (uint32_t)key, 0 };
    (void)!write(sock, &m, sizeof m);
    if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("view FAIL: no CREATED"); return 1; }
    shmctl(id, IPC_RMID, 0);
    struct wl_msg t = { WL_TITLE, 0, 0, 0, 0 };
    memcpy(&t.a, "Image", 5);
    (void)!write(sock, &t, sizeof t);
    if (argc > 1) open_file(cur_name);
    show();
    printf("view ready %dx%d %s\n", iw, ih, errmsg);
    fflush(stdout);
    while (read(sock, &m, sizeof m) == (ssize_t)sizeof m) {
        if (m.op == WL_CLOSE) break;
        if (m.op != WL_KEY || !m.b) continue;
        if (m.a == 0x29) break;
        if (nfiles > 1 && (m.a == 0x4F || m.a == 0x50)) {
            cur = (cur + (m.a == 0x4F ? 1 : nfiles - 1)) % nfiles;
            open_file(files[cur]);
            show();
        }
    }
    return 0;
}
