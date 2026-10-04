// SPDX-License-Identifier: GPL-2.0-or-later
//! AF_UNIX stream sockets: `socketpair`, and `bind`/`listen`/`accept`/`connect` on a path (the
//! path lives in an in-kernel registry, it is not a filesystem node). Foundation for the
//! desktop's client <-> compositor channel and for ordinary Unix daemons.

use alloc::collections::VecDeque;
use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec::Vec;
use core::sync::atomic::{AtomicBool, Ordering};
use spin::Mutex;

use crate::file::{FileOps, POLLERR, POLLHUP, POLLIN, POLLOUT};
use crate::wait::WaitQueue;

const CAP: usize = 128 * 1024;
const S_IFSOCK: u32 = 0o140000;
const EPIPE: i64 = -32;
const EINTR: i64 = -4;
const EAGAIN: i64 = -11;
const EINVAL: i64 = -22;
const EADDRINUSE: i64 = -98;
const ECONNREFUSED: i64 = -111;
const EISCONN: i64 = -106;
const ENOTCONN: i64 = -107;

/// Byte counters and in-flight file descriptors (`SCM_RIGHTS`) of one direction. A descriptor group is
/// tagged with the stream offset of the first byte sent with it and travels with that byte.
struct Meta {
    written: u64,
    read: u64,
    fds: VecDeque<(u64, Vec<Arc<dyn FileOps>>)>,
}

/// One direction of a connection.
struct Chan {
    buf: Mutex<VecDeque<u8>>,
    meta: Mutex<Meta>,
    wr_closed: AtomicBool,
    rd_closed: AtomicBool,
    wq: WaitQueue,
}

impl Chan {
    fn new() -> Arc<Self> {
        Arc::new(Chan {
            buf: Mutex::new(VecDeque::new()),
            meta: Mutex::new(Meta { written: 0, read: 0, fds: VecDeque::new() }),
            wr_closed: AtomicBool::new(false),
            rd_closed: AtomicBool::new(false),
            wq: WaitQueue::new(),
        })
    }
}

struct Listener {
    queue: Mutex<VecDeque<Arc<UnixSock>>>,
    wq: WaitQueue,
    open: AtomicBool,
}

enum State {
    Fresh,
    Bound(Vec<u8>),
    Listening(Vec<u8>, Arc<Listener>),
    Connected { rx: Arc<Chan>, tx: Arc<Chan>, peer: Option<Vec<u8>>, me: Option<Vec<u8>> },
}

pub struct UnixSock {
    st: Mutex<State>,
    nonblock: AtomicBool,
}

static REGISTRY: Mutex<Vec<(Vec<u8>, Arc<Listener>)>> = Mutex::new(Vec::new());

impl UnixSock {
    pub fn new() -> Arc<Self> {
        Arc::new(UnixSock { st: Mutex::new(State::Fresh), nonblock: AtomicBool::new(false) })
    }

    pub fn pair() -> (Arc<Self>, Arc<Self>) {
        let (ab, ba) = (Chan::new(), Chan::new());
        let mk = |rx: &Arc<Chan>, tx: &Arc<Chan>| {
            Arc::new(UnixSock {
                st: Mutex::new(State::Connected { rx: rx.clone(), tx: tx.clone(), peer: None, me: None }),
                nonblock: AtomicBool::new(false),
            })
        };
        (mk(&ba, &ab), mk(&ab, &ba))
    }

    pub fn bind(&self, path: Vec<u8>) -> i64 {
        let mut st = self.st.lock();
        if !matches!(*st, State::Fresh) {
            return EINVAL;
        }
        if REGISTRY.lock().iter().any(|(p, _)| *p == path) {
            return EADDRINUSE;
        }
        *st = State::Bound(path);
        0
    }

    pub fn listen(&self) -> i64 {
        let mut st = self.st.lock();
        let State::Bound(p) = &*st else { return EINVAL };
        let p = p.clone();
        let l = Arc::new(Listener { queue: Mutex::new(VecDeque::new()), wq: WaitQueue::new(), open: AtomicBool::new(true) });
        let mut reg = REGISTRY.lock();
        if reg.iter().any(|(q, _)| *q == p) {
            return EADDRINUSE;
        }
        reg.push((p.clone(), l.clone()));
        *st = State::Listening(p, l);
        0
    }

