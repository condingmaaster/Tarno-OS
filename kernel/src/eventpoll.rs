// SPDX-License-Identifier: GPL-2.0-or-later
//! The Linux event descriptors modern programs are built on: `epoll`, `eventfd`, `timerfd`, `signalfd`
//! (and an `inotify` that never fires, so programs that merely ask for file watching start up).
//!
//! Readiness comes from [`FileOps::poll_mask`] as for `poll`; a waiting thread sleeps on the global poll
//! queue (see `wait::POLL_WQ`) and is woken by any queue wake-up in the kernel, with a short timeout as a
//! safety net for state that changes without one.

use alloc::sync::{Arc, Weak};
use alloc::vec::Vec;
use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use spin::Mutex;

use crate::file::{FileOps, POLLERR, POLLHUP, POLLIN, POLLOUT};
use crate::wait::WaitQueue;
use crate::{sched, signal, usercopy};

const EBADF: i64 = -9;
const EAGAIN: i64 = -11;
const EINTR: i64 = -4;
const EINVAL: i64 = -22;
const ENOENT: i64 = -2;
const EEXIST: i64 = -17;
const EFAULT: i64 = -14;
const EPERM: i64 = -1;
const S_IFIFO_LIKE: u32 = 0o010600;

const O_NONBLOCK: u64 = 0o4000;
const O_CLOEXEC: u64 = 0o2000000;

/// How long a sleeping poller trusts the wake-up hooks before looking again anyway.
const SAFETY_NET_NS: u64 = 50_000_000;
const NS_PER_TICK: u64 = 1_000_000_000 / crate::timer::TICK_HZ;

/// Sleep until the poll generation moves past `gen`, `deadline` (monotonic ns) or `wake_at` (a timerfd), whichever
/// is first; never longer than the safety net.
pub fn poll_block(gen: u64, deadline: Option<u64>, wake_at: Option<u64>) {
    let now = crate::timer::monotonic_ns();
    let mut until = now + SAFETY_NET_NS;
    for t in [deadline, wake_at].into_iter().flatten() {
        until = until.min(t);
    }
    let ticks = until.saturating_sub(now).div_ceil(NS_PER_TICK).max(1);
    crate::wait::poll_sleep(gen, crate::timer::now() + ticks);
}

fn alloc_fd(file: Arc<dyn FileOps>, flags: u64) -> i64 {
    let Some(task) = sched::current().task() else { return EBADF };
    if flags & O_NONBLOCK != 0 {
        file.set_nonblock(true);
    }
    task.fd_alloc_flags(file, flags & O_CLOEXEC != 0) as i64
}

// ---------------------------------------------------------------------------
//  eventfd
// ---------------------------------------------------------------------------

pub struct EventFd {
    count: Mutex<u64>,
    sem: bool,
    wq: WaitQueue,
    nonblock: AtomicBool,
}

impl FileOps for EventFd {
    fn read(&self, buf: &mut [u8]) -> i64 {
        if buf.len() < 8 {
            return EINVAL;
        }
        loop {
            {
                let mut c = self.count.lock();
                if *c > 0 {
                    let v = if self.sem { 1 } else { *c };
                    *c -= v;
                    drop(c);
                    buf[..8].copy_from_slice(&v.to_le_bytes());
                    self.wq.wake_all();
                    return 8;
                }
            }
            if self.nonblock.load(Ordering::Relaxed) {
                return EAGAIN;
            }
            if signal::interrupted() {
                return EINTR;
            }
            self.wq.wait_if_intr(|| *self.count.lock() == 0);
        }
    }
    fn write(&self, buf: &[u8]) -> i64 {
        if buf.len() < 8 {
            return EINVAL;
        }
        let v = u64::from_le_bytes(buf[..8].try_into().unwrap());
        if v == u64::MAX {
            return EINVAL;
        }
        loop {
            {
                let mut c = self.count.lock();
                if c.checked_add(v).is_some_and(|n| n < u64::MAX) {
                    *c += v;
                    drop(c);
                    self.wq.wake_all();
                    return 8;
                }
            }
            if self.nonblock.load(Ordering::Relaxed) {
                return EAGAIN;
            }
            if signal::interrupted() {
                return EINTR;
            }
            self.wq.wait_if_intr(|| self.count.lock().checked_add(v).is_none_or(|n| n >= u64::MAX));
        }
    }
    fn seek(&self, _o: i64, _w: u32) -> i64 {
        -29
    }
    fn stat(&self) -> (u32, u64) {
        (S_IFIFO_LIKE, 0)
    }
    fn poll_mask(&self, want: u16) -> u16 {
        let c = *self.count.lock();
        let mut r = 0;
        if c > 0 && want & POLLIN != 0 {
            r |= POLLIN;
        }
        if c < u64::MAX - 1 && want & POLLOUT != 0 {
            r |= POLLOUT;
        }
        r
    }
    fn set_nonblock(&self, on: bool) {
        self.nonblock.store(on, Ordering::Relaxed);
    }
    fn is_nonblock(&self) -> bool {
        self.nonblock.load(Ordering::Relaxed)
    }
}

