// SPDX-License-Identifier: GPL-2.0-or-later
//! ACPI Machine Language interpreter (lai, MIT, third_party/lai) and its host side: the firmware's AML decides how battery, AC adapter,
//! thermal zones, lid and the embedded controller are reached, so every sensor below goes through here. The C glue in `c/acpi_glue.c`
//! keeps lai's structures away from Rust.
//!
//! lai runs in its own kernel thread (a hanging firmware method must not stop the boot) and is serialized by one lock: lai is not
//! reentrant.

use alloc::alloc::{alloc, dealloc, realloc, Layout};
use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::{c_char, c_int, c_void, CStr};
use core::sync::atomic::{AtomicBool, AtomicPtr, Ordering};
use spin::Mutex;
use x86_64::instructions::port::Port;

use crate::mm::hhdm_offset;

static RSDP: AtomicPtr<u8> = AtomicPtr::new(core::ptr::null_mut());
static READY: AtomicBool = AtomicBool::new(false);
static LOCK: Mutex<()> = Mutex::new(());

extern "C" {
    fn thos_acpi_start(revision: c_int) -> c_int;
    fn thos_acpi_eval_int_arg(path: *const c_char, has_arg: c_int, arg: u64, out: *mut u64) -> c_int;
    fn thos_acpi_eval_pkg(path: *const c_char, out: *mut u64, max: c_int, s: *mut c_char, slen: c_int, sidx: c_int) -> c_int;
    fn thos_acpi_find(pnp: *const c_char, ty: c_int, nth: c_int, out: *mut c_char, outlen: c_int) -> c_int;
    fn thos_acpi_dump(line: extern "C" fn(*const c_char)) -> c_int;
}

/// Is the interpreter up (namespace built, ACPI mode on)?
pub fn ready() -> bool {
    READY.load(Ordering::Acquire)
}

// --- host functions lai calls -----------------------------------------------------------------------------------------------------

const HDR: usize = 16; // keeps 16-byte alignment and stores the size for realloc/free

#[no_mangle]
extern "C" fn laihost_malloc(size: usize) -> *mut c_void {
    unsafe {
        let p = alloc(Layout::from_size_align_unchecked(size + HDR, 16));
        if p.is_null() {
            return p as *mut c_void;
        }
        *(p as *mut usize) = size;
        p.add(HDR) as *mut c_void
    }
}
#[no_mangle]
extern "C" fn laihost_realloc(ptr: *mut c_void, new: usize, _old: usize) -> *mut c_void {
    if ptr.is_null() {
        return laihost_malloc(new);
    }
    unsafe {
        let base = (ptr as *mut u8).sub(HDR);
        let old = *(base as *mut usize);
        let p = realloc(base, Layout::from_size_align_unchecked(old + HDR, 16), new + HDR);
        if p.is_null() {
            return p as *mut c_void;
        }
        *(p as *mut usize) = new;
        p.add(HDR) as *mut c_void
    }
}
#[no_mangle]
extern "C" fn laihost_free(ptr: *mut c_void, _size: usize) {
    if ptr.is_null() {
        return;
    }
    unsafe {
        let base = (ptr as *mut u8).sub(HDR);
        let old = *(base as *mut usize);
        dealloc(base, Layout::from_size_align_unchecked(old + HDR, 16));
    }
}
#[no_mangle]
extern "C" fn laihost_log(level: c_int, msg: *const c_char) {
    // level 1 = debug (very chatty), 2 = warning
    if level >= 2 {
        let s = unsafe { CStr::from_ptr(msg) }.to_str().unwrap_or("?");
        crate::kprintln!("THOS: lai warn         {}", s);
    }
}
#[no_mangle]
extern "C" fn laihost_panic(msg: *const c_char) -> ! {
    let s = unsafe { CStr::from_ptr(msg) }.to_str().unwrap_or("?");
    crate::kprintln!("THOS: lai PANIC        {} — AML interpreter stopped", s);
    // An AML error must not take the kernel with it: park this thread, the rest of the system goes on.
    READY.store(false, Ordering::Release);
    loop {
        crate::sched::yield_now();
        x86_64::instructions::hlt();
    }
}
#[no_mangle]
extern "C" fn laihost_scan(sig: *const c_char, index: usize) -> *mut c_void {
    let rsdp = RSDP.load(Ordering::Acquire) as *const u8;
    if rsdp.is_null() {
        return core::ptr::null_mut();
    }
    let s = unsafe { core::slice::from_raw_parts(sig as *const u8, 4) };
    let sig4: [u8; 4] = [s[0], s[1], s[2], s[3]];
    unsafe {
        if &sig4 == b"DSDT" {
            let Some((fadt, len)) = crate::acpi::find_table_ptr(rsdp, b"FACP") else { return core::ptr::null_mut() };
            let mut pa = core::ptr::read_unaligned(fadt.add(40) as *const u32) as u64;
            if len >= 148 {
                let x = core::ptr::read_unaligned(fadt.add(140) as *const u64);
                if x != 0 {
                    pa = x;
                }
            }
            return if pa == 0 { core::ptr::null_mut() } else { (pa + hhdm_offset()) as *mut c_void };
        }
        crate::acpi::find_table_nth(rsdp, &sig4, index).map_or(core::ptr::null_mut(), |(p, _)| p as *mut c_void)
    }
}
#[no_mangle]
extern "C" fn laihost_map(phys: usize, _len: usize) -> *mut c_void {
    (phys as u64 + hhdm_offset()) as *mut c_void
}
#[no_mangle]
extern "C" fn laihost_unmap(_p: *mut c_void, _len: usize) {}
#[no_mangle]
extern "C" fn laihost_outb(port: u16, v: u8) {
    unsafe { Port::new(port).write(v) }
}
#[no_mangle]
extern "C" fn laihost_outw(port: u16, v: u16) {
    unsafe { Port::new(port).write(v) }
}
#[no_mangle]
extern "C" fn laihost_outd(port: u16, v: u32) {
    unsafe { Port::new(port).write(v) }
}
#[no_mangle]
extern "C" fn laihost_inb(port: u16) -> u8 {
    unsafe { Port::new(port).read() }
}
#[no_mangle]
extern "C" fn laihost_inw(port: u16) -> u16 {
    unsafe { Port::new(port).read() }
}
#[no_mangle]
extern "C" fn laihost_ind(port: u16) -> u32 {
    unsafe { Port::new(port).read() }
}