    pub fn connect(&self, path: Vec<u8>) -> i64 {
        let l = match REGISTRY.lock().iter().find(|(p, _)| *p == path) {
            Some((_, l)) => l.clone(),
            None => return ECONNREFUSED,
        };
        let mut st = self.st.lock();
        if matches!(*st, State::Connected { .. }) {
            return EISCONN;
        }
        if !l.open.load(Ordering::Acquire) {
            return ECONNREFUSED;
        }
        let (ab, ba) = (Chan::new(), Chan::new());
        let server = Arc::new(UnixSock {
            st: Mutex::new(State::Connected { rx: ab.clone(), tx: ba.clone(), peer: None, me: Some(path.clone()) }),
            nonblock: AtomicBool::new(false),
        });
        *st = State::Connected { rx: ba, tx: ab, peer: Some(path), me: None };
        l.queue.lock().push_back(server);
        l.wq.wake_all();
        0
    }

    pub fn accept(&self) -> Result<Arc<UnixSock>, i64> {
        let l = match &*self.st.lock() {
            State::Listening(_, l) => l.clone(),
            _ => return Err(EINVAL),
        };
        loop {
            if let Some(s) = l.queue.lock().pop_front() {
                return Ok(s);
            }
            if self.nonblock.load(Ordering::Relaxed) {
                return Err(EAGAIN);
            }
            if crate::signal::interrupted() {
                return Err(EINTR);
            }
            l.wq.wait_if_intr(|| l.queue.lock().is_empty());
        }
    }

    pub fn local_path(&self) -> Option<Vec<u8>> {
        match &*self.st.lock() {
            State::Bound(p) | State::Listening(p, _) => Some(p.clone()),
            State::Connected { me, .. } => me.clone(),
            State::Fresh => None,
        }
    }

    pub fn peer_path(&self) -> Result<Option<Vec<u8>>, i64> {
        match &*self.st.lock() {
            State::Connected { peer, .. } => Ok(peer.clone()),
            _ => Err(ENOTCONN),
        }
    }

    pub fn shutdown(&self, how: u32) -> i64 {
        let st = self.st.lock();
        let State::Connected { rx, tx, .. } = &*st else { return ENOTCONN };
        if how == 0 || how == 2 {
            rx.rd_closed.store(true, Ordering::Release);
            rx.wq.wake_all();
        }
        if how == 1 || how == 2 {
            tx.wr_closed.store(true, Ordering::Release);
            tx.wq.wake_all();
        }
        0
    }

    fn chans(&self) -> Option<(Arc<Chan>, Arc<Chan>)> {
        match &*self.st.lock() {
            State::Connected { rx, tx, .. } => Some((rx.clone(), tx.clone())),
            _ => None,
        }
    }
}

impl Drop for UnixSock {
    fn drop(&mut self) {
        match &*self.st.lock() {
            State::Connected { rx, tx, .. } => {
                tx.wr_closed.store(true, Ordering::Release);
                rx.rd_closed.store(true, Ordering::Release);
                tx.wq.wake_all();
                rx.wq.wake_all();
            }
            State::Listening(p, l) => {
                l.open.store(false, Ordering::Release);
                REGISTRY.lock().retain(|(q, _)| q != p);
            }
            _ => {}
        }
    }
}

impl UnixSock {
    /// Read up to `buf.len()` bytes; the second value holds the descriptors that were sent with them.
    /// A read never runs across the start of a later descriptor group, so descriptors stay with their message.
    pub fn recv_fds(&self, buf: &mut [u8]) -> (i64, Vec<Arc<dyn FileOps>>) {
        let Some((rx, _)) = self.chans() else { return (ENOTCONN, Vec::new()) };
        if buf.is_empty() {
            return (0, Vec::new());
        }
        loop {
            {
                let mut q = rx.buf.lock();
                if !q.is_empty() {
                    let mut n = buf.len().min(q.len());
                    let mut m = rx.meta.lock();
                    let r0 = m.read;
                    if let Some(&(g, _)) = m.fds.iter().find(|(g, _)| *g > r0 && *g < r0 + n as u64) {
                        n = (g - r0) as usize;
                    }
                    for b in buf.iter_mut().take(n) {
                        *b = q.pop_front().unwrap();
                    }
                    m.read += n as u64;
                    let mut got = Vec::new();
                    while m.fds.front().is_some_and(|(g, _)| *g < r0 + n as u64) {
                        got.extend(m.fds.pop_front().unwrap().1);
                    }
                    drop(m);
                    drop(q);
                    rx.wq.wake_all();
                    return (n as i64, got);
                }
                if rx.wr_closed.load(Ordering::Acquire) || rx.rd_closed.load(Ordering::Acquire) {
                    return (0, Vec::new());
                }
            }
            if self.nonblock.load(Ordering::Relaxed) {
                return (EAGAIN, Vec::new());
            }
            if crate::signal::interrupted() {
                return (EINTR, Vec::new());
            }
            rx.wq.wait_if_intr(|| {
                rx.buf.lock().is_empty() && !rx.wr_closed.load(Ordering::Acquire) && !rx.rd_closed.load(Ordering::Acquire)
            });
        }
    }

