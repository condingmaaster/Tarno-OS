// SPDX-License-Identifier: GPL-2.0-or-later
//! Shared memory for POSIX programs: `memfd_create` + `ftruncate` + `mmap(MAP_SHARED)` and the SysV
//! `shmget`/`shmat`/`shmdt`/`shmctl`. Both sit on the NT personality's `Section` (a set of
//! physical frames mapped into several address spaces), so a desktop client and the compositor can
//! share a window buffer with no copy.

use alloc::sync::Arc;
use alloc::vec::Vec;
use spin::Mutex;

use crate::file::FileOps;
use crate::process::Section;
use crate::{sched, usercopy};

const EINVAL: i64 = -22;
const ENOENT: i64 = -2;
const EEXIST: i64 = -17;
const ENOMEM: i64 = -12;
const S_IFREG: u32 = 0o100000;
const MAX_SEG: usize = 64 << 20;

/// `memfd_create` file: sparse. Pages are allocated the first time they are touched (read/write, or a fault
/// in a mapping), so a program can `ftruncate` a few hundred MiB and use a small part of it.
pub struct MemFd {
    pages: Mutex<alloc::collections::BTreeMap<usize, x86_64::structures::paging::PhysFrame>>,
    pos: core::sync::atomic::AtomicU64,
    logical: core::sync::atomic::AtomicU64,
}

impl MemFd {
    pub fn new() -> Arc<Self> {
        Arc::new(MemFd {
            pages: Mutex::new(alloc::collections::BTreeMap::new()),
            pos: core::sync::atomic::AtomicU64::new(0),
            logical: core::sync::atomic::AtomicU64::new(0),
        })
    }
    fn size(&self) -> usize {
        self.logical.load(core::sync::atomic::Ordering::Relaxed) as usize
    }
    /// Physical address of page `idx`, allocating a zeroed one on first use.
    fn page(&self, idx: usize, alloc: bool) -> Option<u64> {
        let mut m = self.pages.lock();
        if let Some(f) = m.get(&idx) {
            return Some(f.start_address().as_u64());
        }
        if !alloc {
            return None;
        }
        let f = crate::mm::FRAME_ALLOC.lock().alloc()?;
        let p = f.start_address().as_u64();
        unsafe { core::ptr::write_bytes(crate::mm::phys_to_virt(f.start_address()).as_mut_ptr::<u8>(), 0, 4096) };
        m.insert(idx, f);
        Some(p)
    }
}

impl Drop for MemFd {
    fn drop(&mut self) {
        let mut fa = crate::mm::FRAME_ALLOC.lock();
        for (_, f) in core::mem::take(&mut *self.pages.lock()) {
            fa.dealloc(f);
        }
    }
}

impl FileOps for MemFd {
    fn is_tty(&self) -> bool {
        false
    }
    fn read(&self, buf: &mut [u8]) -> i64 {
        let pos = self.pos.load(core::sync::atomic::Ordering::Relaxed) as usize;
        let size = self.size();
        if pos >= size {
            return 0;
        }
        let n = buf.len().min(size - pos);
        let mut done = 0;
        while done < n {
            let off = pos + done;
            let chunk = (n - done).min(4096 - off % 4096);
            match self.page(off / 4096, false) {
                Some(p) => buf[done..done + chunk].copy_from_slice(unsafe {
                    core::slice::from_raw_parts(crate::mm::phys_to_virt(x86_64::PhysAddr::new(p)).as_ptr::<u8>().add(off % 4096), chunk)
                }),
                None => buf[done..done + chunk].fill(0), // a hole reads as zeros
            }
            done += chunk;
        }
        self.pos.store((pos + done) as u64, core::sync::atomic::Ordering::Relaxed);
        done as i64
    }
    fn write(&self, buf: &[u8]) -> i64 {
        let pos = self.pos.load(core::sync::atomic::Ordering::Relaxed) as usize;
        let size = self.size();
        if pos >= size {
            return -28; // ENOSPC: the size is set by ftruncate
        }
        let n = buf.len().min(size - pos);
        let mut done = 0;
        while done < n {
            let off = pos + done;
            let chunk = (n - done).min(4096 - off % 4096);
            let Some(p) = self.page(off / 4096, true) else { return if done > 0 { done as i64 } else { ENOMEM } };
            unsafe {
                core::slice::from_raw_parts_mut(crate::mm::phys_to_virt(x86_64::PhysAddr::new(p)).as_mut_ptr::<u8>().add(off % 4096), chunk)
                    .copy_from_slice(&buf[done..done + chunk])
            };
            done += chunk;
        }
        self.pos.store((pos + done) as u64, core::sync::atomic::Ordering::Relaxed);
        done as i64
    }
    fn seek(&self, o: i64, w: u32) -> i64 {
        let size = self.size() as i64;
        let cur = self.pos.load(core::sync::atomic::Ordering::Relaxed) as i64;
        let np = match w {
            0 => o,
            1 => cur + o,
            2 => size + o,
            _ => return EINVAL,
        };
        if np < 0 {
            return EINVAL;
        }
        self.pos.store(np as u64, core::sync::atomic::Ordering::Relaxed);
        np
    }
    fn stat(&self) -> (u32, u64) {
        (S_IFREG | 0o600, self.size() as u64)
    }
    fn truncate(&self, len: u64) -> i64 {
        if len > (1 << 30) {
            return ENOMEM;
        }
        // Shrinking only lowers the size: pages stay (they may be mapped somewhere) until the file is dropped.
        self.logical.store(len, core::sync::atomic::Ordering::Relaxed);
        0
    }
    fn is_memfd(&self) -> bool {
        true
    }
    fn lazy_page(&self, idx: usize) -> Option<u64> {
        if idx * 4096 >= self.size() {
            return None;
        }
        self.page(idx, true)
    }
}

