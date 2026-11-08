// SPDX-License-Identifier: GPL-2.0-or-later
// Flat C entry points over lai (the AML interpreter) so the Rust side never touches lai's structures.
#include <lai/core.h>
#include <lai/helpers/sci.h>
#include <lai/helpers/pm.h>
#include <lai/internal-ns.h>
#include <lai/internal-exec.h>
#include "libc.h"

#include <lai/drivers/ec.h>

static int started;

// Embedded controller: lai's own driver polls without a timeout and insists on burst mode (some firmware has neither the bit nor the
// patience), so the transfers here are bounded and plain read/write/data commands. A dead EC reads as 0xFF, never as a hang.
static struct lai_ec_driver ecs[2];
static int ec_count;
static int ec_fail;
static lai_nsnode_t *ec_node[2];

static int ec_wait(struct lai_ec_driver *d, uint8_t mask, int want) {
    for (int i = 0; i < 200000; i++) {
        if (((laihost_inb(d->cmd_port) & mask) != 0) == want) return 0;
    }
    ec_fail++;
    return -1;
}
static uint8_t ec_rd(struct lai_ec_driver *d, uint8_t off) {
    if (!d->cmd_port || ec_wait(d, 2, 0)) return 0xFF;
    laihost_outb(d->cmd_port, 0x80);
    if (ec_wait(d, 2, 0)) return 0xFF;
    laihost_outb(d->data_port, off);
    if (ec_wait(d, 1, 1)) return 0xFF;
    return laihost_inb(d->data_port);
}
static void ec_wr(struct lai_ec_driver *d, uint8_t off, uint8_t v) {
    if (!d->cmd_port || ec_wait(d, 2, 0)) return;
    laihost_outb(d->cmd_port, 0x81);
    if (ec_wait(d, 2, 0)) return;
    laihost_outb(d->data_port, off);
    if (ec_wait(d, 2, 0)) return;
    laihost_outb(d->data_port, v);
}
static uint8_t ov_rb(uint64_t o, void *u) { return ec_rd(u, o); }
static uint16_t ov_rw(uint64_t o, void *u) { return ec_rd(u, o) | (ec_rd(u, o + 1) << 8); }
static uint32_t ov_rd(uint64_t o, void *u) { return ov_rw(o, u) | ((uint32_t)ov_rw(o + 2, u) << 16); }
static uint64_t ov_rq(uint64_t o, void *u) { return ov_rd(o, u) | ((uint64_t)ov_rd(o + 4, u) << 32); }
static void ov_wb(uint64_t o, uint8_t v, void *u) { ec_wr(u, o, v); }
static void ov_ww(uint64_t o, uint16_t v, void *u) { ec_wr(u, o, v); ec_wr(u, o + 1, v >> 8); }
static void ov_wd(uint64_t o, uint32_t v, void *u) { ov_ww(o, v, u); ov_ww(o + 2, v >> 16, u); }
static void ov_wq(uint64_t o, uint64_t v, void *u) { ov_wd(o, v, u); ov_wd(o + 4, v >> 32, u); }
static const struct lai_opregion_override ec_override = {ov_rb, ov_rw, ov_rd, ov_rq, ov_wb, ov_ww, ov_wd, ov_wq};

static int pnp_match(lai_nsnode_t *dev, const char *pnp, lai_state_t *st);

// Give every EmbeddedControl operation region the bounded driver, then tell the firmware the handler exists (_REG(3, 1)).
static void init_embedded_controller(void) {
    lai_state_t st;
    lai_init_state(&st);
    struct lai_ns_iterator it = LAI_NS_ITERATOR_INITIALIZER;
    lai_nsnode_t *n;
    lai_nsnode_t *ecnode[2] = {0, 0};
    while ((n = lai_ns_iterate(&it)) && ec_count < 2) {
        if (n->type != LAI_NAMESPACE_DEVICE || !pnp_match(n, "PNP0C09", &st)) continue;
        lai_initialize_ec_driver(&ecs[ec_count]);
        lai_init_ec(n, &ecs[ec_count]);
        if (ecs[ec_count].cmd_port && ecs[ec_count].data_port) ecnode[ec_count++] = n;
    }
    lai_finalize_state(&st);
    for (int i = 0; i < ec_count; i++) ec_node[i] = ecnode[i];
    if (!ec_count) return;
    it = (struct lai_ns_iterator)LAI_NS_ITERATOR_INITIALIZER;
    while ((n = lai_ns_iterate(&it))) {
        if (n->type != LAI_NAMESPACE_OPREGION || n->op_address_space != 3) continue;
        int pick = 0;
        for (lai_nsnode_t *p = n->parent; p; p = p->parent)
            for (int i = 0; i < ec_count; i++)
                if (p == ecnode[i]) { pick = i; goto found; }
    found:
        lai_ns_override_opregion(n, &ec_override, &ecs[pick]);
    }
    for (int i = 0; i < ec_count; i++) {
        lai_nsnode_t *reg = lai_resolve_path(ecnode[i], "_REG");
        if (!reg) continue;
        lai_state_t s2;
        lai_init_state(&s2);
        LAI_CLEANUP_VAR lai_variable_t a = LAI_VAR_INITIALIZER;
        LAI_CLEANUP_VAR lai_variable_t b = LAI_VAR_INITIALIZER;
        a.type = LAI_INTEGER; a.integer = 3;
        b.type = LAI_INTEGER; b.integer = 1;
        lai_eval_largs(NULL, reg, &s2, &a, &b, NULL);
        lai_finalize_state(&s2);
    }
}