/// `eventfd2(initval, flags)`; flags: EFD_SEMAPHORE=1, EFD_NONBLOCK, EFD_CLOEXEC.
pub fn sys_eventfd(initval: u64, flags: u64) -> i64 {
    if flags & !(1 | O_NONBLOCK | O_CLOEXEC) != 0 {
        return EINVAL;
    }
    let f = Arc::new(EventFd { count: Mutex::new(initval & 0xFFFF_FFFF), sem: flags & 1 != 0, wq: WaitQueue::new(), nonblock: AtomicBool::new(false) });
    alloc_fd(f, flags)
}

// ---------------------------------------------------------------------------
//  timerfd
// ---------------------------------------------------------------------------

struct TimerState {
    realtime: bool,
    /// Monotonic deadline of the next expiry; 0 = disarmed.
    next: u64,
    interval: u64,
    pending: u64,
}

pub struct TimerFd {
    st: Mutex<TimerState>,
    wq: WaitQueue,
    nonblock: AtomicBool,
}

impl TimerFd {
    /// Fold elapsed expirations into `pending`.
    fn update(st: &mut TimerState) {
        if st.next == 0 {
            return;
        }
        let now = crate::timer::monotonic_ns();
        if now < st.next {
            return;
        }
        if st.interval > 0 {
            let n = 1 + (now - st.next) / st.interval;
            st.pending += n;
            st.next += n * st.interval;
        } else {
            st.pending += 1;
            st.next = 0;
        }
    }
}

impl FileOps for TimerFd {
    fn read(&self, buf: &mut [u8]) -> i64 {
        if buf.len() < 8 {
            return EINVAL;
        }
        loop {
            let wait_until;
            {
                let mut st = self.st.lock();
                Self::update(&mut st);
                if st.pending > 0 {
                    let v = core::mem::take(&mut st.pending);
                    buf[..8].copy_from_slice(&v.to_le_bytes());
                    return 8;
                }
                wait_until = st.next;
            }
            if self.nonblock.load(Ordering::Relaxed) {
                return EAGAIN;
            }
            if signal::interrupted() {
                return EINTR;
            }
            if wait_until == 0 {
                self.wq.wait_if_intr(|| self.st.lock().next == 0 && self.st.lock().pending == 0); // disarmed: until re-armed
            } else {
                let ns = wait_until.saturating_sub(crate::timer::monotonic_ns());
                let dl = crate::timer::now() + ns.div_ceil(NS_PER_TICK).max(1);
                self.wq.wait_if_until(dl, || true);
            }
        }
    }
    fn write(&self, _buf: &[u8]) -> i64 {
        EINVAL
    }
    fn seek(&self, _o: i64, _w: u32) -> i64 {
        -29
    }
    fn stat(&self) -> (u32, u64) {
        (S_IFIFO_LIKE, 0)
    }
    fn poll_mask(&self, want: u16) -> u16 {
        let mut st = self.st.lock();
        Self::update(&mut st);
        if st.pending > 0 { want & POLLIN } else { 0 }
    }
    fn next_wake_ns(&self) -> Option<u64> {
        let st = self.st.lock();
        (st.next != 0).then_some(st.next)
    }
    fn as_timerfd(&self) -> Option<&TimerFd> {
        Some(self)
    }
    fn set_nonblock(&self, on: bool) {
        self.nonblock.store(on, Ordering::Relaxed);
    }
    fn is_nonblock(&self) -> bool {
        self.nonblock.load(Ordering::Relaxed)
    }
}

