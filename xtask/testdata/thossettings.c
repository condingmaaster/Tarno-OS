// SPDX-License-Identifier: GPL-2.0-or-later
// THOS settings (a legacy-protocol desktop client): a sidebar of pages (About, Network, Storage, Display) with live values
// read from /proc. Built only from the UI toolkit: gliding sidebar pills (ui_list), cards, usage bars, a short slide+fade when the page changes.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "thoswl.h"
#include "thosresize.h"
#include "thosgfx.h"
#include "thosui.h"

static int WW=760;
static int WH=500;
#define SIDE 200
#define ROWH 44
static uint32_t *px;
static int sock, scr_w, scr_h;
static struct thos_resize resize;
static struct ui_list list;
static struct tween page_t;
static int page, hover_row = -1;
static int net_mode, net_manual, net_focus;
static char net_fields[4][32] = { "", "24", "0.0.0.0", "0.0.0.0" };
static char net_message[128];
static const char *names[] = { "About", "Network", "Storage", "Display", "Appearance", "Development", "Power & sound" };
static const uint32_t icons[] = { IC_INFO, 0xf0ac, IC_ARCHIVE, IC_DESKTOP, 0xf1fc, IC_TERMINAL, 0xf011 };
#define NPAGES 7

static int slurp(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) { buf[0] = 0; return 0; }
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return (int)n;
}
/* the value after `key` on its line ("model name\t: Intel ..." -> "Intel ..."), or "" */
static void kv(const char *text, const char *key, char *out, size_t cap) {
    const char *p = text;
    out[0] = 0;
    while (*p && (strncmp(p, key, strlen(key)) || (p[strlen(key)] != ' ' && p[strlen(key)] != '\t' && p[strlen(key)] != ':'))) {
        p = strchr(p, '\n'); if (!p) return; p++;
    }
    if (!*p) return;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    size_t n = 0;
    while (p[n] && p[n] != '\n' && n + 1 < cap) { out[n] = p[n]; n++; }
    out[n] = 0;
}

static void row(int x, int y, int w, const char *label, const char *value, int last) {
    ui_text(px, WW, WH, x + 18, y + 27, label, UI_REG, 13, UI_MUTED);
    char v[120]; snprintf(v, sizeof v, "%s", value);
    while (ui_text_width(UI_REG, 13, v) > w - 160 && strlen(v) > 4) v[strlen(v) - 1] = 0;
    ui_text(px, WW, WH, x + w - 18 - ui_text_width(UI_REG, 13, v), y + 27, v, UI_REG, 13, UI_TEXT);
    if (!last) for (int i = x + 18; i < x + w - 18; i++) px[(y + ROWH - 1) * WW + i] = 0x232a3b;
}
static void card(int x, int y, int w, int rows) { ui_card(px, WW, WH, x, y, w, rows * ROWH + 4, 14, 0x1b2030, 0x262d40); }
static const char *firmware_label(const char *value) {
    if (!strcmp(value, "validated")) return "Available, not loaded";
    if (!strcmp(value, "missing")) return "Missing files";
    if (!strcmp(value, "invalid")) return "File check failed";
    return "Not checked";
}

