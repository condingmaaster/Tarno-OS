// SPDX-License-Identifier: GPL-2.0-or-later
//! Pseudo-terminals: `/dev/ptmx` (master) and `/dev/pts/N` (slave) with a small line discipline
//! (canonical editing, echo, ISIG, ICRNL, ONLCR). A terminal emulator window drives an
//! interactive shell through one of these.

use alloc::collections::VecDeque;
use alloc::sync::Arc;
use alloc::vec::Vec;
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use spin::Mutex;

use crate::file::{FileOps, POLLHUP, POLLIN, POLLOUT};
use crate::wait::WaitQueue;
use crate::usercopy;

const S_IFCHR: u32 = 0o020000;
const CAP: usize = 64 * 1024;

const ICRNL: u32 = 0x100;
const INLCR: u32 = 0x40;
const IGNCR: u32 = 0x80;
const OPOST: u32 = 0x1;
const ONLCR: u32 = 0x4;
const ISIG: u32 = 0x1;
const ICANON: u32 = 0x2;
const ECHO: u32 = 0x8;

struct St {
    to_slave: VecDeque<u8>,
    line: Vec<u8>,
    to_master: VecDeque<u8>,
    iflag: u32,
    oflag: u32,
    cflag: u32,
    lflag: u32,
    cc: [u8; 19],
    winsz: [u16; 4],
    fg_pgrp: u64,
    eof: bool,
}

pub struct Pty {
    id: u32,
    st: Mutex<St>,
    wq_m: WaitQueue, // master readers / slave writers waiting for space
    wq_s: WaitQueue, // slave readers
    master_open: AtomicBool,
    slaves: AtomicUsize,
    slave_seen: AtomicBool,
}

static PTYS: Mutex<(Vec<Arc<Pty>>, u32)> = Mutex::new((Vec::new(), 0));

impl Pty {
    fn new(id: u32) -> Arc<Self> {
        let mut cc = [0u8; 19];
        cc[0] = 3; // VINTR
        cc[1] = 28; // VQUIT
        cc[2] = 0x7f; // VERASE
        cc[3] = 21; // VKILL
        cc[4] = 4; // VEOF
        cc[6] = 1; // VMIN
        Arc::new(Pty {
            id,
            st: Mutex::new(St {
                to_slave: VecDeque::new(),
                line: Vec::new(),
                to_master: VecDeque::new(),
                iflag: ICRNL | 0x400,
                oflag: OPOST | ONLCR,
                cflag: 0xbf,
                lflag: 0x8a3b,
                cc,
                winsz: [24, 80, 0, 0],
                fg_pgrp: 0,
                eof: false,
            }),
            wq_m: WaitQueue::new(),
            wq_s: WaitQueue::new(),
            master_open: AtomicBool::new(true),
            slaves: AtomicUsize::new(0),
            slave_seen: AtomicBool::new(false),
        })
    }
}

fn echo_out(s: &mut St, c: u8) {
    if c == b'\n' && s.oflag & OPOST != 0 && s.oflag & ONLCR != 0 {
        s.to_master.push_back(b'\r');
    }
    s.to_master.push_back(c);
}