int thos_acpi_ec_info(int *count, uint16_t *cmd, uint16_t *data, int *failures) {
    *count = ec_count; *cmd = ecs[0].cmd_port; *data = ecs[0].data_port; *failures = ec_fail;
    return 0;
}

int thos_acpi_start(int revision) {
    lai_set_acpi_revision(revision);
    lai_create_namespace();
    started = 1;
    init_embedded_controller();
    return lai_enable_acpi(1);
}

static lai_nsnode_t *node_of(const char *path) {
    return started ? lai_resolve_path(NULL, path) : NULL;
}

// 0 = ok (value in *out), -1 = no such object, -2 = evaluation failed, -3 = not an integer
int thos_acpi_eval_int_arg(const char *path, int has_arg, uint64_t arg, uint64_t *out) {
    lai_nsnode_t *n = node_of(path);
    if (!n) return -1;
    lai_state_t st;
    lai_init_state(&st);
    LAI_CLEANUP_VAR lai_variable_t res = LAI_VAR_INITIALIZER;
    lai_api_error_t e;
    if (has_arg) {
        LAI_CLEANUP_VAR lai_variable_t a = LAI_VAR_INITIALIZER;
        a.type = LAI_INTEGER;
        a.integer = arg;
        e = lai_eval_largs(&res, n, &st, &a, NULL);
    } else {
        e = lai_eval(&res, n, &st);
    }
    lai_finalize_state(&st);
    if (e) return -2;
    if (lai_obj_get_integer(&res, out)) return -3;
    return 0;
}

int thos_acpi_eval_int(const char *path, uint64_t *out) { return thos_acpi_eval_int_arg(path, 0, 0, out); }

// Evaluate a method returning a package; integers are stored in out[], other elements as 0 (strings: see below).
// Returns the element count (>= 0) or a negative error as above.
int thos_acpi_eval_pkg(const char *path, uint64_t *out, int max, char *str, int strlen_max, int str_index) {
    lai_nsnode_t *n = node_of(path);
    if (!n) return -1;
    lai_state_t st;
    lai_init_state(&st);
    LAI_CLEANUP_VAR lai_variable_t res = LAI_VAR_INITIALIZER;
    lai_api_error_t e = lai_eval(&res, n, &st);
    lai_finalize_state(&st);
    if (e) return -2;
    if (lai_obj_get_type(&res) != LAI_TYPE_PACKAGE) return -3;
    int count = 0;
    for (int i = 0; i < max; i++) {
        LAI_CLEANUP_VAR lai_variable_t el = LAI_VAR_INITIALIZER;
        if (lai_obj_get_pkg(&res, i, &el)) break;
        count++;
        out[i] = 0;
        enum lai_object_type t = lai_obj_get_type(&el);
        if (t == LAI_TYPE_INTEGER) lai_obj_get_integer(&el, &out[i]);
        else if (t == LAI_TYPE_STRING && i == str_index && str && strlen_max > 0) {
            const char *s = lai_exec_string_access(&el);
            int k = 0;
            while (s[k] && k < strlen_max - 1) { str[k] = s[k]; k++; }
            str[k] = 0;
        }
    }
    return count;
}

static int pnp_match(lai_nsnode_t *dev, const char *pnp, lai_state_t *st) {
    LAI_CLEANUP_VAR lai_variable_t id = LAI_VAR_INITIALIZER;
    lai_eisaid(&id, pnp);
    if (!lai_check_device_pnp_id(dev, &id, st)) return 1;
    LAI_CLEANUP_VAR lai_variable_t sid = LAI_VAR_INITIALIZER;
    if (lai_create_c_string(&sid, pnp)) return 0;
    return !lai_check_device_pnp_id(dev, &sid, st);
}

