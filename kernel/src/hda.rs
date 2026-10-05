// SPDX-License-Identifier: GPL-2.0-or-later
//! Intel High Definition Audio: one controller, the first codec that has a usable output path, 48 kHz 16-bit stereo
//! playback through `/dev/dsp` (OSS style `write`). Polled: no interrupts, commands through the immediate-command registers.
//!
//! The output path is found by walking the codec's widget graph from every connected speaker / headphone / line-out pin back
//! to a DAC (through mixers and selectors); every such path is unmuted and fed from the same stream, so a laptop plays on the
//! speaker and on the headphone jack alike.

use alloc::vec::Vec;
use core::sync::atomic::{AtomicBool, Ordering};
use spin::Mutex;

use crate::file::FileOps;
use crate::pci;

const GCAP: usize = 0x00;
const GCTL: usize = 0x08;
const STATESTS: usize = 0x0E;
const ICW: usize = 0x60;
const IRR: usize = 0x64;
const ICS: usize = 0x68;

const RING: usize = 0x20000; // 128 KiB cyclic buffer = 4 BDL entries of 32 KiB (about 0.68 s of 48 kHz stereo)
const BDL_OFF: usize = 0;
const BUF_OFF: usize = 0x1000;
const GUARD: usize = 4096;

fn ms(n: u64) {
    crate::timer::sleep_ns(n * 1_000_000);
}

pub struct Hda {
    mmio: *mut u8,
    dma: *mut u8,
    dma_phys: u64,
    sd: usize, // register offset of the output stream descriptor
    codec: u8,
    /// DACs fed by the stream (one per connected output pin path).
    dacs: Vec<u8>,
    configured: bool,
    running: bool,
    wp: u64,
    played: u64,
    last_lpib: u32,
}

unsafe impl Send for Hda {}

impl Hda {
    fn r32(&self, off: usize) -> u32 {
        unsafe { core::ptr::read_volatile(self.mmio.add(off) as *const u32) }
    }
    fn w32(&self, off: usize, v: u32) {
        unsafe { core::ptr::write_volatile(self.mmio.add(off) as *mut u32, v) }
    }
    fn r16(&self, off: usize) -> u16 {
        unsafe { core::ptr::read_volatile(self.mmio.add(off) as *const u16) }
    }
    fn w16(&self, off: usize, v: u16) {
        unsafe { core::ptr::write_volatile(self.mmio.add(off) as *mut u16, v) }
    }
    fn r8(&self, off: usize) -> u8 {
        unsafe { core::ptr::read_volatile(self.mmio.add(off)) }
    }
    fn w8(&self, off: usize, v: u8) {
        unsafe { core::ptr::write_volatile(self.mmio.add(off), v) }
    }

    /// One verb through the immediate-command interface. `v` = the low 20 bits of the command (verb + payload).
    fn verb(&self, nid: u8, v: u32) -> Option<u32> {
        let busy_end = crate::timer::monotonic_ns() + 20_000_000;
        while self.r16(ICS) & 1 != 0 {
            if crate::timer::monotonic_ns() > busy_end {
                return None; // the previous command never finished: do not stack another one on it
            }
        }
        self.w16(ICS, 0x2); // clear a stale "valid"
        self.w32(ICW, ((self.codec as u32) << 28) | ((nid as u32) << 20) | (v & 0xF_FFFF));
        self.w16(ICS, 0x1);
        let end = crate::timer::monotonic_ns() + 20_000_000;
        loop {
            let s = self.r16(ICS);
            if s & 3 == 2 {
                return Some(self.r32(IRR));
            }
            if crate::timer::monotonic_ns() > end {
                self.w16(ICS, 0x2);
                return None;
            }
            core::hint::spin_loop();
        }
    }
    fn param(&self, nid: u8, p: u32) -> u32 {
        self.verb(nid, 0xF0000 | p).unwrap_or(0)
    }
    fn set(&self, nid: u8, verb12: u32, payload: u32) {
        let _ = self.verb(nid, (verb12 << 8) | (payload & 0xFF));
    }
    fn set16(&self, nid: u8, verb4: u32, payload: u32) {
        let _ = self.verb(nid, (verb4 << 16) | (payload & 0xFFFF));
    }