static void net_button(int x, int y, int w, const char *label, int active) {
    ui_rrect(px, WW, WH, x, y, w, 34, 9, active ? 0x283b63 : 0x232b40);
    ui_text(px, WW, WH, x + 12, y + 23, label, UI_REG, 13, UI_TEXT);
}
static void net_request(const char *cmd) {
    int fd = open("/dev/netctl", O_WRONLY);
    ssize_t n = fd >= 0 ? write(fd, cmd, strlen(cmd)) : -1;
    int e = errno; if (fd >= 0) close(fd);
    if (n == (ssize_t)strlen(cmd)) snprintf(net_message, sizeof net_message, "%s", !strcmp(cmd,"dhcp") ? "DHCP requested; waiting for a real lease." : !strcmp(cmd,"disconnect") ? "Ethernet disconnected." : !strcmp(cmd,"wifi-recheck") ? "Hardware check refreshed; see status below." : "Manual IPv4 settings applied for this session.");
    else snprintf(net_message,sizeof net_message,"%s",e == ENODEV ? "No Ethernet adapter available." : e == EACCES ? "Only the administrator can change networking." : e == EINVAL ? "Invalid IPv4 settings. Check address, prefix and gateway." : "Network action failed.");
}
static void net_apply(void) {
    char cmd[160]; snprintf(cmd,sizeof cmd,"static %s %s %s %s",net_fields[0],net_fields[1],net_fields[2],net_fields[3]); net_request(cmd);
}
static void page_net_setup(int x, int y, int w) {
    net_button(x + w - 74, y + 3, 74, "Back", 0);
    ui_text(px,WW,WH,x,y+28,net_mode == 1 ? "Ethernet settings" : "Wi-Fi settings",UI_BOLD,22,UI_TEXT);
    if (net_mode == 1) {
        net_button(x,y+52,238,"Automatic (DHCP)",!net_manual);
        net_button(x+250,y+52,w-250,"Manual IPv4",net_manual);
        if (net_manual) {
            const char *labels[] = {"IP address","Prefix length (1–30)","Gateway (0.0.0.0 for none)","DNS server (0.0.0.0 for none)"};
            for (int i=0;i<4;i++) {
                int fy=y+110+i*58;
                ui_text(px,WW,WH,x,fy,labels[i],UI_REG,12,UI_MUTED);
                ui_rrect(px,WW,WH,x,fy+7,w,33,8,i==net_focus ? 0x283b63 : 0x1b2030);
                ui_text(px,WW,WH,x+12,fy+30,net_fields[i],UI_REG,14,UI_TEXT);
            }
            net_button(x,y+350,180,"Apply IPv4 settings",1);
        } else {
            ui_text(px,WW,WH,x,y+122,"Obtain an address, gateway and DNS from your router.",UI_REG,13,UI_TEXT);
            ui_text(px,WW,WH,x,y+148,"A cable/link and an actual DHCP reply are required.",UI_REG,12,UI_MUTED);
            net_button(x,y+182,238,"Request DHCP lease",1);
            net_button(x,y+232,238,"Disconnect Ethernet",0);
        }
        ui_text(px,WW,WH,x,y+410,net_message,UI_REG,12,UI_MUTED);
    } else {
        char wifi[640],adapter[64],state[32],fw[24],detail[128];
        slurp("/proc/wlaninfo",wifi,sizeof wifi); kv(wifi,"adapter",adapter,sizeof adapter); kv(wifi,"state",state,sizeof state); kv(wifi,"firmware",fw,sizeof fw); kv(wifi,"detail",detail,sizeof detail);
        card(x,y+52,w,3);
        row(x,y+54,w,"Adapter",!strcmp(adapter,"none") ? "Not detected" : adapter,0);
        row(x,y+54+ROWH,w,"Driver state",state,0);
        row(x,y+54+2*ROWH,w,"Firmware",firmware_label(fw),1);
        ui_text(px,WW,WH,x,y+215,"Hardware check",UI_BOLD,14,UI_TEXT);
        ui_text(px,WW,WH,x,y+241,detail,UI_REG,12,UI_MUTED);
        ui_text(px,WW,WH,x,y+280,"Wi-Fi scan and connection support is not implemented yet.",UI_REG,13,UI_TEXT);
        ui_text(px,WW,WH,x,y+305,"Recognizing the adapter does not make a Wi-Fi connection.",UI_REG,12,UI_MUTED);
        net_button(x,y+337,238,"Recheck hardware",0);
        ui_text(px,WW,WH,x,y+405,net_message,UI_REG,12,UI_MUTED);
    }
}
static void net_click(int mx,int my) {
    int x=SIDE+28,y=14,w=WW-SIDE-56;
    if (!net_mode) { if(my>=452 && my<486 && mx>=x && mx<x+w) { net_mode=mx<x+250 ? 1 : 2; net_message[0]=0; } return; }
    if(mx>=x+w-74 && my>=y+3 && my<y+37) {net_mode=0; return;}
    if(net_mode==2) {if(mx>=x && mx<x+238 && my>=y+337 && my<y+371) net_request("wifi-recheck"); return;}
    if(my>=y+52 && my<y+86) {net_manual=mx>=x+250; return;}
    if(net_manual) {
        for(int i=0;i<4;i++) if(my>=y+117+i*58 && my<y+150+i*58) net_focus=i;
        if(mx>=x && mx<x+180 && my>=y+350 && my<y+384) net_apply();
    } else if(mx>=x && mx<x+238) {
        if(my>=y+182 && my<y+216) net_request("dhcp");
        if(my>=y+232 && my<y+266) net_request("disconnect");
    }
}