    /// Write `data`; `fds` (if any) ride with its first byte.
    pub fn send_fds(&self, data: &[u8], mut fds: Option<Vec<Arc<dyn FileOps>>>) -> i64 {
        let Some((_, tx)) = self.chans() else { return EPIPE };
        let mut done = 0;
        while done < data.len() {
            if tx.rd_closed.load(Ordering::Acquire) || tx.wr_closed.load(Ordering::Acquire) {
                return if done == 0 { EPIPE } else { done as i64 };
            }
            {
                let mut q = tx.buf.lock();
                let space = CAP - q.len();
                if space > 0 {
                    let n = space.min(data.len() - done);
                    q.extend(data[done..done + n].iter().copied());
                    {
                        let mut m = tx.meta.lock();
                        if let Some(f) = fds.take() {
                            let at = m.written;
                            m.fds.push_back((at, f));
                        }
                        m.written += n as u64;
                    }
                    done += n;
                    drop(q);
                    tx.wq.wake_all();
                    continue;
                }
            }
            if self.nonblock.load(Ordering::Relaxed) {
                return if done > 0 { done as i64 } else { EAGAIN };
            }
            if crate::signal::interrupted() {
                return if done == 0 { EINTR } else { done as i64 };
            }
            tx.wq.wait_if_intr(|| tx.buf.lock().len() == CAP && !tx.rd_closed.load(Ordering::Acquire));
        }
        done as i64
    }
}

impl FileOps for UnixSock {
    fn read(&self, buf: &mut [u8]) -> i64 {
        self.recv_fds(buf).0 // descriptors sent with these bytes are dropped (closed)
    }

    fn write(&self, data: &[u8]) -> i64 {
        self.send_fds(data, None)
    }

    fn seek(&self, _o: i64, _w: u32) -> i64 {
        -29 // ESPIPE
    }
    fn stat(&self) -> (u32, u64) {
        (S_IFSOCK | 0o777, 0)
    }
    fn poll_mask(&self, want: u16) -> u16 {
        let mut r = 0;
        match &*self.st.lock() {
            State::Listening(_, l) => {
                if want & POLLIN != 0 && !l.queue.lock().is_empty() {
                    r |= POLLIN;
                }
            }
            State::Connected { rx, tx, .. } => {
                if want & POLLIN != 0 && !rx.buf.lock().is_empty() {
                    r |= POLLIN;
                }
                if rx.wr_closed.load(Ordering::Acquire) {
                    r |= POLLHUP | (want & POLLIN);
                }
                if want & POLLOUT != 0 && tx.buf.lock().len() < CAP {
                    r |= POLLOUT;
                }
                if tx.rd_closed.load(Ordering::Acquire) {
                    r |= POLLERR;
                }
            }
            _ => {}
        }
        r
    }
    fn as_unix(&self) -> Option<&UnixSock> {
        Some(self)
    }
    fn set_nonblock(&self, on: bool) {
        self.nonblock.store(on, Ordering::Relaxed);
    }
    fn is_nonblock(&self) -> bool {
        self.nonblock.load(Ordering::Relaxed)
    }
}

/// Parse a `struct sockaddr_un` into a registry key (abstract names keep their leading NUL).
pub fn parse_addr(ptr: u64, len: u64) -> Result<Vec<u8>, i64> {
    if len < 3 || len > 110 {
        return Err(EINVAL);
    }
    let b = crate::usercopy::slice(ptr, len as usize)?;
    if u16::from_le_bytes([b[0], b[1]]) != 1 {
        return Err(-97); // EAFNOSUPPORT
    }
    let mut p = b[2..].to_vec();
    if p[0] != 0 {
        if let Some(z) = p.iter().position(|&c| c == 0) {
            p.truncate(z);
        }
    }
    Ok(p)
}

/// Write `sockaddr_un` for `path` (or just the family when there is none).
pub fn write_addr(ptr: u64, lenp: u64, path: Option<&[u8]>) -> i64 {
    if ptr == 0 || lenp == 0 {
        return 0;
    }
    let Ok(cap) = crate::usercopy::read_u32(lenp) else { return crate::usercopy::EFAULT };
    let mut sa: Vec<u8> = alloc::vec![1, 0];
    if let Some(p) = path {
        sa.extend_from_slice(p);
        if p.first() != Some(&0) {
            sa.push(0);
        }
    }
    let n = (cap as usize).min(sa.len());
    match crate::usercopy::slice_mut(ptr, n) {
        Ok(b) => b.copy_from_slice(&sa[..n]),
        Err(e) => return e,
    }
    match crate::usercopy::write_u32(lenp, sa.len() as u32) {
        Ok(()) => 0,
        Err(e) => e,
    }
}

#[allow(dead_code)]
fn _u(_: String) {
    let _ = POLLERR;
}
