// SPDX-License-Identifier: GPL-2.0-or-later
//! A small virtual `/proc` — enough for `ps`, `free`, `uptime`, `top` and for programs that
//! read `/proc/self/...`. Every file is generated when it is opened (a snapshot, which is also
//! how Linux's behaves for a single `read`), nothing is stored.

use alloc::format;
use alloc::string::{String, ToString};
use alloc::sync::Arc;
use alloc::vec::Vec;

use crate::file::{DirFile, Ext2File, FileOps};
use crate::process::{self, Task};
use crate::sched;

const DT_DIR: u8 = 4;
const DT_REG: u8 = 8;

pub fn is_proc(path: &str) -> bool {
    path == "/proc" || path.starts_with("/proc/")
}

fn file(path: &str, text: String) -> Option<Arc<dyn FileOps>> {
    let f: Arc<dyn FileOps> = Ext2File::new(path.to_string(), text.into_bytes());
    Some(f)
}

fn dir(names: &[(&str, u8)]) -> Option<Arc<dyn FileOps>> {
    let entries: Vec<(u64, u8, String)> =
        names.iter().enumerate().map(|(i, (n, t))| (i as u64 + 1, *t, n.to_string())).collect();
    let d: Arc<dyn FileOps> = DirFile::new(&entries);
    Some(d)
}

fn comm_of(t: &Task) -> String {
    let cl = t.cmdline();
    let first = cl.split(|&b| b == 0).next().unwrap_or(&[]);
    let s = String::from_utf8_lossy(first).to_string();
    let base = s.rsplit('/').next().unwrap_or("").to_string();
    let base = if base.is_empty() { "?".to_string() } else { base };
    base.chars().take(15).collect()
}

fn state_of(t: &Task) -> char {
    if t.is_exited() { 'Z' } else { 'S' }
}

fn pid_stat(t: &Task) -> String {
    // pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt
    // utime stime cutime cstime priority nice num_threads itrealvalue starttime vsize rss ...
    let mut s = format!(
        "{} ({}) {} {} {} {} 0 -1 0 0 0 0 0 0 0 0 0 20 0 1 0 0 0 0 18446744073709551615",
        t.pid,
        comm_of(t),
        state_of(t),
        t.ppid,
        t.pgid(),
        t.sid()
    );
    for _ in 0..28 {
        s.push_str(" 0");
    }
    s.push('\n');
    s
}

fn pid_status(t: &Task) -> String {
    format!(
        "Name:\t{}\nState:\t{} ({})\nPid:\t{}\nPPid:\t{}\nUid:\t{u}\t{u}\t{u}\t{u}\nGid:\t{g}\t{g}\t{g}\t{g}\nThreads:\t1\nVmSize:\t0 kB\n",
        comm_of(t),
        state_of(t),
        if t.is_exited() { "zombie" } else { "sleeping" },
        t.pid,
        t.ppid,
        u = t.uid,
        g = t.gid,
    )
}

fn by_pid(pid: &str) -> Option<Arc<Task>> {
    if pid == "self" {
        return sched::current().task();
    }
    process::find_task(pid.parse().ok()?)
}