static void page_about(int x, int y, int w) {
    char cpu[512], mem[512], up[64], host[64], model[120], cores[16] = "1";
    slurp("/proc/cpuinfo", cpu, sizeof cpu); slurp("/proc/meminfo", mem, sizeof mem); slurp("/proc/uptime", up, sizeof up);
    kv(cpu, "model name", model, sizeof model);
    int nc = 0; for (const char *p = cpu; (p = strstr(p, "processor")); p++) nc++;
    if (nc) snprintf(cores, sizeof cores, "%d", nc);
    if (gethostname(host, sizeof host) != 0) snprintf(host, sizeof host, "thos");
    char t[64]; kv(mem, "MemTotal", t, sizeof t); long total = atol(t);
    kv(mem, "MemFree", t, sizeof t); long fr = atol(t);
    long secs = atol(up);
    ui_text(px, WW, WH, x, y + 28, "THOS", UI_BOLD, 28, UI_TEXT);
    ui_text(px, WW, WH, x + 106, y + 28, "a hybrid kernel with Linux and Windows personalities", UI_REG, 13, UI_MUTED);
    y += 52;
    char bd[16]; slurp("/proc/bootdisk", bd, sizeof bd);
    card(x, y, w, 6);
    char b[120];
    row(x, y + 2, w, "Device name", host, 0);
    row(x, y + 2 + ROWH, w, "Processor", model[0] ? model : "unknown", 0);
    snprintf(b, sizeof b, "%s", cores); row(x, y + 2 + 2 * ROWH, w, "Cores", b, 0);
    snprintf(b, sizeof b, "%ld MB total, %ld MB in use", total / 1024, (total - fr) / 1024); row(x, y + 2 + 3 * ROWH, w, "Memory", b, 0);
    snprintf(b, sizeof b, "%ld h %02ld min", secs / 3600, secs / 60 % 60); row(x, y + 2 + 4 * ROWH, w, "Running for", b, 0);
    row(x, y + 2 + 5 * ROWH, w, "Started from", !strncmp(bd, "usb", 3) ? "USB stick (Install THOS is in the Start menu)" : "Internal disk", 1);
    y += 6 * ROWH + 20;
    ui_text(px, WW, WH, x + 4, y + 8, "Memory use", UI_REG, 12, UI_MUTED);
    ui_bar(px, WW, WH, x, y + 18, w, 10, total ? (float)(total - fr) / (float)total : 0, UI_ACCENT);
}
static void page_net(int x, int y, int w) {
    if(net_mode) { page_net_setup(x,y,w); return; }
    char n[384], link[32], ip[32], dns[96], state_net[32], iface[32], mode[16];
    slurp("/proc/netinfo", n, sizeof n);
    kv(n, "link", link, sizeof link); kv(n, "ip", ip, sizeof ip); kv(n, "dns", dns, sizeof dns);
    kv(n,"state",state_net,sizeof state_net); kv(n,"interface",iface,sizeof iface); kv(n,"address_mode",mode,sizeof mode);
    int up = !strcmp(link, "up");
    ui_text(px, WW, WH, x, y + 28, "Network", UI_BOLD, 22, UI_TEXT);
    y += 48;
    ui_card(px, WW, WH, x, y, w, 64, 14, 0x1b2030, 0x262d40);
    ui_disc(px, WW, WH, x + 34, y + 32, 6, up ? UI_GOOD : UI_BAD);
    ui_text(px, WW, WH, x + 54, y + 29, up ? "Connected" : "Not connected", UI_BOLD, 15, UI_TEXT);
    const char *status = up ? (!strcmp(mode,"dhcp") ? "Ethernet, address from DHCP" : "Ethernet, manual IPv4 address") : !strcmp(state_net,"no-adapter") ? "No Ethernet adapter available" : !strcmp(state_net,"disabled") ? "Ethernet disabled in settings" : !strcmp(state_net,"no-carrier") ? "No cable/link detected" : "Waiting for a DHCP lease";
    ui_text(px, WW, WH, x + 54, y + 49, status, UI_REG, 12, UI_MUTED);
    y += 80;
    card(x, y, w, 3);
    row(x, y + 2, w, "IP address", up ? ip : "none", 0);
    row(x, y + 2 + ROWH, w, "Name servers", up && dns[0] ? dns : "none", 0);
    row(x, y + 2 + 2 * ROWH, w, "Interface", iface[0] ? iface : "none", 1);
    y += 3 * ROWH + 22;
    char wifi[640], present[8], state[32], adapter[64], firmware[24], core_state[24];
    slurp("/proc/wlaninfo", wifi, sizeof wifi);
    kv(wifi, "present", present, sizeof present);
    kv(wifi, "state", state, sizeof state);
    kv(wifi, "adapter", adapter, sizeof adapter);
    kv(wifi, "firmware", firmware, sizeof firmware);
    kv(wifi, "core_state", core_state, sizeof core_state);
    card(x, y, w, 3);
    row(x, y + 2, w, "Wi-Fi adapter", !strcmp(present, "1") ? adapter : "Not detected", 0);
    row(x, y + 2 + ROWH, w, "Wi-Fi status", !strcmp(present, "1")
        ? (!strcmp(state, "bcma-ready") ? (!strcmp(core_state, "reset") ? "Awaiting initialization" : "Connection support pending") : "Hardware check failed")
        : "No adapter available", 0);
    row(x, y + 2 + 2 * ROWH, w, "Wi-Fi firmware", firmware_label(firmware), 1);
    net_button(x,452,238,"Ethernet settings",0);
    net_button(x+250,452,w-250,"Wi-Fi settings",0);
}
static void page_storage(int x, int y, int w) {
    struct statvfs sv;
    long total = 0, fr = 0;
    if (statvfs("/", &sv) == 0 && sv.f_blocks) { total = (long)((unsigned long long)sv.f_blocks * sv.f_frsize / 1048576); fr = (long)((unsigned long long)sv.f_bfree * sv.f_frsize / 1048576); }
    ui_text(px, WW, WH, x, y + 28, "Storage", UI_BOLD, 22, UI_TEXT);
    y += 48;
    ui_card(px, WW, WH, x, y, w, 96, 14, 0x1b2030, 0x262d40);
    ui_icon(px, WW, WH, x + 36, y + 34, IC_ARCHIVE, 20, UI_ACCENT_HI);
    ui_text(px, WW, WH, x + 66, y + 30, "THOS system disk", UI_BOLD, 15, UI_TEXT);
    char b[100];
    if (total) snprintf(b, sizeof b, "%ld MB used of %ld MB, %ld MB free", total - fr, total, fr); else snprintf(b, sizeof b, "size unknown");
    ui_text(px, WW, WH, x + 66, y + 50, b, UI_REG, 12, UI_MUTED);
    ui_bar(px, WW, WH, x + 18, y + 66, w - 36, 10, total ? (float)(total - fr) / (float)total : 0, total && fr * 10 < total ? UI_WARN : UI_ACCENT);
    y += 116;
    struct stat st1, st2;                                           /* the raw disks the kernel found (see /dev/sda, /dev/sdb) */
    int h1 = stat("/dev/sda", &st1) == 0, h2 = stat("/dev/sdb", &st2) == 0;
    char bd[16]; slurp("/proc/bootdisk", bd, sizeof bd);
    int usb = !strncmp(bd, "usb", 3);
    if (h1 || h2) {
        card(x, y, w, (h1 && h2) ? 2 : 1);
        int r = 0;
        if (h1) { char v[80]; snprintf(v, sizeof v, "%llu GB%s", (unsigned long long)st1.st_size / 1000000000ULL, usb ? "" : " (running from it)"); row(x, y + 2 + r * ROWH, w, "Internal disk", v, !h2); r++; }
        if (h2) { char v[80]; snprintf(v, sizeof v, "%llu GB%s", (unsigned long long)st2.st_size / 1000000000ULL, usb ? " (running from it)" : ""); row(x, y + 2 + r * ROWH, w, "USB stick", v, 1); }
    }
}
static void page_display(int x, int y, int w) {
    ui_text(px, WW, WH, x, y + 28, "Display", UI_BOLD, 22, UI_TEXT);
    y += 48;
    card(x, y, w, 7);
    char b[64], gpu[384], adapter[64], memory[24], acceleration[8], firmware[24], wc[8];
    slurp("/proc/gpuinfo", gpu, sizeof gpu);
    kv(gpu, "adapter", adapter, sizeof adapter);
    kv(gpu, "vram_mb", memory, sizeof memory);
    kv(gpu, "acceleration", acceleration, sizeof acceleration);
    kv(gpu, "firmware", firmware, sizeof firmware);
    kv(gpu, "framebuffer_wc_configured", wc, sizeof wc);
    snprintf(b, sizeof b, "%d x %d", scr_w, scr_h); row(x, y + 2, w, "Resolution", b, 0);
    row(x, y + 2 + ROWH, w, "Colour", "32 bit", 0);
    row(x, y + 2 + 2 * ROWH, w, "Graphics adapter", adapter[0] ? adapter : "Boot framebuffer", 0);
    snprintf(b, sizeof b, "%ld MB", atol(memory));
    row(x, y + 2 + 3 * ROWH, w, "Graphics memory", atol(memory) > 0 ? b : "Not reported", 0);
    row(x, y + 2 + 4 * ROWH, w, "Hardware acceleration", !strcmp(acceleration, "1") ? "Active" : "Not available yet", 0);
    row(x, y + 2 + 5 * ROWH, w, "GPU firmware", firmware_label(firmware), 0);
    row(x, y + 2 + 6 * ROWH, w, "Framebuffer writes", !strcmp(wc, "1") ? "Write combining configured" : "Firmware default", 1);
}