fn loc(bus: u8, slot: u8, fun: u8) -> crate::pci::Location {
    crate::pci::Location { bus, dev: slot, func: fun }
}
#[no_mangle]
extern "C" fn laihost_pci_readd(seg: u16, bus: u8, slot: u8, fun: u8, off: u16) -> u32 {
    if seg != 0 || off > 0xFC {
        return 0xFFFF_FFFF;
    }
    crate::pci::read32(loc(bus, slot, fun), off as u8)
}
#[no_mangle]
extern "C" fn laihost_pci_readw(seg: u16, bus: u8, slot: u8, fun: u8, off: u16) -> u16 {
    if seg != 0 || off > 0xFE {
        return 0xFFFF;
    }
    crate::pci::read16(loc(bus, slot, fun), off as u8)
}
#[no_mangle]
extern "C" fn laihost_pci_readb(seg: u16, bus: u8, slot: u8, fun: u8, off: u16) -> u8 {
    (laihost_pci_readd(seg, bus, slot, fun, off & !3) >> ((off & 3) * 8)) as u8
}
#[no_mangle]
extern "C" fn laihost_pci_writed(seg: u16, bus: u8, slot: u8, fun: u8, off: u16, v: u32) {
    if seg == 0 && off <= 0xFC {
        crate::pci::write32(loc(bus, slot, fun), off as u8, v);
    }
}
#[no_mangle]
extern "C" fn laihost_pci_writew(seg: u16, bus: u8, slot: u8, fun: u8, off: u16, v: u16) {
    if seg == 0 && off <= 0xFE {
        crate::pci::write16(loc(bus, slot, fun), off as u8, v);
    }
}
#[no_mangle]
extern "C" fn laihost_pci_writeb(seg: u16, bus: u8, slot: u8, fun: u8, off: u16, v: u8) {
    let o = off & !3;
    let sh = (off & 3) * 8;
    let old = laihost_pci_readd(seg, bus, slot, fun, o);
    laihost_pci_writed(seg, bus, slot, fun, o, (old & !(0xFF << sh)) | ((v as u32) << sh));
}
/// lai wants milliseconds.
#[no_mangle]
extern "C" fn laihost_sleep(ms: u64) {
    if ms == 0 {
        return;
    }
    crate::timer::sleep_until(crate::timer::deadline_after_ns(ms * 1_000_000));
}
/// 100 ns units.
#[no_mangle]
extern "C" fn laihost_timer() -> u64 {
    crate::timer::monotonic_ns() / 100
}