pub fn sys_timerfd_create(clock: u64, flags: u64) -> i64 {
    if !matches!(clock, 0 | 1 | 7 | 8 | 9) || flags & !(O_NONBLOCK | O_CLOEXEC) != 0 {
        return EINVAL;
    }
    let f = Arc::new(TimerFd {
        st: Mutex::new(TimerState { realtime: clock == 0 || clock == 8, next: 0, interval: 0, pending: 0 }),
        wq: WaitQueue::new(),
        nonblock: AtomicBool::new(false),
    });
    alloc_fd(f, flags)
}

fn read_timespec_ns(p: u64) -> Result<u64, i64> {
    let s = usercopy::read_u64(p)? as i64;
    let n = usercopy::read_u64(p + 8)? as i64;
    if s < 0 || !(0..1_000_000_000).contains(&n) {
        return Err(EINVAL);
    }
    Ok((s as u64).saturating_mul(1_000_000_000).saturating_add(n as u64))
}

fn write_timespec_ns(p: u64, ns: u64) -> Result<(), i64> {
    usercopy::write_u64(p, ns / 1_000_000_000)?;
    usercopy::write_u64(p + 8, ns % 1_000_000_000)
}

pub fn sys_timerfd_settime(fd: u64, flags: u64, new: u64, old: u64) -> i64 {
    let Some(task) = sched::current().task() else { return EBADF };
    let Some(file) = task.fd_get(fd as i32) else { return EBADF };
    let Some(t) = file.as_timerfd() else { return EINVAL };
    if flags & !3 != 0 || new == 0 {
        return EINVAL;
    }
    let (interval, value) = match (read_timespec_ns(new), read_timespec_ns(new + 16)) {
        (Ok(i), Ok(v)) => (i, v),
        (Err(e), _) | (_, Err(e)) => return e,
    };
    let now = crate::timer::monotonic_ns();
    let mut st = t.st.lock();
    TimerFd::update(&mut st);
    if old != 0 {
        let left = if st.next == 0 { 0 } else { st.next.saturating_sub(now) };
        if write_timespec_ns(old, st.interval).is_err() || write_timespec_ns(old + 16, left).is_err() {
            return EFAULT;
        }
    }
    st.pending = 0;
    st.interval = interval;
    st.next = if value == 0 {
        0
    } else if flags & 1 != 0 {
        // absolute time on the timer's clock: convert to the monotonic base
        let base = if st.realtime { crate::timer::realtime_ns() } else { now };
        now + value.saturating_sub(base).max(1)
    } else {
        now + value
    };
    drop(st);
    t.wq.wake_all();
    0
}

pub fn sys_timerfd_gettime(fd: u64, cur: u64) -> i64 {
    let Some(task) = sched::current().task() else { return EBADF };
    let Some(file) = task.fd_get(fd as i32) else { return EBADF };
    let Some(t) = file.as_timerfd() else { return EINVAL };
    let mut st = t.st.lock();
    TimerFd::update(&mut st);
    let left = if st.next == 0 { 0 } else { st.next.saturating_sub(crate::timer::monotonic_ns()) };
    if write_timespec_ns(cur, st.interval).is_err() || write_timespec_ns(cur + 16, left).is_err() {
        return EFAULT;
    }
    0
}

// ---------------------------------------------------------------------------
//  signalfd
// ---------------------------------------------------------------------------

pub struct SignalFd {
    mask: AtomicU64,
    nonblock: AtomicBool,
}