/* ---- appearance: accent swatches and wallpaper presets; a click writes ~/.thos-theme and tells the compositor (WL_THEME) ---- */
static const uint32_t swatch[6] = { 0x5b8def, 0x8b5cf6, 0x14b8a6, 0x22c55e, 0xf59e0b, 0xec4899 };
static const char *swatch_name[6] = { "Blue", "Violet", "Teal", "Green", "Amber", "Pink" };
static const uint32_t wp_a[4] = { 0x1a2146, 0x3a2038, 0x123a3a, 0x24262c }, wp_b[4] = { 0x3b4a8c, 0x9a5a4a, 0x2f8a78, 0x4a4e5a };
static const char *wp_name[4] = { "Aurora", "Dusk", "Forest", "Graphite" };
static int cur_swatch(void) { for (int i = 0; i < 6; i++) if (swatch[i] == (ui_accent & 0xffffff)) return i; return -1; }
static void apply_theme(uint32_t accent, int wall) {
    const char *h = getenv("HOME");
    char path[220];
    snprintf(path, sizeof path, "%s/.thos-theme", h && h[0] == '/' && h[1] ? h : "/tmp");
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "accent=%06x\nwallpaper=%d\n", accent & 0xffffff, wall); fclose(f); }
    ui_theme_load();
    uint32_t m[5] = { WL_THEME, 0, 0, 0, 0 };
    (void)!write(sock, m, sizeof m);
}
static int sw_x(int i, int x) { return x + 18 + i * 56; }
static void page_appear(int x, int y, int w) {
    ui_text(px, WW, WH, x, y + 28, "Appearance", UI_BOLD, 22, UI_TEXT);
    y += 48;
    ui_card(px, WW, WH, x, y, w, 120, 14, 0x1b2030, 0x262d40);
    ui_text(px, WW, WH, x + 18, y + 30, "Accent colour", UI_BOLD, 14, UI_TEXT);
    ui_text(px, WW, WH, x + 18, y + 48, swatch_name[cur_swatch() < 0 ? 0 : cur_swatch()], UI_REG, 12, UI_MUTED);
    for (int i = 0; i < 6; i++) {
        int cx = sw_x(i, x) + 18, cy = y + 84;
        if (i == cur_swatch()) ui_disc(px, WW, WH, cx, cy, 20, 0xe8ebf4);
        ui_disc(px, WW, WH, cx, cy, i == cur_swatch() ? 17 : 16, swatch[i]);
        if (i == cur_swatch()) ui_icon(px, WW, WH, cx, cy, 0xf00c, 13, 0xffffffu);
    }
    y += 140;
    ui_card(px, WW, WH, x, y, w, 150, 14, 0x1b2030, 0x262d40);
    ui_text(px, WW, WH, x + 18, y + 30, "Wallpaper", UI_BOLD, 14, UI_TEXT);
    for (int i = 0; i < 4; i++) {
        int tx = x + 18 + i * 116, ty = y + 44;
        if (i == ui_wall_theme) ui_rrect(px, WW, WH, tx - 3, ty - 3, 116, 74, 12, UI_ACCENT_HI);
        ui_rrect_g(px, WW, WH, tx, ty, 110, 68, 10, wp_a[i], wp_b[i], 255);
        ui_text(px, WW, WH, tx + 55 - ui_text_width(UI_REG, 12, wp_name[i]) / 2, ty + 88, wp_name[i], i == ui_wall_theme ? UI_BOLD : UI_REG, 12, i == ui_wall_theme ? UI_TEXT : UI_MUTED);
    }
}
static void appear_click(int mxp, int myp) {
    int x = SIDE + 28, y = 14 + 48;
    for (int i = 0; i < 6; i++) { int cx = sw_x(i, x) + 18, cy = y + 84; if ((mxp - cx) * (mxp - cx) + (myp - cy) * (myp - cy) <= 22 * 22) { apply_theme(swatch[i], ui_wall_theme); return; } }
    y += 140;
    for (int i = 0; i < 4; i++) { int tx = x + 18 + i * 116, ty = y + 44; if (mxp >= tx && mxp < tx + 110 && myp >= ty && myp < ty + 68) { apply_theme(ui_accent, i); return; } }
}
static const char *jdk_home(void) {
    static const char *homes[]={"/usr/lib/jvm/java-21-openjdk-amd64","/opt/root/usr/lib/jvm/java-21-openjdk-amd64"};
    char path[200];
    for(int compiler=1;compiler>=0;compiler--) for(unsigned i=0;i<2;i++) {
        snprintf(path,sizeof path,"%s/bin/%s",homes[i],compiler?"javac":"java");
        if(access(path,X_OK)==0)return homes[i];
    }
    return homes[0];
}
static int jdk_available(const char *tool) {
    char path[200];snprintf(path,sizeof path,"%s/bin/%s",jdk_home(),tool);
    return access(path,X_OK)==0;
}
static void page_development(int x,int y,int w) {
    ui_text(px,WW,WH,x,y+28,"Development",UI_BOLD,22,UI_TEXT);
    y+=48;card(x,y,w,3);
    row(x,y,w,"Java runtime",jdk_available("java")?"OpenJDK 21 available":"Not installed",0);
    row(x,y+ROWH,w,"Java compiler",jdk_available("javac")?"Available":"Not installed",0);
    row(x,y+2*ROWH,w,"Background service","None required",1);
    y+=3*ROWH+24;
    net_button(x,y,180,"JDK terminal",jdk_available("java"));
    net_button(x+196,y,180,"Compiler version",jdk_available("javac"));
    net_button(x,y+50,180,"Open Software",1);
    net_button(x+196,y+50,180,"Admin terminal",access("/bin/sudo",X_OK)==0);
    ui_text(px,WW,WH,x,y+112,"The JDK terminal sets JAVA_HOME and PATH for this session.",UI_REG,12,UI_MUTED);
    ui_text(px,WW,WH,x,y+136,"Installed tools run on demand; they do not start a background service.",UI_REG,12,UI_MUTED);
}
static void development_click(int mx,int my) {
    int x=SIDE+28,y=14+48+3*ROWH+24;
    if(mx<x||mx>=x+376)return;
    char text[600];const char *command=0;
    if(my>=y&&my<y+34) {
        if(mx<x+180&&jdk_available("java")) {snprintf(text,sizeof text,"export JAVA_HOME=%s; export PATH=$JAVA_HOME/bin:$PATH; echo 'JDK environment ready'; exec /busybox sh -i",jdk_home());command=text;}
        else if(mx>=x+196&&jdk_available("javac")) {snprintf(text,sizeof text,"%s/bin/javac -J-Xshare:off -J-XX:+UseSerialGC -J-Xmx96m -version",jdk_home());command=text;}
    } else if(my>=y+50&&my<y+84&&mx<x+180) {
        if(fork()==0) {close(sock);execl("/thossoft","thossoft",(char*)0);_exit(127);}return;
    } else if(my>=y+50&&my<y+84&&mx>=x+196&&access("/bin/sudo",X_OK)==0) {
        command="/bin/sudo -s";
    }
    if(command&&fork()==0) {close(sock);execl("/thosterm","thosterm","-l","-c",command,(char*)0);_exit(127);}
}
/* ---- power and sound: battery / AC / temperatures from /proc/power (what the firmware's AML reports), brightness and volume sliders ---- */
#define THOS_BASE 0x54480000L
static long thos2(long n, long a, long b) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(n), "D"(a), "S"(b) : "rcx", "r11", "memory");
    return r;
}
static int slider_y[2];                                      /* top of the brightness and volume tracks (0: not shown) */
static void slider(int x, int y, int w, const char *label, int pct) {
    ui_text(px, WW, WH, x + 4, y + 14, label, UI_REG, 13, UI_MUTED);
    char v[8]; snprintf(v, sizeof v, "%d %%", pct);
    ui_text(px, WW, WH, x + w - 4 - ui_text_width(UI_REG, 13, v), y + 14, v, UI_REG, 13, UI_TEXT);
    ui_bar(px, WW, WH, x, y + 26, w, 10, (float)pct / 100.0f, UI_ACCENT);
}
static void page_power(int x, int y, int w) {
    ui_text(px, WW, WH, x, y + 28, "Power & sound", UI_BOLD, 22, UI_TEXT);
    y += 48;
    char pw[640], v[96], b[120];
    slurp("/proc/power", pw, sizeof pw);
    int rows = 0;
    char st[24], pc[16], ac[16], lid[16], cpu[16], tz[16], full[24], rem[24], unit[8];
    kv(pw, "bat0.state", st, sizeof st); kv(pw, "bat0.percent", pc, sizeof pc); kv(pw, "ac0", ac, sizeof ac);
    kv(pw, "lid", lid, sizeof lid); kv(pw, "cputemp", cpu, sizeof cpu); kv(pw, "thermal0", tz, sizeof tz);
    kv(pw, "bat0.full", full, sizeof full); kv(pw, "bat0.remaining", rem, sizeof rem); kv(pw, "bat0.unit", unit, sizeof unit);
    int have_bat = pc[0] != 0, have_ac = ac[0] != 0;
    rows = have_bat + have_ac + (cpu[0] != 0) + (tz[0] != 0) + (lid[0] != 0);
    if (rows == 0) {
        card(x, y, w, 1);
        row(x, y, w, "Sensors", "The firmware reports none (or the ACPI interpreter is off)", 1);
        y += ROWH + 24;
    } else {
        card(x, y, w, rows);
        int r = 0;
        if (have_bat) { snprintf(b, sizeof b, "%s %% (%s)", pc, st[0] ? st : "?"); row(x, y + 2 + r * ROWH, w, "Battery", b, r == rows - 1); r++; }
        if (have_ac) { row(x, y + 2 + r * ROWH, w, "Power source", !strcmp(ac, "online") ? "Mains adapter" : "Battery", r == rows - 1); r++; }
        if (cpu[0]) { snprintf(v, sizeof v, "%s", cpu); row(x, y + 2 + r * ROWH, w, "Processor temperature", v, r == rows - 1); r++; }
        if (tz[0]) { snprintf(v, sizeof v, "%s", tz); row(x, y + 2 + r * ROWH, w, "Thermal zone", v, r == rows - 1); r++; }
        if (lid[0]) { row(x, y + 2 + r * ROWH, w, "Lid", lid, r == rows - 1); r++; }
        y += rows * ROWH + 24;
    }
    slider_y[0] = slider_y[1] = 0;
    long bl = thos2(THOS_BASE + 2, 0, 0), vol = thos2(THOS_BASE + 3, 0, 0);
    if (bl >= 0) { slider(x, y, w, "Screen brightness", (int)bl); slider_y[0] = y + 26; y += 56; }
    if (vol >= 0) { slider(x, y, w, "Volume", (int)vol); slider_y[1] = y + 26; y += 56; }
    if (bl < 0 && vol < 0) ui_text(px, WW, WH, x + 4, y + 14, "No adjustable brightness or sound output found.", UI_REG, 13, UI_MUTED);
}
static void power_click(int mxp, int myp) {
    int x = SIDE + 28, w = WW - SIDE - 56;
    for (int i = 0; i < 2; i++) {
        if (!slider_y[i] || myp < slider_y[i] - 14 || myp > slider_y[i] + 24) continue;
        long pct = ((long)(mxp - x) * 100 + w / 2) / w;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        thos2(THOS_BASE + 2 + i, 1, pct);
        return;
    }
}
static void render(void) {
    for (int i = 0; i < WW * WH; i++) px[i] = UI_SURFACE;
    for (int y = 0; y < WH; y++) for (int x = 0; x < SIDE; x++) px[y * WW + x] = 0x131722;
    for (int y = 0; y < WH; y++) px[y * WW + SIDE] = UI_LINE;
    ui_text(px, WW, WH, 22, 38, "Settings", UI_BOLD, 17, UI_TEXT);
    ui_list_pills(px, WW, WH, &list, 10, 62, SIDE - 20, 0x232b40, 0x1b2132, 10);
    for (int i = 0; i < NPAGES; i++) {
        int y = 62 + ui_list_row_y(&list, i);
        ui_icon(px, WW, WH, 34, y + 21, icons[i], 14, i == page ? UI_ACCENT_HI : UI_MUTED);
        ui_text(px, WW, WH, 56, y + 26, names[i], i == page ? UI_BOLD : UI_REG, 14, i == page ? UI_TEXT : 0xb8bfd4);
    }
    float t = tween_busy(&page_t) ? tween_value(&page_t, ease_out) : 1.0f;
    int cx = SIDE + 28, cw = WW - SIDE - 56, cy = 14 + (int)((1.0f - t) * 14);
    ui_set_clip(SIDE + 1, 0, WW, WH);
    switch (page) { case 0: page_about(cx, cy, cw); break; case 1: page_net(cx, cy, cw); break; case 2: page_storage(cx, cy, cw); break; case 3: page_display(cx, cy, cw); break; case 5: page_development(cx, cy, cw); break; case 6: page_power(cx, cy, cw); break; default: page_appear(cx, cy, cw); }
    if (t < 1.0f) ui_rrect_g(px, WW, WH, SIDE + 1, 0, WW - SIDE - 1, WH, 0, UI_SURFACE, UI_SURFACE, (unsigned)((1.0f - t) * 255));      /* the old page fades into the new one */
    ui_set_clip(0, 0, 0x7fffffff, 0x7fffffff);
    struct wl_msg c = { WL_COMMIT, 0, 0, WW, WH };
    (void)!write(sock, &c, sizeof c);
}
static void goto_page(int p) {
    if (p == page || p < 0 || p >= NPAGES) return;
    page = p;
    net_mode = 0;
    ui_list_select(&list, p);
    tween_start(&page_t, 0, 1, MO_BASE);
}