/// One byte typed on the master side goes through the input line discipline.
fn ldisc_input(p: &Pty, mut c: u8) {
    let mut s = p.st.lock();
    if c == b'\r' {
        if s.iflag & IGNCR != 0 {
            return;
        }
        if s.iflag & ICRNL != 0 {
            c = b'\n';
        }
    } else if c == b'\n' && s.iflag & INLCR != 0 {
        c = b'\r';
    }
    let echo = s.lflag & ECHO != 0;
    if s.lflag & ISIG != 0 {
        let sig = if c == s.cc[0] { 2 } else if c == s.cc[1] { 3 } else { 0 };
        if sig != 0 {
            s.line.clear();
            s.to_slave.clear();
            if echo {
                s.to_master.push_back(b'^');
                s.to_master.push_back(c + 64);
                echo_out(&mut s, b'\n');
            }
            let pg = s.fg_pgrp;
            drop(s);
            if pg != 0 {
                crate::signal::send_pgrp(pg, sig);
            }
            p.wq_s.wake_all();
            p.wq_m.wake_all();
            return;
        }
    }
    if s.lflag & ICANON != 0 {
        if c == s.cc[2] || c == 8 {
            if s.line.pop().is_some() && echo {
                s.to_master.extend(b"\x08 \x08".iter().copied());
            }
        } else if c == s.cc[3] {
            let n = s.line.len();
            s.line.clear();
            if echo {
                for _ in 0..n {
                    s.to_master.extend(b"\x08 \x08".iter().copied());
                }
            }
        } else if c == s.cc[4] {
            if s.line.is_empty() {
                s.eof = true;
            } else {
                let l = core::mem::take(&mut s.line);
                s.to_slave.extend(l);
            }
        } else if c == b'\n' {
            let mut l = core::mem::take(&mut s.line);
            l.push(b'\n');
            s.to_slave.extend(l);
            if echo {
                echo_out(&mut s, b'\n');
            }
        } else if s.line.len() < 4096 {
            s.line.push(c);
            if echo {
                if c < 32 && c != b'\t' {
                    s.to_master.push_back(b'^');
                    s.to_master.push_back(c + 64);
                } else {
                    s.to_master.push_back(c);
                }
            }
        }
    } else {
        s.to_slave.push_back(c);
        if echo {
            echo_out(&mut s, c);
        }
    }
    drop(s);
    p.wq_s.wake_all();
    p.wq_m.wake_all();
}

fn tty_ioctl(p: &Pty, cmd: u64, arg: u64) -> Option<i64> {
    Some(match cmd {
        0x5401 => match usercopy::slice_mut(arg, 36) {
            Ok(b) => {
                let s = p.st.lock();
                b.fill(0);
                b[0..4].copy_from_slice(&s.iflag.to_le_bytes());
                b[4..8].copy_from_slice(&s.oflag.to_le_bytes());
                b[8..12].copy_from_slice(&s.cflag.to_le_bytes());
                b[12..16].copy_from_slice(&s.lflag.to_le_bytes());
                b[17..36].copy_from_slice(&s.cc);
                0
            }
            Err(e) => e,
        },
        0x5402 | 0x5403 | 0x5404 => match usercopy::slice(arg, 36) {
            Ok(b) => {
                let mut s = p.st.lock();
                s.iflag = u32::from_le_bytes([b[0], b[1], b[2], b[3]]);
                s.oflag = u32::from_le_bytes([b[4], b[5], b[6], b[7]]);
                s.cflag = u32::from_le_bytes([b[8], b[9], b[10], b[11]]);
                s.lflag = u32::from_le_bytes([b[12], b[13], b[14], b[15]]);
                s.cc.copy_from_slice(&b[17..36]);
                if s.lflag & ICANON == 0 {
                    let l = core::mem::take(&mut s.line);
                    s.to_slave.extend(l); // leaving canonical mode releases the half-typed line
                }
                drop(s);
                p.wq_s.wake_all();
                0
            }
            Err(e) => e,
        },
        0x5413 => match usercopy::slice_mut(arg, 8) {
            Ok(b) => {
                let w = p.st.lock().winsz;
                for (i, v) in w.iter().enumerate() {
                    b[2 * i..2 * i + 2].copy_from_slice(&v.to_le_bytes());
                }
                0
            }
            Err(e) => e,
        },
        0x5414 => match usercopy::slice(arg, 8) {
            Ok(b) => {
                let mut s = p.st.lock();
                for i in 0..4 {
                    s.winsz[i] = u16::from_le_bytes([b[2 * i], b[2 * i + 1]]);
                }
                let pg = s.fg_pgrp;
                drop(s);
                if pg != 0 {
                    crate::signal::send_pgrp(pg, 28); // SIGWINCH
                }
                0
            }
            Err(e) => e,
        },
        0x540f => match usercopy::write_u32(arg, p.st.lock().fg_pgrp as u32) {
            Ok(()) => 0,
            Err(e) => e,
        },
        0x5410 => match usercopy::read_u32(arg) {
            Ok(g) => {
                p.st.lock().fg_pgrp = g as u64;
                0
            }
            Err(e) => e,
        },
        0x540e => {
            // TIOCSCTTY: the caller's session takes this terminal; its group is the foreground one
            if let Some(t) = crate::sched::current().task() {
                p.st.lock().fg_pgrp = t.pgid();
                t.set_ctty(PTYS.lock().0.iter().find(|q| q.id == p.id).cloned());
            }
            0
        }
        0x5429 => match usercopy::write_u32(arg, crate::sched::current().task().map_or(0, |t| t.sid() as u32)) {
            Ok(()) => 0,
            Err(e) => e,
        },
        0x541b => match usercopy::write_u32(arg, p.st.lock().to_slave.len() as u32) {
            Ok(()) => 0,
            Err(e) => e,
        },
        0x80045430 => match usercopy::write_u32(arg, p.id) {
            Ok(()) => 0,
            Err(e) => e,
        },
        0x40045431 | 0x540b | 0x5422 => 0, // TIOCSPTLCK, TCFLSH, TIOCNOTTY
        _ => return None,
    })
}