// Path of the nth device whose _HID/_CID is `pnp` ("PNP0C0A"); 0 on success. pnp == NULL: the nth node of type `type`
// (LAI_NODETYPE_*; thermal zones, processors).
int thos_acpi_find(const char *pnp, int type, int nth, char *out, int outlen) {
    if (!started) return -1;
    lai_state_t st;
    lai_init_state(&st);
    struct lai_ns_iterator it = LAI_NS_ITERATOR_INITIALIZER;
    lai_nsnode_t *n;
    int idx = 0, rc = -1;
    while ((n = lai_ns_iterate(&it))) {
        int ok;
        if (pnp) ok = n->type == LAI_NAMESPACE_DEVICE && pnp_match(n, pnp, &st);
        else ok = lai_ns_get_node_type(n) == (enum lai_node_type)type;
        if (!ok) continue;
        if (idx++ != nth) continue;
        char *p = lai_stringify_node_path(n);
        int k = 0;
        while (p[k] && k < outlen - 1) { out[k] = p[k]; k++; }
        out[k] = 0;
        laihost_free(p, lai_strlen(p) + 1);
        rc = 0;
        break;
    }
    lai_finalize_state(&st);
    return rc;
}

// Count of devices / nodes, and a one-line-per-node dump through the host log.
int thos_acpi_dump(void (*line)(const char *)) {
    if (!started) return 0;
    struct lai_ns_iterator it = LAI_NS_ITERATOR_INITIALIZER;
    lai_nsnode_t *n;
    int count = 0;
    while ((n = lai_ns_iterate(&it))) {
        enum lai_node_type t = lai_ns_get_node_type(n);
        if (t != LAI_NODETYPE_DEVICE && t != LAI_NODETYPE_THERMALZONE && t != LAI_NODETYPE_PROCESSOR && t != LAI_NODETYPE_POWERRESOURCE) continue;
        char *p = lai_stringify_node_path(n);
        char buf[96];
        lai_snprintf(buf, sizeof buf, "%s %s", t == LAI_NODETYPE_DEVICE ? "device" : t == LAI_NODETYPE_THERMALZONE ? "thermal" : t == LAI_NODETYPE_PROCESSOR ? "cpu" : "power", p);
        line(buf);
        laihost_free(p, lai_strlen(p) + 1);
        count++;
    }
    return count;
}

// Path of the object that owns the nth method called `name` ("_BCM"): the display output that has a backlight.
int thos_acpi_find_owner(const char *name, int nth, char *out, int outlen) {
    if (!started) return -1;
    struct lai_ns_iterator it = LAI_NS_ITERATOR_INITIALIZER;
    lai_nsnode_t *n;
    int idx = 0;
    while ((n = lai_ns_iterate(&it))) {
        if (n->type != LAI_NAMESPACE_METHOD || !n->parent) continue;
        if (n->name[0] != name[0] || n->name[1] != name[1] || n->name[2] != name[2] || n->name[3] != name[3]) continue;
        if (idx++ != nth) continue;
        char *p = lai_stringify_node_path(n->parent);
        int k = 0;
        while (p[k] && k < outlen - 1) { out[k] = p[k]; k++; }
        out[k] = 0;
        laihost_free(p, lai_strlen(p) + 1);
        return 0;
    }
    return -1;
}

// The embedded controller raises SCI_EVT (status bit 5) when something happened (Fn keys, battery/AC change, lid): ask which event it
// was (query command 0x84) and run the firmware's _Qxx method for it. Polled, so no SCI interrupt routing is needed. Returns the number
// of the event handled, -1 if none.
int thos_acpi_ec_poll(void) {
    int handled = -1;
    for (int i = 0; i < ec_count; i++) {
        struct lai_ec_driver *d = &ecs[i];
        if (!(laihost_inb(d->cmd_port) & 0x20)) continue;
        if (ec_wait(d, 2, 0)) continue;
        laihost_outb(d->cmd_port, 0x84);
        if (ec_wait(d, 1, 1)) continue;
        uint8_t q = laihost_inb(d->data_port);
        if (!q) continue;
        char name[5] = {'_', 'Q', "0123456789ABCDEF"[q >> 4], "0123456789ABCDEF"[q & 15], 0};
        lai_nsnode_t *m = lai_resolve_path(ec_node[i], name);
        if (m) {
            lai_state_t st;
            lai_init_state(&st);
            lai_eval(NULL, m, &st);
            lai_finalize_state(&st);
        }
        handled = q;
    }
    return handled;
}