int main(void) {
    int key = 0x5000 + getpid();
    int id = shmget(key, WW * WH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1 || gfx_load_font() != 0 || ui_init() != 0) { puts("settings FAIL: shm/font"); return 1; }
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(sock, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("settings FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, WW, WH, (uint32_t)key, WL_RESIZABLE };
    (void)!write(sock, &m, sizeof m);
    if (ui_read(sock, &m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("settings FAIL: no CREATED"); return 1; }
    scr_w = (int)m.b; scr_h = (int)m.c;
    shmctl(id, IPC_RMID, 0);
    thos_resize_limits(sock,760,500);
    struct wl_msg t = { WL_TITLE, 0, 0, 0, 0 };
    memcpy(&t.a, "Settings", 8);
    (void)!write(sock, &t, sizeof t);
    ui_list_init(&list, ROWH, WH - 62);
    ui_list_reset(&list, NPAGES);
    const char *p0 = getenv("SETTINGS_PAGE");
    if (p0) { page = atoi(p0) % NPAGES; ui_list_select(&list, page); spring_init(&list.selp, (float)(page * ROWH), SP_SNAPPY); }
    render();
    printf("settings ready\n");
    fflush(stdout);
    long long clk = mo_now_ms(), last_data = clk;
    for (;;) {
        int busy = ui_list_busy(&list) || tween_busy(&page_t);
        int pr = ui_wait(sock, busy);
        long long now = mo_now_ms();
        float dt = (float)(now - clk) / 1000.0f;
        clk = now;
        if (busy) { ui_list_step(&list, dt); render(); }
        else if (now - last_data > 2000) { last_data = now; render(); }        /* live values */
        if (pr <= 0) continue;
        if (ui_read(sock, &m) != (ssize_t)sizeof m || m.op == WL_CLOSE) break;
        int resized=thos_resize_event(sock,&m,&resize,&px,&WW,&WH);
        if(resized) {
            if(resized==2) {list.view_h=WH-62;ui_list_clamp(&list);render();thos_resize_continue(sock,&resize);}
            continue;
        }
        if (m.op == WL_THEME) { ui_theme_load(); render(); continue; }
        if (m.op == WL_POINTER && m.c == 1) {
            int i = (int)m.a < SIDE ? ui_list_at(&list, (int)m.b - 62) : -1;
            if (i >= 0) goto_page(i);
            else if ((int)m.a > SIDE && page == 1) net_click((int)m.a,(int)m.b);
            else if ((int)m.a > SIDE && page == 4) appear_click((int)m.a, (int)m.b);
            else if ((int)m.a > SIDE && page == 5) development_click((int)m.a,(int)m.b);
            else if ((int)m.a > SIDE && page == 6) power_click((int)m.a, (int)m.b);
            render();
        } else if (m.op == WL_POINTER && m.c != 3) {
            int rw = m.c == 0 && (int)m.a < SIDE && (int)m.b >= 62 ? ui_list_at(&list, (int)m.b - 62) : -1;
            ui_list_hover(&list, rw);
            ui_cursor(sock, rw >= 0 ? 2 : 0);
        } else if (m.op == WL_KEY && m.b) {
            if(page == 1 && net_mode) {
                if(m.a==0x29) net_mode=0;
                else if(net_mode==1 && net_manual) {
                    size_t len=strlen(net_fields[net_focus]);
                    if(m.a==0x2b) net_focus=(net_focus+1)%4;
                    else if(m.a==0x2a && len) net_fields[net_focus][len-1]=0;
                    else if(m.a==0x28) net_apply();
                    else if(len+1<sizeof net_fields[0] && ((m.c>='0' && m.c<='9') || (m.c=='.' && net_focus!=1))) {net_fields[net_focus][len]=(char)m.c; net_fields[net_focus][len+1]=0;}
                }
            } else if (m.a == 0x52) goto_page(page - 1);
            else if (m.a == 0x51) goto_page(page + 1);
            render();
        }
    }
    printf("settings done\n");
    thos_resize_dispose(&resize);
    return 0;
}