impl FileOps for SignalFd {
    fn read(&self, buf: &mut [u8]) -> i64 {
        if buf.len() < 128 {
            return EINVAL;
        }
        loop {
            let mask = self.mask.load(Ordering::Relaxed);
            let mut n = 0;
            while buf.len() - n * 128 >= 128 {
                let Some(sig) = signal::take_pending(mask) else { break };
                let rec = &mut buf[n * 128..n * 128 + 128];
                rec.fill(0);
                rec[0..4].copy_from_slice(&sig.to_le_bytes()); // ssi_signo; ssi_code (SI_USER) stays 0
                n += 1;
            }
            if n > 0 {
                return (n * 128) as i64;
            }
            if self.nonblock.load(Ordering::Relaxed) {
                return EAGAIN;
            }
            if signal::interrupted() {
                return EINTR;
            }
            signal::SIGFD_WQ.wait_if_intr(|| !signal::pending_in(mask));
        }
    }
    fn write(&self, _buf: &[u8]) -> i64 {
        EINVAL
    }
    fn seek(&self, _o: i64, _w: u32) -> i64 {
        -29
    }
    fn stat(&self) -> (u32, u64) {
        (S_IFIFO_LIKE, 0)
    }
    fn poll_mask(&self, want: u16) -> u16 {
        if signal::pending_in(self.mask.load(Ordering::Relaxed)) { want & POLLIN } else { 0 }
    }
    fn set_nonblock(&self, on: bool) {
        self.nonblock.store(on, Ordering::Relaxed);
    }
    fn is_nonblock(&self) -> bool {
        self.nonblock.load(Ordering::Relaxed)
    }
}

/// `signalfd4(fd, mask*, sizemask, flags)`: fd == -1 creates one, otherwise it replaces the mask.
pub fn sys_signalfd(fd: u64, mask_ptr: u64, size: u64, flags: u64) -> i64 {
    if size != 8 || flags & !(O_NONBLOCK | O_CLOEXEC) != 0 {
        return EINVAL;
    }
    let mask = match usercopy::read_u64(mask_ptr) {
        Ok(m) => m & !((1 << (signal::SIGKILL - 1)) | (1 << (signal::SIGSTOP - 1))),
        Err(e) => return e,
    };
    if fd as i32 != -1 {
        let Some(task) = sched::current().task() else { return EBADF };
        let Some(f) = task.fd_get(fd as i32) else { return EBADF };
        // only a signalfd has a mask to replace; others are rejected by reading nothing from them
        if f.stat().0 != S_IFIFO_LIKE {
            return EINVAL;
        }
        return EINVAL; // re-masking an existing descriptor is not supported: programs create a new one
    }
    alloc_fd(Arc::new(SignalFd { mask: AtomicU64::new(mask), nonblock: AtomicBool::new(false) }), flags)
}

// ---------------------------------------------------------------------------
//  inotify (accepts watches, never reports anything)
// ---------------------------------------------------------------------------

pub struct Inotify {
    next_wd: AtomicU64,
    nonblock: AtomicBool,
    wq: WaitQueue,
}

impl FileOps for Inotify {
    fn read(&self, _buf: &mut [u8]) -> i64 {
        loop {
            if self.nonblock.load(Ordering::Relaxed) {
                return EAGAIN;
            }
            if signal::interrupted() {
                return EINTR;
            }
            self.wq.wait_if_intr(|| true);
        }
    }
    fn write(&self, _buf: &[u8]) -> i64 {
        EINVAL
    }
    fn seek(&self, _o: i64, _w: u32) -> i64 {
        -29
    }
    fn stat(&self) -> (u32, u64) {
        (S_IFIFO_LIKE, 0)
    }
    fn poll_mask(&self, _want: u16) -> u16 {
        0
    }
    fn set_nonblock(&self, on: bool) {
        self.nonblock.store(on, Ordering::Relaxed);
    }
    fn is_nonblock(&self) -> bool {
        self.nonblock.load(Ordering::Relaxed)
    }
    fn as_inotify_wd(&self) -> Option<i64> {
        Some(self.next_wd.fetch_add(1, Ordering::Relaxed) as i64)
    }
}

pub fn sys_inotify_init(flags: u64) -> i64 {
    if flags & !(O_NONBLOCK | O_CLOEXEC) != 0 {
        return EINVAL;
    }
    alloc_fd(Arc::new(Inotify { next_wd: AtomicU64::new(1), nonblock: AtomicBool::new(false), wq: WaitQueue::new() }), flags)
}