pub struct PtyMaster {
    p: Arc<Pty>,
    nonblock: AtomicBool,
}

pub struct PtySlave {
    p: Arc<Pty>,
    nonblock: AtomicBool,
}

/// `open("/dev/ptmx")`.
pub fn open_master() -> Arc<PtyMaster> {
    let mut g = PTYS.lock();
    let id = g.1;
    g.1 += 1;
    let p = Pty::new(id);
    g.0.push(p.clone());
    Arc::new(PtyMaster { p, nonblock: AtomicBool::new(false) })
}

/// `open("/dev/tty")` for a task whose controlling terminal is a pty.
pub fn open_ctty(p: &Arc<Pty>) -> Arc<PtySlave> {
    p.slaves.fetch_add(1, Ordering::AcqRel);
    p.slave_seen.store(true, Ordering::Release);
    Arc::new(PtySlave { p: p.clone(), nonblock: AtomicBool::new(false) })
}

/// `open("/dev/pts/N")`.
pub fn open_slave(id: u32) -> Option<Arc<PtySlave>> {
    let p = PTYS.lock().0.iter().find(|p| p.id == id)?.clone();
    p.slaves.fetch_add(1, Ordering::AcqRel);
    p.slave_seen.store(true, Ordering::Release);
    Some(Arc::new(PtySlave { p, nonblock: AtomicBool::new(false) }))
}

impl Drop for PtyMaster {
    fn drop(&mut self) {
        self.p.master_open.store(false, Ordering::Release);
        let pg = self.p.st.lock().fg_pgrp;
        if pg != 0 && self.p.slaves.load(Ordering::Acquire) > 0 {
            crate::signal::send_pgrp(pg, 1); // SIGHUP: the terminal went away
        }
        self.p.wq_s.wake_all();
        self.p.wq_m.wake_all();
        PTYS.lock().0.retain(|q| q.id != self.p.id);
    }
}

impl Drop for PtySlave {
    fn drop(&mut self) {
        self.p.slaves.fetch_sub(1, Ordering::AcqRel);
        self.p.wq_m.wake_all();
    }
}