pub fn sys_memfd_create(flags: u64) -> i64 {
    let Some(task) = sched::current().task() else { return -9 };
    task.fd_alloc_flags(MemFd::new(), flags & 1 != 0) as i64
}

pub fn sys_ftruncate(fd: u64, len: u64) -> i64 {
    match sched::current().task().and_then(|t| t.fd_get(fd as i32)) {
        Some(f) => f.truncate(len),
        None => -9,
    }
}

// --- SysV ---

struct Seg {
    key: i64,
    id: i64,
    sec: Arc<Section>,
}

static SEGS: Mutex<(Vec<Seg>, i64)> = Mutex::new((Vec::new(), 1));

pub fn sys_shmget(key: u64, size: u64, flags: u64) -> i64 {
    let key = key as i32 as i64;
    let mut g = SEGS.lock();
    if key != 0 {
        if let Some(s) = g.0.iter().find(|s| s.key == key) {
            if flags & 0o2000 != 0 && flags & 0o1000 != 0 {
                return EEXIST; // IPC_CREAT | IPC_EXCL
            }
            if size as usize > s.sec.size {
                return EINVAL;
            }
            return s.id;
        }
        if flags & 0o1000 == 0 {
            return ENOENT;
        }
    }
    if size == 0 || size as usize > MAX_SEG {
        return EINVAL;
    }
    let sec = Arc::new(Section::new(&alloc::vec![0u8; size as usize], None));
    let id = g.1;
    g.1 += 1;
    g.0.push(Seg { key, id, sec });
    id
}

pub fn sys_shmat(id: u64) -> i64 {
    let sec = match SEGS.lock().0.iter().find(|s| s.id == id as i64) {
        Some(s) => s.sec.clone(),
        None => return EINVAL,
    };
    let Some(p) = sched::current_proc() else { return EINVAL };
    p.map_section_view(&sec, 0, sec.size) as i64
}

pub fn sys_shmdt(addr: u64) -> i64 {
    match sched::current_proc() {
        Some(p) if p.unmap_view(addr) => 0,
        _ => EINVAL,
    }
}

/// `shmctl`: `IPC_RMID` forgets the key (attached views keep the memory alive); `IPC_STAT` is
/// not filled in beyond zeroes.
pub fn sys_shmctl(id: u64, cmd: u64, buf: u64) -> i64 {
    let mut g = SEGS.lock();
    let Some(pos) = g.0.iter().position(|s| s.id == id as i64) else { return EINVAL };
    match cmd {
        0 => {
            g.0.remove(pos);
            0
        }
        2 => {
            // struct shmid_ds: shm_segsz at offset 48
            let size = g.0[pos].sec.size as u64;
            if buf != 0 {
                if let Ok(b) = usercopy::slice_mut(buf, 112) {
                    b.fill(0);
                    b[48..56].copy_from_slice(&size.to_le_bytes());
                }
            }
            0
        }
        _ => 0,
    }
}