pub fn sys_inotify_add_watch(fd: u64) -> i64 {
    match sched::current().task().and_then(|t| t.fd_get(fd as i32)) {
        Some(f) => f.as_inotify_wd().unwrap_or(EINVAL),
        None => EBADF,
    }
}

// ---------------------------------------------------------------------------
//  epoll
// ---------------------------------------------------------------------------

const EPOLLIN: u32 = 0x001;
const EPOLLOUT: u32 = 0x004;
const EPOLLERR: u32 = 0x008;
const EPOLLHUP: u32 = 0x010;
const EPOLLRDNORM: u32 = 0x040;
const EPOLLWRNORM: u32 = 0x100;
const EPOLLRDHUP: u32 = 0x2000;
const EPOLLONESHOT: u32 = 1 << 30;
const EPOLLET: u32 = 1 << 31;

struct Item {
    fd: i32,
    file: Weak<dyn FileOps>,
    events: u32,
    data: u64,
    /// Readiness last reported (edge-triggered mode reports only newly set bits) and the poll generation then:
    /// new activity anywhere since (`POLL_GEN` moved) also counts as an edge while the object is still ready.
    last: u32,
    last_gen: u64,
    disabled: bool,
}

pub struct Epoll {
    items: Mutex<Vec<Item>>,
}

impl Epoll {
    /// Scan the interest list; fills `out` with up to `max` ready `(events, data)` pairs. Also reports the earliest
    /// timerfd expiry among the watched objects.
    fn scan(&self, max: usize, out: &mut Vec<(u32, u64)>) -> Option<u64> {
        let mut wake_at: Option<u64> = None;
        let gen = crate::wait::POLL_GEN.load(Ordering::Acquire);
        let mut items = self.items.lock();
        items.retain(|it| it.file.strong_count() > 0); // closed descriptors drop out, as in Linux
        for it in items.iter_mut() {
            let Some(f) = it.file.upgrade() else { continue };
            if let Some(t) = f.next_wake_ns() {
                wake_at = Some(wake_at.map_or(t, |w| w.min(t)));
            }
            if it.disabled {
                continue;
            }
            let mut want = 0u16;
            if it.events & (EPOLLIN | EPOLLRDNORM) != 0 {
                want |= POLLIN;
            }
            if it.events & (EPOLLOUT | EPOLLWRNORM) != 0 {
                want |= POLLOUT;
            }
            let r = f.poll_mask(want) as u32;
            let mut ev = 0u32;
            if r & POLLIN as u32 != 0 && it.events & (EPOLLIN | EPOLLRDNORM) != 0 {
                ev |= EPOLLIN;
            }
            if r & POLLOUT as u32 != 0 && it.events & (EPOLLOUT | EPOLLWRNORM) != 0 {
                ev |= EPOLLOUT;
            }
            if r & POLLERR as u32 != 0 {
                ev |= EPOLLERR;
            }
            if r & POLLHUP as u32 != 0 {
                ev |= EPOLLHUP;
                if it.events & EPOLLRDHUP != 0 {
                    ev |= EPOLLRDHUP;
                }
            }
            let report = if it.events & EPOLLET != 0 {
                let fresh = ev & !it.last;
                let activity = ev != 0 && gen != it.last_gen;
                it.last = ev;
                if fresh != 0 || activity {
                    it.last_gen = gen;
                    true
                } else {
                    false
                }
            } else {
                ev != 0
            };
            if report && out.len() < max {
                out.push((ev, it.data));
                if it.events & EPOLLONESHOT != 0 {
                    it.disabled = true;
                }
            }
        }
        wake_at
    }
}

impl FileOps for Epoll {
    fn read(&self, _buf: &mut [u8]) -> i64 {
        EINVAL
    }
    fn write(&self, _buf: &[u8]) -> i64 {
        EINVAL
    }
    fn seek(&self, _o: i64, _w: u32) -> i64 {
        -29
    }
    fn stat(&self) -> (u32, u64) {
        (S_IFIFO_LIKE, 0)
    }
    fn poll_mask(&self, want: u16) -> u16 {
        let mut out = Vec::new();
        self.scan(1, &mut out);
        if !out.is_empty() { want & POLLIN } else { 0 }
    }
    fn as_epoll(&self) -> Option<&Epoll> {
        Some(self)
    }
}