fn cpuinfo() -> String {
    use core::arch::x86_64::__cpuid;
    let mut out = String::new();
    let (vendor, brand, fam, model, step, flags) = unsafe {
        let l0 = __cpuid(0);
        let mut v = [0u8; 12];
        v[0..4].copy_from_slice(&l0.ebx.to_le_bytes());
        v[4..8].copy_from_slice(&l0.edx.to_le_bytes());
        v[8..12].copy_from_slice(&l0.ecx.to_le_bytes());
        let mut b = [0u8; 48];
        if __cpuid(0x8000_0000).eax >= 0x8000_0004 {
            for i in 0..3u32 {
                let r = __cpuid(0x8000_0002 + i);
                for (j, w) in [r.eax, r.ebx, r.ecx, r.edx].iter().enumerate() {
                    b[(i as usize) * 16 + j * 4..][..4].copy_from_slice(&w.to_le_bytes());
                }
            }
        }
        let l1 = __cpuid(1);
        let fam = ((l1.eax >> 8) & 0xF) + if (l1.eax >> 8) & 0xF == 0xF { (l1.eax >> 20) & 0xFF } else { 0 };
        let model = ((l1.eax >> 4) & 0xF) | if matches!((l1.eax >> 8) & 0xF, 6 | 0xF) { ((l1.eax >> 16) & 0xF) << 4 } else { 0 };
        let mut f = String::from("fpu");
        let edx: [(u32, &str); 10] = [(4, "tsc"), (5, "msr"), (6, "pae"), (9, "apic"), (15, "cmov"), (23, "mmx"), (24, "fxsr"), (25, "sse"), (26, "sse2"), (28, "ht")];
        for (bit, n) in edx {
            if l1.edx & (1 << bit) != 0 {
                f.push(' ');
                f.push_str(n);
            }
        }
        let ecx: [(u32, &str); 9] = [(0, "pni"), (9, "ssse3"), (19, "sse4_1"), (20, "sse4_2"), (23, "popcnt"), (25, "aes"), (28, "avx"), (5, "vmx"), (26, "xsave")];
        for (bit, n) in ecx {
            if l1.ecx & (1 << bit) != 0 {
                f.push(' ');
                f.push_str(n);
            }
        }
        if l0.eax >= 7 && __cpuid(7).ebx & (1 << 5) != 0 {
            f.push_str(" avx2");
        }
        f.push_str(" syscall nx lm");
        (String::from_utf8_lossy(&v).to_string(), String::from_utf8_lossy(&b).trim_matches(|c: char| c == '\0' || c == ' ').to_string(), fam, model, l1.eax & 0xF, f)
    };
    for i in 0..crate::smp::cpu_count() {
        out.push_str(&format!(
            "processor\t: {i}\nvendor_id\t: {vendor}\ncpu family\t: {fam}\nmodel\t\t: {model}\nmodel name\t: {brand}\nstepping\t: {step}\ncpu MHz\t\t: {}\nflags\t\t: {flags}\n\n",
            crate::timer::tsc_mhz()
        ));
    }
    out
}

/// `/proc/pci`: every function on the first buses, `bus:dev.fn vendor:device class:subclass:progif irq`.
fn pci_list() -> String {
    use crate::pci::{read16, read32, Location};
    let mut out = String::new();
    for bus in 0..8u8 {
        for dev in 0..32u8 {
            for func in 0..8u8 {
                let loc = Location { bus, dev, func };
                let vid = read16(loc, 0x00);
                if vid == 0xFFFF {
                    if func == 0 {
                        break;
                    }
                    continue;
                }
                let cls = read32(loc, 0x08);
                out.push_str(&format!(
                    "{bus:02x}:{dev:02x}.{func} {vid:04x}:{:04x} class {:02x}{:02x} progif {:02x} irq {}\n",
                    read16(loc, 0x02),
                    cls >> 24,
                    (cls >> 16) & 0xFF,
                    (cls >> 8) & 0xFF,
                    read32(loc, 0x3C) & 0xFF
                ));
            }
        }
    }
    out
}

