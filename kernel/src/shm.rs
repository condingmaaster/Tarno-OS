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

/// `memfd_create` file: a section whose size is set by `ftruncate`.
pub struct MemFd {
    sec: Mutex<Option<Arc<Section>>>,
}

impl MemFd {
    pub fn new() -> Arc<Self> {
        Arc::new(MemFd { sec: Mutex::new(None) })
    }
}

impl FileOps for MemFd {
    fn read(&self, _buf: &mut [u8]) -> i64 {
        -9
    }
    fn write(&self, _buf: &[u8]) -> i64 {
        -9
    }
    fn seek(&self, _o: i64, _w: u32) -> i64 {
        0
    }
    fn stat(&self) -> (u32, u64) {
        (S_IFREG | 0o600, self.sec.lock().as_ref().map_or(0, |s| s.size as u64))
    }
    fn truncate(&self, len: u64) -> i64 {
        if len as usize > MAX_SEG {
            return ENOMEM;
        }
        let mut s = self.sec.lock();
        if s.as_ref().is_some_and(|x| x.size == len as usize) {
            return 0;
        }
        *s = Some(Arc::new(Section::new(&alloc::vec![0u8; len as usize], None)));
        0
    }
    fn shm_section(&self) -> Option<Arc<Section>> {
        self.sec.lock().clone()
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