pub fn sys_epoll_create(flags: u64) -> i64 {
    if flags & !O_CLOEXEC != 0 {
        return EINVAL;
    }
    alloc_fd(Arc::new(Epoll { items: Mutex::new(Vec::new()) }), flags)
}

/// `epoll_ctl(epfd, op, fd, event*)`: op 1 = ADD, 2 = DEL, 3 = MOD.
pub fn sys_epoll_ctl(epfd: u64, op: u64, fd: u64, event: u64) -> i64 {
    let Some(task) = sched::current().task() else { return EBADF };
    let Some(ep_file) = task.fd_get(epfd as i32) else { return EBADF };
    let Some(ep) = ep_file.as_epoll() else { return EINVAL };
    let Some(target) = task.fd_get(fd as i32) else { return EBADF };
    if core::ptr::eq(Arc::as_ptr(&ep_file) as *const u8, Arc::as_ptr(&target) as *const u8) {
        return EINVAL;
    }
    let (events, data) = if op == 2 {
        (0, 0)
    } else {
        match (usercopy::read_u32(event), usercopy::read_u64(event + 4)) {
            (Ok(e), Ok(d)) => (e, d),
            _ => return EFAULT,
        }
    };
    let mut items = ep.items.lock();
    items.retain(|it| it.file.strong_count() > 0);
    let same = |it: &Item| it.fd == fd as i32 && it.file.upgrade().is_some_and(|f| Arc::ptr_eq(&f, &target));
    match op {
        1 => {
            if items.iter().any(same) {
                return EEXIST;
            }
            if target.stat().0 & 0o170000 == 0o100000 {
                return EPERM; // regular files cannot be polled
            }
            items.push(Item { fd: fd as i32, file: Arc::downgrade(&target), events, data, last: 0, last_gen: 0, disabled: false });
        }
        2 => {
            let before = items.len();
            items.retain(|it| !same(it));
            if items.len() == before {
                return ENOENT;
            }
        }
        3 => {
            let Some(it) = items.iter_mut().find(|it| same(it)) else { return ENOENT };
            it.events = events;
            it.data = data;
            it.last = 0;
            it.disabled = false;
        }
        _ => return EINVAL,
    }
    drop(items);
    crate::wait::poll_notify();
    0
}

/// `epoll_wait(epfd, events*, maxevents, timeout_ms)`; `timeout_ns < 0` = forever.
pub fn sys_epoll_wait(epfd: u64, events: u64, maxevents: u64, timeout_ns: i64) -> i64 {
    if maxevents == 0 || maxevents > 4096 {
        return EINVAL;
    }
    let Some(task) = sched::current().task() else { return EBADF };
    let Some(ep_file) = task.fd_get(epfd as i32) else { return EBADF };
    let Some(ep) = ep_file.as_epoll() else { return EINVAL };
    if !usercopy::user_ok(events, maxevents as usize * 12, true) {
        return EFAULT;
    }
    let deadline = (timeout_ns >= 0).then(|| crate::timer::monotonic_ns().saturating_add(timeout_ns as u64));
    let _listening = crate::wait::Pollers::enter();
    loop {
        let gen = crate::wait::POLL_GEN.load(Ordering::Acquire);
        let mut out = Vec::new();
        let wake_at = ep.scan(maxevents as usize, &mut out);
        if !out.is_empty() {
            for (i, (ev, data)) in out.iter().enumerate() {
                let base = events + (i * 12) as u64;
                if usercopy::write_u32(base, *ev).is_err() || usercopy::write_u64(base + 4, *data).is_err() {
                    return EFAULT;
                }
            }
            return out.len() as i64;
        }
        if signal::interrupted() {
            return EINTR;
        }
        if deadline.is_some_and(|d| crate::timer::monotonic_ns() >= d) {
            return 0;
        }
        poll_block(gen, deadline, wake_at);
    }
}