/// Open a path under `/proc`; `None` if it does not exist.
pub fn open(path: &str) -> Option<Arc<dyn FileOps>> {
    let rel = path.trim_end_matches('/').strip_prefix("/proc")?;
    let parts: Vec<&str> = rel.split('/').filter(|s| !s.is_empty()).collect();
    match parts.as_slice() {
        [] => {
            let mut names: Vec<(String, u8)> = ["meminfo", "uptime", "version", "cpuinfo", "loadavg", "stat", "mounts", "pci", "dmesg", "netinfo", "wlaninfo", "gpuinfo", "bootdisk", "self"]
                .iter()
                .map(|n| (n.to_string(), if *n == "self" { DT_DIR } else { DT_REG }))
                .collect();
            for t in process::all_live_tasks() {
                names.push((t.pid.to_string(), DT_DIR));
            }
            let refs: Vec<(&str, u8)> = names.iter().map(|(n, t)| (n.as_str(), *t)).collect();
            dir(&refs)
        }
        ["meminfo"] => {
            let total = crate::mm::total_frames() * 4;
            let free = crate::mm::FRAME_ALLOC.lock().free_frames() * 4;
            file(path, format!(
                "MemTotal:       {total} kB\nMemFree:        {free} kB\nMemAvailable:   {free} kB\nBuffers:               0 kB\nCached:                0 kB\nSwapCached:            0 kB\nSwapTotal:             0 kB\nSwapFree:              0 kB\n"
            ))
        }
        ["uptime"] => {
            let ns = crate::timer::monotonic_ns();
            file(path, format!("{}.{:02} {}.{:02}\n", ns / 1_000_000_000, (ns / 10_000_000) % 100, 0, 0))
        }
        ["version"] => file(path, "Linux version 6.1.0-thos (thos@thos) (rustc) #1 SMP THOS\n".to_string()),
        ["cpuinfo"] => file(path, cpuinfo()),
        ["hang_test"] => {
            // deliberately stuck in a kernel loop (interrupts on): proves the lock-up detector (lockup.rs) reports it; `lockup-test` uses this
            loop {
                core::hint::spin_loop();
            }
        }
        ["bootdisk"] => file(path, format!("{}\n", if crate::blk::is_ram() { "ram" } else if crate::blk::is_usb() { "usb" } else { "sata" })),
        ["netinfo"] => {
            file(path, crate::net::info())
        }
        ["wlaninfo"] => file(path, crate::wlan::info()),
        ["gpuinfo"] => file(path, crate::radeon::info()),
        ["pci"] => file(path, pci_list()),
        ["dmesg"] => {
            let f: Arc<dyn FileOps> = Ext2File::new(path.to_string(), crate::serial::log_snapshot());
            Some(f)
        }
        ["loadavg"] => file(path, format!("0.00 0.00 0.00 1/{} {}\n", process::all_live_tasks().len(), process::current_pid())),
        ["stat"] => file(path, format!(
            "cpu  0 0 0 0 0 0 0 0 0 0\nbtime {}\nprocesses {}\nprocs_running 1\n",
            crate::timer::unix_secs().saturating_sub(crate::timer::monotonic_ns() / 1_000_000_000),
            process::all_live_tasks().len()
        )),
        ["mounts"] => file(path, "rootfs / ext2 rw 0 0\n".to_string()),
        [pid] => {
            by_pid(pid)?;
            dir(&[("stat", DT_REG), ("status", DT_REG), ("cmdline", DT_REG), ("exe", DT_REG), ("maps", DT_REG)])
        }
        [pid, "maps"] => file(path, by_pid(pid)?.space().maps_text(None)),
        [pid, "stat"] => file(path, pid_stat(&*by_pid(pid)?)),
        [pid, "status"] => file(path, pid_status(&*by_pid(pid)?)),
        [pid, "cmdline"] => {
            let t = by_pid(pid)?;
            let f: Arc<dyn FileOps> = Ext2File::new(path.to_string(), t.cmdline());
            Some(f)
        }
        _ => None,
    }
}

/// `readlink` of `/proc/<pid>/exe` (argv[0], which is what THOS records of the executable).
pub fn readlink(path: &str) -> Option<String> {
    let rel = path.strip_prefix("/proc/")?;
    let (pid, what) = rel.split_once('/')?;
    if what != "exe" {
        return None;
    }
    let t = by_pid(pid)?;
    if !t.exe().is_empty() {
        return Some(t.exe());
    }
    let cl = t.cmdline();
    let first = cl.split(|&b| b == 0).next().unwrap_or(&[]);
    let s = String::from_utf8_lossy(first).to_string();
    Some(if s.starts_with('/') { s } else { format!("/{s}") })
}