    /// Connection list of a widget (node ids).
    fn connections(&self, nid: u8) -> Vec<u8> {
        let info = self.param(nid, 0x0E);
        let n = (info & 0x7F) as usize;
        let long = info & 0x80 != 0;
        let mut out = Vec::new();
        let mut i = 0;
        while i < n && out.len() < 32 {
            let r = self.verb(nid, 0xF0200 | i as u32).unwrap_or(0);
            if long {
                for k in 0..2 {
                    if i + k < n {
                        out.push(((r >> (16 * k)) & 0x7FFF) as u8);
                    }
                }
                i += 2;
            } else {
                for k in 0..4 {
                    if i + k < n {
                        out.push(((r >> (8 * k)) & 0xFF) as u8);
                    }
                }
                i += 4;
            }
        }
        out
    }

    fn wtype(&self, nid: u8) -> u32 {
        (self.param(nid, 0x09) >> 20) & 0xF
    }

    /// Walk from `nid` towards a DAC; returns the nodes of the path, pin first, DAC last.
    fn find_dac(&self, nid: u8, depth: u32) -> Option<Vec<u8>> {
        if depth > 5 {
            return None;
        }
        for c in self.connections(nid) {
            match self.wtype(c) {
                0 => return Some(alloc::vec![nid, c]),
                2 | 3 => {
                    if let Some(mut p) = self.find_dac(c, depth + 1) {
                        p.insert(0, nid);
                        return Some(p);
                    }
                }
                _ => {}
            }
        }
        None
    }

    fn amp_gain(&self, nid: u8, afg: u8) -> u32 {
        let mut caps = self.param(nid, 0x12);
        if caps == 0 {
            caps = self.param(afg, 0x12);
        }
        let steps = (caps >> 8) & 0x7F;
        let offset = caps & 0x7F;
        if steps == 0 { 0 } else { offset.min(steps) }
    }

    /// Find and program the output paths. Returns the number of DACs that will play.
    fn setup_paths(&mut self) -> usize {
        let root = self.param(0, 0x04);
        let (first, count) = ((root >> 16) & 0xFF, root & 0xFF);
        let mut afg = None;
        for n in first..first + count {
            if self.param(n as u8, 0x05) & 0xFF == 1 {
                afg = Some(n as u8);
                break;
            }
        }
        let Some(afg) = afg else { return 0 };
        self.set(afg, 0x705, 0); // function group to D0
        ms(10);
        let sub = self.param(afg, 0x04);
        let (wfirst, wcount) = ((sub >> 16) & 0xFF, sub & 0xFF);
        let mut paths: Vec<(u8, Vec<u8>)> = Vec::new();
        for n in wfirst..wfirst + wcount {
            let nid = n as u8;
            if self.wtype(nid) != 4 {
                continue;
            }
            let cfg = self.verb(nid, 0xF1C00).unwrap_or(0);
            let (conn, dev) = ((cfg >> 30) & 3, (cfg >> 20) & 0xF);
            if conn == 1 || !matches!(dev, 0 | 1 | 2) {
                continue; // no physical connection, or not an output (line out 0, speaker 1, headphone 2)
            }
            if self.param(nid, 0x0C) & (1 << 4) == 0 {
                continue; // pin cannot drive output
            }
            if let Some(p) = self.find_dac(nid, 0) {
                paths.push((nid, p));
            }
        }
        let mut used = 0;
        for (pin, path) in &paths {
            let dac = *path.last().unwrap();
            if self.dacs.contains(&dac) && used >= 2 {
                continue;
            }
            // program from the DAC back to the pin
            for w in 0..path.len() {
                let nid = path[w];
                let caps = self.param(nid, 0x09);
                self.set(nid, 0x705, 0); // D0
                if caps & (1 << 2) != 0 {
                    // output amplifier: unmute both channels at 0 dB
                    let g = self.amp_gain(nid, afg);
                    self.set16(nid, 0x3, 0xB000 | g);
                }
                if w + 1 < path.len() && caps & (1 << 1) != 0 {
                    // input amplifier: unmute the input we use
                    let idx = self.connections(nid).iter().position(|&c| c == path[w + 1]).unwrap_or(0) as u32;
                    let g = self.amp_gain(nid, afg);
                    self.set16(nid, 0x3, 0x7000 | (idx << 8) | g);
                    if self.wtype(nid) == 3 || self.connections(nid).len() > 1 {
                        self.set(nid, 0x701, idx); // select that connection (selectors, multi-input pins)
                    }
                }
            }
            let pin_caps = self.param(*pin, 0x0C);
            if pin_caps & (1 << 16) != 0 {
                self.set(*pin, 0x70C, 2); // EAPD on: external amplifier powered
            }
            let cfg = self.verb(*pin, 0xF1C00).unwrap_or(0);
            let hp = (cfg >> 20) & 0xF == 2;
            self.set(*pin, 0x707, if hp { 0xC0 } else { 0x40 }); // output enable (+ headphone amp)
            if !self.dacs.contains(&dac) {
                self.dacs.push(dac);
            }
            used += 1;
        }
        self.dacs.len()
    }