// --- public API -------------------------------------------------------------------------------------------------------------------

fn cstr(s: &str) -> Vec<u8> {
    let mut v = Vec::with_capacity(s.len() + 1);
    v.extend_from_slice(s.as_bytes());
    v.push(0);
    v
}

/// Evaluate an integer object or method (absolute ACPI path such as `\_SB_.BAT0._STA`).
pub fn eval_int(path: &str) -> Option<u64> {
    eval_int_arg(path, None)
}
pub fn eval_int_arg(path: &str, arg: Option<u64>) -> Option<u64> {
    if !ready() {
        return None;
    }
    let p = cstr(path);
    let _g = LOCK.lock();
    let mut out = 0u64;
    let rc = unsafe { thos_acpi_eval_int_arg(p.as_ptr() as *const c_char, arg.is_some() as c_int, arg.unwrap_or(0), &mut out) };
    (rc == 0).then_some(out)
}
/// Evaluate a method returning a package: the integer elements (strings count as 0), plus string element `str_index` if wanted.
pub fn eval_pkg(path: &str, max: usize, str_index: Option<usize>) -> Option<(Vec<u64>, String)> {
    if !ready() {
        return None;
    }
    let p = cstr(path);
    let _g = LOCK.lock();
    let mut vals = alloc::vec![0u64; max];
    let mut sbuf = [0 as c_char; 64];
    let n = unsafe { thos_acpi_eval_pkg(p.as_ptr() as *const c_char, vals.as_mut_ptr(), max as c_int, sbuf.as_mut_ptr(), 64, str_index.map_or(-1, |i| i as c_int)) };
    if n < 0 {
        return None;
    }
    vals.truncate(n as usize);
    let s = unsafe { CStr::from_ptr(sbuf.as_ptr()) }.to_str().unwrap_or("").into();
    Some((vals, s))
}
/// Path of the `nth` device with the given PNP/ACPI id.
pub fn find_device(pnp: &str, nth: usize) -> Option<String> {
    if !ready() {
        return None;
    }
    let p = cstr(pnp);
    let _g = LOCK.lock();
    let mut out = [0 as c_char; 96];
    let rc = unsafe { thos_acpi_find(p.as_ptr() as *const c_char, 0, nth as c_int, out.as_mut_ptr(), 96) };
    (rc == 0).then(|| unsafe { CStr::from_ptr(out.as_ptr()) }.to_str().unwrap_or("").into())
}
/// Path of the `nth` thermal zone (`ty` = 6) or processor object (`ty` = 5).
pub fn find_node(ty: i32, nth: usize) -> Option<String> {
    if !ready() {
        return None;
    }
    let _g = LOCK.lock();
    let mut out = [0 as c_char; 96];
    let rc = unsafe { thos_acpi_find(core::ptr::null(), ty, nth as c_int, out.as_mut_ptr(), 96) };
    (rc == 0).then(|| unsafe { CStr::from_ptr(out.as_ptr()) }.to_str().unwrap_or("").into())
}

extern "C" fn dump_line(s: *const c_char) {
    let s = unsafe { CStr::from_ptr(s) }.to_str().unwrap_or("?");
    crate::kprintln!("THOS: acpi             {}", s);
}

/// Start the interpreter: namespace from DSDT + SSDTs, ACPI mode on. Runs on the calling thread (see [`spawn`]).
pub fn start(rsdp: *const u8, revision: u8) {
    RSDP.store(rsdp as *mut u8, Ordering::Release);
    let t0 = crate::timer::monotonic_ns();
    let rc = {
        let _g = LOCK.lock();
        unsafe { thos_acpi_start(revision as c_int) }
    };
    READY.store(rc == 0, Ordering::Release);
    crate::kprintln!("THOS: acpi aml         interpreter {} in {} ms", if rc == 0 { "up" } else { "FAILED" }, (crate::timer::monotonic_ns() - t0) / 1_000_000);
    if rc == 0 {
        let _g = LOCK.lock();
        let n = unsafe { thos_acpi_dump(dump_line) };
        crate::kprintln!("THOS: acpi             {} devices / zones / processors", n);
    }
}