impl FileOps for PtyMaster {
    fn read(&self, buf: &mut [u8]) -> i64 {
        if buf.is_empty() {
            return 0;
        }
        loop {
            {
                let mut s = self.p.st.lock();
                if !s.to_master.is_empty() {
                    let n = buf.len().min(s.to_master.len());
                    for b in buf.iter_mut().take(n) {
                        *b = s.to_master.pop_front().unwrap();
                    }
                    drop(s);
                    self.p.wq_m.wake_all();
                    return n as i64;
                }
            }
            if self.p.slave_seen.load(Ordering::Acquire) && self.p.slaves.load(Ordering::Acquire) == 0 {
                return -5; // EIO: every slave closed
            }
            if self.nonblock.load(Ordering::Relaxed) {
                return -11;
            }
            if crate::signal::interrupted() {
                return -4;
            }
            self.p.wq_m.wait_if_intr(|| {
                self.p.st.lock().to_master.is_empty()
                    && !(self.p.slave_seen.load(Ordering::Acquire) && self.p.slaves.load(Ordering::Acquire) == 0)
            });
        }
    }
    fn write(&self, data: &[u8]) -> i64 {
        for &c in data {
            ldisc_input(&self.p, c);
        }
        data.len() as i64
    }
    fn seek(&self, _o: i64, _w: u32) -> i64 {
        -29
    }
    fn stat(&self) -> (u32, u64) {
        (S_IFCHR | 0o666, 0)
    }
    fn poll_mask(&self, want: u16) -> u16 {
        let mut r = 0;
        if want & POLLIN != 0 && !self.p.st.lock().to_master.is_empty() {
            r |= POLLIN;
        }
        if self.p.slave_seen.load(Ordering::Acquire) && self.p.slaves.load(Ordering::Acquire) == 0 {
            r |= POLLHUP | (want & POLLIN);
        }
        r | (want & POLLOUT)
    }
    fn tty_ioctl(&self, cmd: u64, arg: u64) -> Option<i64> {
        tty_ioctl(&self.p, cmd, arg)
    }
    fn set_nonblock(&self, on: bool) {
        self.nonblock.store(on, Ordering::Relaxed);
    }
    fn is_nonblock(&self) -> bool {
        self.nonblock.load(Ordering::Relaxed)
    }
}

impl FileOps for PtySlave {
    fn read(&self, buf: &mut [u8]) -> i64 {
        if buf.is_empty() {
            return 0;
        }
        loop {
            {
                let mut s = self.p.st.lock();
                if !s.to_slave.is_empty() {
                    let canon = s.lflag & ICANON != 0;
                    let mut n = 0;
                    while n < buf.len() {
                        let Some(c) = s.to_slave.pop_front() else { break };
                        buf[n] = c;
                        n += 1;
                        if canon && c == b'\n' {
                            break;
                        }
                    }
                    return n as i64;
                }
                if s.eof {
                    s.eof = false;
                    return 0;
                }
            }
            if !self.p.master_open.load(Ordering::Acquire) {
                return 0;
            }
            if self.nonblock.load(Ordering::Relaxed) {
                return -11;
            }
            if crate::signal::interrupted() {
                return -4;
            }
            self.p.wq_s.wait_if_intr(|| {
                let s = self.p.st.lock();
                s.to_slave.is_empty() && !s.eof && self.p.master_open.load(Ordering::Acquire)
            });
        }
    }
    fn write(&self, data: &[u8]) -> i64 {
        let mut done = 0;
        while done < data.len() {
            if !self.p.master_open.load(Ordering::Acquire) {
                return if done == 0 { -5 } else { done as i64 };
            }
            {
                let mut s = self.p.st.lock();
                while done < data.len() && s.to_master.len() < CAP {
                    let c = data[done];
                    echo_out(&mut s, c);
                    done += 1;
                }
            }
            self.p.wq_m.wake_all();
            if done < data.len() {
                if crate::signal::interrupted() {
                    return if done == 0 { -4 } else { done as i64 };
                }
                self.p.wq_m.wait_if_intr(|| self.p.st.lock().to_master.len() >= CAP && self.p.master_open.load(Ordering::Acquire));
            }
        }
        done as i64
    }
    fn seek(&self, _o: i64, _w: u32) -> i64 {
        -29
    }
    fn stat(&self) -> (u32, u64) {
        (S_IFCHR | 0o620, 0)
    }
    fn poll_mask(&self, want: u16) -> u16 {
        let s = self.p.st.lock();
        let mut r = 0;
        if want & POLLIN != 0 && (!s.to_slave.is_empty() || s.eof) {
            r |= POLLIN;
        }
        if !self.p.master_open.load(Ordering::Acquire) {
            r |= POLLHUP | (want & POLLIN);
        }
        r | (want & POLLOUT)
    }
    fn tty_ioctl(&self, cmd: u64, arg: u64) -> Option<i64> {
        tty_ioctl(&self.p, cmd, arg)
    }
    fn set_nonblock(&self, on: bool) {
        self.nonblock.store(on, Ordering::Relaxed);
    }
    fn is_nonblock(&self) -> bool {
        self.nonblock.load(Ordering::Relaxed)
    }
}