    fn start_stream(&mut self) {
        let sd = self.sd;
        // reset the stream descriptor
        self.w8(sd, 1);
        for _ in 0..100 {
            if self.r8(sd) & 1 != 0 {
                break;
            }
            ms(1);
        }
        self.w8(sd, 0);
        for _ in 0..100 {
            if self.r8(sd) & 1 == 0 {
                break;
            }
            ms(1);
        }
        // silence, BDL: four 32 KiB entries
        unsafe { core::ptr::write_bytes(self.dma.add(BUF_OFF), 0, RING) };
        for i in 0..4usize {
            let e = BDL_OFF + i * 16;
            let phys = self.dma_phys + (BUF_OFF + i * 0x8000) as u64;
            unsafe {
                core::ptr::write_volatile(self.dma.add(e) as *mut u64, phys);
                core::ptr::write_volatile(self.dma.add(e + 8) as *mut u32, 0x8000);
                core::ptr::write_volatile(self.dma.add(e + 12) as *mut u32, 0);
            }
        }
        let bdl = self.dma_phys + BDL_OFF as u64;
        self.w32(sd + 0x18, bdl as u32);
        self.w32(sd + 0x1C, (bdl >> 32) as u32);
        self.w32(sd + 0x08, RING as u32);
        self.w16(sd + 0x0C, 3);
        self.w16(sd + 0x12, 0x0011); // 48 kHz, 16 bit, 2 channels
        // stream tag 1 in bits 23:20 of the control register (byte 2)
        self.w8(sd + 2, 0x10);
        for &dac in &self.dacs {
            self.set16(dac, 0x2, 0x0011);
            self.set(dac, 0x706, 0x10); // stream 1, channel 0
            let caps = self.param(dac, 0x09);
            let _ = caps;
        }
        self.wp = 0;
        self.played = 0;
        self.last_lpib = 0;
    }

    fn run(&mut self, on: bool) {
        let v = self.r8(self.sd);
        self.w8(self.sd, if on { v | 2 } else { v & !2 });
        self.running = on;
    }

    /// Fold the DMA position into the byte count the hardware has consumed.
    fn update_played(&mut self) {
        let lpib = self.r32(self.sd + 0x04) % RING as u32;
        let delta = lpib.wrapping_sub(self.last_lpib) as usize % RING;
        self.played += delta as u64;
        self.last_lpib = lpib;
    }