// --- sensors ----------------------------------------------------------------------------------------------------------------------

static SNAPSHOT: Mutex<String> = Mutex::new(String::new());

/// The last sensor reading as `key: value` lines (`/proc/power`); empty before the first poll.
pub fn power_text() -> String {
    SNAPSHOT.lock().clone()
}

const UNKNOWN: u64 = 0xFFFF_FFFF;

fn battery_lines(out: &mut String, idx: usize, path: &str) {
    use core::fmt::Write;
    let sta = eval_int(&alloc::format!("{path}._STA")).unwrap_or(0x1F);
    if sta & 0x10 == 0 {
        let _ = writeln!(out, "bat{idx}: absent");
        return;
    }
    let _ = writeln!(out, "bat{idx}: present");
    // _BIX (ACPI 4) has more fields but _BIF is everywhere: unit, design capacity, last full capacity, technology, design voltage.
    let bif = eval_pkg(&alloc::format!("{path}._BIF"), 13, Some(9));
    let (unit, full) = match &bif {
        Some((v, model)) if v.len() >= 3 => {
            let _ = writeln!(out, "bat{idx}.unit: {}", if v[0] == 0 { "mWh" } else { "mAh" });
            let _ = writeln!(out, "bat{idx}.design: {}", v[1]);
            let _ = writeln!(out, "bat{idx}.full: {}", v[2]);
            if !model.is_empty() {
                let _ = writeln!(out, "bat{idx}.model: {model}");
            }
            (v[0], v[2])
        }
        _ => (0, 0),
    };
    let _ = unit;
    if let Some((bst, _)) = eval_pkg(&alloc::format!("{path}._BST"), 4, None) {
        if bst.len() >= 4 {
            let st = bst[0];
            let state = if st & 4 != 0 { "critical" } else if st & 1 != 0 { "discharging" } else if st & 2 != 0 { "charging" } else { "full" };
            let _ = writeln!(out, "bat{idx}.state: {state}");
            if bst[2] != UNKNOWN {
                let _ = writeln!(out, "bat{idx}.remaining: {}", bst[2]);
                if full > 0 && full != UNKNOWN {
                    let _ = writeln!(out, "bat{idx}.percent: {}", (bst[2] * 100 / full).min(100));
                }
            }
            if bst[1] != UNKNOWN {
                let _ = writeln!(out, "bat{idx}.rate: {}", bst[1]);
            }
            if bst[3] != UNKNOWN {
                let _ = writeln!(out, "bat{idx}.voltage: {}", bst[3]);
            }
        }
    }
}

/// Read every sensor the firmware describes once.
fn poll_once() -> String {
    use core::fmt::Write;
    let mut out = String::new();
    for i in 0..2 {
        let Some(p) = find_device("ACPI0003", i) else { break };
        if let Some(v) = eval_int(&alloc::format!("{p}._PSR")) {
            let _ = writeln!(out, "ac{i}: {}", if v != 0 { "online" } else { "offline" });
        }
    }
    for i in 0..4 {
        let Some(p) = find_device("PNP0C0A", i) else { break };
        battery_lines(&mut out, i, &p);
    }
    for i in 0..8 {
        let Some(p) = find_node(6, i) else { break };
        if let Some(dk) = eval_int(&alloc::format!("{p}._TMP")) {
            if dk > 2000 && dk < 4000 {
                let c10 = dk as i64 - 2731;
                let _ = writeln!(out, "thermal{i}: {}.{} C", c10 / 10, (c10 % 10).abs());
            }
        }
    }
    if let Some(p) = find_device("PNP0C0D", 0) {
        if let Some(v) = eval_int(&alloc::format!("{p}._LID")) {
            let _ = writeln!(out, "lid: {}", if v != 0 { "open" } else { "closed" });
        }
    }
    out
}

/// Body of the interpreter thread: start lai, then refresh the sensor snapshot every few seconds.
pub fn run(rsdp: *const u8, revision: u8) -> ! {
    start(rsdp, revision);
    if ready() {
        let mut first = true;
        loop {
            let text = poll_once();
            if first {
                for l in text.lines() {
                    crate::kprintln!("THOS: sensors          {l}");
                }
                first = false;
            }
            *SNAPSHOT.lock() = text;
            crate::timer::sleep_until(crate::timer::deadline_after_ns(5_000_000_000));
        }
    }
    crate::sched::exit()
}