    fn queued(&self) -> usize {
        self.wp.saturating_sub(self.played) as usize
    }
}

static HDA: Mutex<Option<Hda>> = Mutex::new(None);
static OPEN: AtomicBool = AtomicBool::new(false);

pub fn init() -> Result<(), &'static str> {
    // a class 04/03 device whose codec answers: the Intel controller first (a GPU's HDMI audio has no analog output)
    let mut cands = Vec::new();
    for bus in 0..4u8 {
        for dev in 0..32u8 {
            for func in 0..8u8 {
                let loc = pci::Location { bus, dev, func };
                if pci::read16(loc, 0) == 0xFFFF {
                    if func == 0 {
                        break;
                    }
                    continue;
                }
                let class = pci::read32(loc, 8) >> 16;
                if class == 0x0403 {
                    cands.push(loc);
                }
            }
        }
    }
    if cands.is_empty() {
        return Err("no HD Audio controller");
    }
    cands.sort_by_key(|l| (pci::read16(*l, 0) != 0x8086) as u8);
    let mut last_err = "no codec with an output path";
    for loc in cands {
        match try_controller(loc) {
            Ok(h) => {
                *HDA.lock() = Some(h);
                return Ok(());
            }
            Err(e) => last_err = e,
        }
    }
    Err(last_err)
}

fn try_controller(loc: pci::Location) -> Result<Hda, &'static str> {
    let bar = pci::bar(loc, 0);
    if bar == 0 {
        return Err("HDA: BAR0 missing");
    }
    pci::enable_bus_master(loc);
    let mmio = crate::vmm::map_mmio(bar, 0x4000) as *mut u8;
    let (dma_phys, dma_len) = crate::mm::audio_dma();
    if dma_len < (BUF_OFF + RING) as u64 || dma_phys >= 1 << 32 {
        return Err("HDA: DMA region unsuitable");
    }
    let dma = crate::mm::phys_to_virt(x86_64::PhysAddr::new(dma_phys)).as_mut_ptr::<u8>();
    let mut h = Hda { mmio, dma, dma_phys, sd: 0, codec: 0, dacs: Vec::new(), configured: false, running: false, wp: 0, played: 0, last_lpib: 0 };
    // controller reset
    h.w32(GCTL, 0);
    for _ in 0..100 {
        if h.r32(GCTL) & 1 == 0 {
            break;
        }
        ms(1);
    }
    ms(2);
    h.w32(GCTL, 1);
    let mut up = false;
    for _ in 0..200 {
        if h.r32(GCTL) & 1 != 0 {
            up = true;
            break;
        }
        ms(1);
    }
    if !up {
        return Err("HDA: controller did not leave reset");
    }
    ms(10);
    let gcap = h.r16(GCAP);
    let (iss, oss) = (((gcap >> 8) & 0xF) as usize, ((gcap >> 12) & 0xF) as usize);
    if oss == 0 {
        return Err("HDA: no output stream");
    }
    h.sd = 0x80 + 0x20 * iss;
    let codecs = h.r16(STATESTS) & 0x7FFF;
    if codecs == 0 {
        return Err("HDA: no codec present");
    }
    for c in 0..15u8 {
        if codecs & (1 << c) == 0 {
            continue;
        }
        h.codec = c;
        if h.verb(0, 0xF0000).is_none() {
            continue;
        }
        if h.setup_paths() > 0 {
            crate::kprintln!("THOS: hda ok           codec {} vendor/device {:08x}, {} output path(s)", c, h.param(0, 0x00), h.dacs.len());
            return Ok(h);
        }
        h.dacs.clear();
    }
    Err("HDA: no codec with a connected output")
}

/// `/dev/dsp`
pub struct Dsp;

pub fn open_dsp() -> Option<alloc::sync::Arc<dyn FileOps>> {
    if HDA.lock().is_none() || OPEN.swap(true, Ordering::AcqRel) {
        return None;
    }
    Some(alloc::sync::Arc::new(Dsp))
}

impl Drop for Dsp {
    fn drop(&mut self) {
        // let the queued sound finish, then stop
        let end = crate::timer::monotonic_ns() + 3_000_000_000;
        loop {
            let done = {
                let mut g = HDA.lock();
                match g.as_mut() {
                    Some(h) if h.running => {
                        h.update_played();
                        h.queued() == 0
                    }
                    _ => true,
                }
            };
            if done || crate::timer::monotonic_ns() > end {
                break;
            }
            ms(5);
        }
        if let Some(h) = HDA.lock().as_mut() {
            if h.running {
                h.run(false);
            }
            h.configured = false;
        }
        OPEN.store(false, Ordering::Release);
    }
}

impl FileOps for Dsp {
    fn is_tty(&self) -> bool {
        false
    }
    fn read(&self, _buf: &mut [u8]) -> i64 {
        0
    }
    fn write(&self, data: &[u8]) -> i64 {
        let mut done = 0usize;
        // whole stereo frames only
        let data = &data[..data.len() & !3];
        while done < data.len() {
            {
                let mut g = HDA.lock();
                let Some(h) = g.as_mut() else { return -19 };
                if !h.configured {
                    h.start_stream();
                    h.configured = true;
                }
                if h.running {
                    h.update_played();
                }
                let free = RING.saturating_sub(h.queued() + GUARD);
                let n = free.min(data.len() - done) & !3;
                if n > 0 {
                    let start = (h.wp % RING as u64) as usize;
                    let first = n.min(RING - start);
                    unsafe {
                        core::ptr::copy_nonoverlapping(data.as_ptr().add(done), h.dma.add(BUF_OFF + start), first);
                        if n > first {
                            core::ptr::copy_nonoverlapping(data.as_ptr().add(done + first), h.dma.add(BUF_OFF), n - first);
                        }
                    }
                    h.wp += n as u64;
                    done += n;
                    // keep silence ahead of the write pointer, so a drained buffer does not replay old sound
                    let tail = (h.wp % RING as u64) as usize;
                    let z = GUARD.min(RING - tail);
                    unsafe {
                        core::ptr::write_bytes(h.dma.add(BUF_OFF + tail), 0, z);
                        if z < GUARD {
                            core::ptr::write_bytes(h.dma.add(BUF_OFF), 0, GUARD - z);
                        }
                    }
                    if !h.running && h.wp >= (RING / 4) as u64 {
                        h.run(true);
                    }
                }
            }
            if done < data.len() {
                ms(3);
                if crate::signal::interrupted() {
                    return if done > 0 { done as i64 } else { -4 };
                }
            }
        }
        // short sounds (below the start threshold) still have to play
        if let Some(h) = HDA.lock().as_mut() {
            if h.configured && !h.running && h.wp > 0 {
                h.run(true);
            }
        }
        data.len() as i64
    }
    fn seek(&self, _o: i64, _w: u32) -> i64 {
        -29
    }
    fn stat(&self) -> (u32, u64) {
        (0o020666, 0)
    }
    fn poll_mask(&self, want: u16) -> u16 {
        want & crate::file::POLLOUT
    }
    fn ioctl(&self, cmd: u64, arg: u64) -> i64 {
        // OSS: _IOWR('P', nr, int)
        if (cmd >> 8) & 0xFF != 0x50 {
            return -25;
        }
        let put = |v: u32| crate::usercopy::write_u32(arg, v).map(|_| 0).unwrap_or(-14);
        match cmd & 0xFF {
            0 | 1 | 4 => 0,        // RESET, SYNC, POST
            2 => put(48000),       // SPEED: the device runs at 48 kHz
            3 => put(1),           // STEREO
            5 => put(0x10),        // SETFMT: signed 16-bit little endian
            6 => put(2),           // CHANNELS
            0x0B => put(0x10),     // GETFMTS
            _ => 0,
        }
    }
}

