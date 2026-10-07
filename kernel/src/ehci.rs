// SPDX-License-Identifier: GPL-2.0-or-later
//! EHCI (USB 2.0) host controller and a USB HID boot-protocol keyboard, polled with control transfers.
//!
//! The Acer's two EHCI controllers (Intel 5 Series) have only an internal rate-matching hub on their root
//! ports, so devices sit behind a high-speed hub: this driver walks root ports and (one level of) hubs,
//! addresses every device with control transfers on the asynchronous schedule (full/low-speed devices
//! behind a hub use split transactions, which for control endpoints is just the hub-address/port fields
//! of the queue head), and picks the first HID boot keyboard it finds. Reports are fetched with the HID
//! class request GET_REPORT every few milliseconds — no periodic schedule needed.

use alloc::vec::Vec;

use crate::mm::{phys_to_virt, FRAME_ALLOC};
use crate::pci;

fn r32(a: u64) -> u32 {
    unsafe { core::ptr::read_volatile(a as *const u32) }
}
fn w32(a: u64, v: u32) {
    unsafe { core::ptr::write_volatile(a as *mut u32, v) }
}
fn ms(n: u64) {
    crate::timer::sleep_ns(n * 1_000_000);
}

const CMD: u64 = 0x00;
const STS: u64 = 0x04;
const CTRLDSSEG: u64 = 0x10;
const ASYNCLIST: u64 = 0x18;
const CONFIGFLAG: u64 = 0x40;
const PORTSC: u64 = 0x44;

const CMD_RUN: u32 = 1;
const CMD_RESET: u32 = 2;
const CMD_ASYNC: u32 = 1 << 5;
const STS_HALTED: u32 = 1 << 12;
const STS_ASS: u32 = 1 << 15;

/// A USB mass-storage device (Bulk-Only Transport, SCSI transparent command set): a stick or an external disk.
struct Msc {
    dev: Dev,
    ep_in: u8,
    ep_out: u8,
    mps_in: u16,
    mps_out: u16,
    toggle_in: bool,
    toggle_out: bool,
    tag: u32,
    sectors: u64,
}

enum Data<'a> {
    In(&'a mut [u8]),
    Out(&'a [u8]),
}

pub struct Ehci {
    op: u64,
    ports: u32,
    wide: bool,
    // one page: QH head (0x000), QH device (0x080), qTDs (0x100..), setup (0x400), data (0x440)
    dma_phys: u64,
    dma: *mut u8,
    kbd: Option<Kbd>,
    msc: Option<Msc>,
    /// USB bulk DMA area: queue head 0x0000, qTDs 0x0100.., CBW/CSW 0x1000, data bounce 0x2000..0x12000.
    ud_phys: u64,
    ud: *mut u8,
}

#[derive(Clone, Copy)]
struct Dev {
    addr: u8,
    speed: u8, // 0 full, 1 low, 2 high (EHCI EPS encoding)
    mps: u16,
    hub_addr: u8,
    hub_port: u8,
}

struct Kbd {
    dev: Dev,
    iface: u8,
}

unsafe impl Send for Ehci {}

fn setup_pkt(bm: u8, req: u8, val: u16, idx: u16, len: u16) -> [u8; 8] {
    let mut s = [0u8; 8];
    s[0] = bm;
    s[1] = req;
    s[2..4].copy_from_slice(&val.to_le_bytes());
    s[4..6].copy_from_slice(&idx.to_le_bytes());
    s[6..8].copy_from_slice(&len.to_le_bytes());
    s
}

impl Ehci {
    fn p32(&self, off: usize, v: u32) {
        unsafe { core::ptr::write_volatile(self.dma.add(off) as *mut u32, v) }
    }
    fn g32(&self, off: usize) -> u32 {
        unsafe { core::ptr::read_volatile(self.dma.add(off) as *const u32) }
    }
    fn cmd(&self, set: u32, clear: u32) {
        w32(self.op + CMD, (r32(self.op + CMD) | set) & !clear);
    }
    fn wait_sts(&self, mask: u32, want: bool) -> bool {
        for _ in 0..500 {
            if (r32(self.op + STS) & mask != 0) == want {
                return true;
            }
            ms(1);
        }
        false
    }

    /// One control transfer on endpoint 0 of `dev`. `dir_in`: data stage from the device. Returns the
    /// bytes of the data stage.
    fn control(&mut self, dev: Dev, setup: [u8; 8], dir_in: bool, len: usize) -> Result<Vec<u8>, &'static str> {
        let len = len.min(0x200);
        // stop the asynchronous schedule while the queue head is rewritten
        self.cmd(0, CMD_ASYNC);
        if !self.wait_sts(STS_ASS, false) {
            return Err("EHCI: async schedule did not stop");
        }
        unsafe {
            core::ptr::copy_nonoverlapping(setup.as_ptr(), self.dma.add(0x400), 8);
            core::ptr::write_bytes(self.dma.add(0x440), 0, 0x200);
        }
        let ph = |off: u64| self.dma_phys + off;
        // qTDs: SETUP, [DATA], STATUS
        let td = |i: usize| 0x100 + i * 64;
        let tok = |pid: u32, toggle: u32, bytes: usize, ioc: bool| -> u32 {
            (toggle << 31) | ((bytes as u32) << 16) | ((ioc as u32) << 15) | (3 << 10) | (pid << 8) | 0x80
        };
        let have_data = len > 0;
        let n_td = if have_data { 3 } else { 2 };
        for i in 0..n_td {
            let off = td(i);
            for k in 0..16 {
                self.p32(off + k * 4, 0);
            }
            let last = i == n_td - 1;
            self.p32(off, if last { 1 } else { ph(td(i + 1) as u64) as u32 });
            self.p32(off + 4, 1);
        }
        let set_buf = |s: &Self, off: usize, phys: u64| {
            s.p32(off + 12, phys as u32);
            s.p32(off + 16, (phys as u32 & !0xFFF).wrapping_add(0x1000));
            s.p32(off + 44, (phys >> 32) as u32);
        };
        // SETUP (PID 2)
        self.p32(td(0) + 8, tok(2, 0, 8, false));
        set_buf(self, td(0), ph(0x400));
        let status_idx = n_td - 1;
        if have_data {
            self.p32(td(1) + 8, tok(if dir_in { 1 } else { 0 }, 1, len, false));
            set_buf(self, td(1), ph(0x440));
        }
        // STATUS: opposite direction of the data stage (IN when there is no data), toggle 1, IOC
        self.p32(td(status_idx) + 8, tok(if have_data && dir_in { 0 } else { 1 }, 1, 0, true));

        // queue head of the device
        let eps = dev.speed as u32;
        let ctrl_flag = (dev.speed != 2) as u32;
        self.p32(0x80, (ph(0) as u32) | 2); // horizontal link: back to the head QH
        self.p32(0x84, (15 << 28) | (ctrl_flag << 27) | ((dev.mps as u32) << 16) | (1 << 14) | (eps << 12) | dev.addr as u32);
        self.p32(0x88, (1 << 30) | ((dev.hub_addr as u32) << 23) | ((dev.hub_port as u32) << 16));
        self.p32(0x8C, 0);
        self.p32(0x90, ph(td(0) as u64) as u32); // next qTD
        self.p32(0x94, 1);
        for k in 0..12 {
            self.p32(0x98 + k * 4, 0); // overlay: inactive
        }
        // head QH: H=1, links to the device QH
        self.p32(0x00, (ph(0x80) as u32) | 2);
        self.p32(0x04, (1 << 15) | (1 << 14) | (2 << 12) | 0x40);
        self.p32(0x08, 0);
        self.p32(0x10, 1);
        self.p32(0x14, 1);
        self.p32(0x18, 0x40); // halted overlay
        w32(self.op + ASYNCLIST, ph(0) as u32);
        self.cmd(CMD_ASYNC, 0);
        if !self.wait_sts(STS_ASS, true) {
            return Err("EHCI: async schedule did not start");
        }
        // wait for the STATUS qTD to finish (Active clear) or any qTD to halt
        let mut done = false;
        for _ in 0..1000 {
            let st: Vec<u32> = (0..n_td).map(|i| self.g32(td(i) + 8)).collect();
            if st.iter().any(|t| t & 0x40 != 0 || t & 0x1C != 0) {
                self.cmd(0, CMD_ASYNC);
                return Err("EHCI: transfer error (stall / babble / xact error)");
            }
            if st[status_idx] & 0x80 == 0 {
                done = true;
                break;
            }
            ms(1);
        }
        self.cmd(0, CMD_ASYNC);
        if !done {
            return Err("EHCI: control transfer timed out");
        }
        let mut out = Vec::new();
        if have_data && dir_in {
            let left = ((self.g32(td(1) + 8) >> 16) & 0x7FFF) as usize;
            let got = len.saturating_sub(left);
            out.extend_from_slice(unsafe { core::slice::from_raw_parts(self.dma.add(0x440), got) });
        }
        Ok(out)
    }

    fn get_descriptor(&mut self, dev: Dev, ty: u8, idx: u8, len: usize) -> Result<Vec<u8>, &'static str> {
        self.control(dev, setup_pkt(0x80, 6, ((ty as u16) << 8) | idx as u16, 0, len as u16), true, len)
    }

    /// Address and configure the device on a freshly reset port (it answers at address 0).
    fn enumerate(&mut self, speed: u8, hub_addr: u8, hub_port: u8, next_addr: &mut u8) -> Result<Dev, &'static str> {
        let mut dev = Dev { addr: 0, speed, mps: if speed == 2 { 64 } else { 8 }, hub_addr, hub_port };
        let d8 = self.get_descriptor(dev, 1, 0, 8)?;
        if d8.len() < 8 {
            return Err("EHCI: short device descriptor");
        }
        dev.mps = d8[7].max(8) as u16;
        let addr = *next_addr;
        *next_addr += 1;
        self.control(dev, setup_pkt(0, 5, addr as u16, 0, 0), false, 0)?;
        ms(10);
        dev.addr = addr;
        Ok(dev)
    }

    /// Reset root port `p` (or a hub port via `reset_hub_port`) and report the device speed.
    fn reset_root_port(&mut self, p: u32) -> Option<u8> {
        let a = self.op + PORTSC + 4 * p as u64;
        let keep = |v: u32| v & !((1 << 1) | (1 << 3) | (1 << 5)); // do not write the change bits as 1
        w32(a, keep(r32(a)) | (1 << 8)); // port reset
        ms(60);
        w32(a, keep(r32(a)) & !(1 << 8));
        for _ in 0..50 {
            if r32(a) & (1 << 8) == 0 {
                break;
            }
            ms(2);
        }
        ms(10);
        if r32(a) & 4 != 0 {
            Some(2) // enabled after reset = high speed
        } else {
            // a low/full-speed device: it belongs to a companion controller; this driver has none
            w32(a, keep(r32(a)) | (1 << 13));
            None
        }
    }

    fn hub_ports(&mut self, hub: Dev) -> Result<(), &'static str> {
        // SET_CONFIGURATION 1, hub descriptor, power all ports
        self.control(hub, setup_pkt(0, 9, 1, 0, 0), false, 0)?;
        let hd = self.control(hub, setup_pkt(0xA0, 6, 0x2900, 0, 8), true, 8)?;
        let n = *hd.get(2).ok_or("EHCI: short hub descriptor")? as u16;
        crate::kprintln!("THOS: usb              hub with {} ports", n);
        for p in 1..=n {
            let _ = self.control(hub, setup_pkt(0x23, 3, 8, p, 0), false, 0); // PORT_POWER
        }
        ms(200);
        let mut next = 20u8;
        for p in 1..=n {
            let st = self.control(hub, setup_pkt(0xA3, 0, 0, p, 4), true, 4)?;
            if st.len() < 4 || st[0] & 1 == 0 {
                continue; // nothing connected
            }
            crate::kprintln!("THOS: usb              hub port {}: device connected (status {:02x}{:02x})", p, st[1], st[0]);
            let _ = self.control(hub, setup_pkt(0x23, 3, 4, p, 0), false, 0); // PORT_RESET
            ms(80);
            let st = self.control(hub, setup_pkt(0xA3, 0, 0, p, 4), true, 4)?;
            if st[0] & 2 == 0 {
                crate::kprintln!("THOS: usb              hub port {}: not enabled after reset (status {:02x}{:02x})", p, st[1], st[0]);
                continue; // not enabled
            }
            let speed = if st[1] & 4 != 0 { 2 } else if st[1] & 2 != 0 { 1 } else { 0 };
            let _ = self.control(hub, setup_pkt(0x23, 1, 0x10, p, 0), false, 0); // clear C_PORT_RESET
            match self.enumerate(speed, hub.addr, p as u8, &mut next) {
                Ok(dev) => {
                    if let Ok(dd) = self.get_descriptor(dev, 1, 0, 18) {
                        crate::kprintln!("THOS: usb              hub port {}: device {:02x}{:02x}:{:02x}{:02x} class {} speed {}", p, dd.get(9).copied().unwrap_or(0), dd.get(8).copied().unwrap_or(0), dd.get(11).copied().unwrap_or(0), dd.get(10).copied().unwrap_or(0), dd.get(4).copied().unwrap_or(0), speed);
                    }
                    let _ = self.try_keyboard(dev); // keyboard and/or mass storage: look at every port
                }
                Err(why) => crate::kprintln!("THOS: usb              hub port {}: could not enumerate: {}", p, why),
            }
        }
        Ok(())
    }

    /// Is `dev` a HID boot keyboard? If so configure it and remember it.
    fn try_keyboard(&mut self, dev: Dev) -> bool {
        let Ok(dd) = self.get_descriptor(dev, 1, 0, 18) else { return false };
        let class = dd.get(4).copied().unwrap_or(0xFF);
        let Ok(cfg) = self.get_descriptor(dev, 2, 0, 64) else { return false };
        // walk interface descriptors
        let (mut off, mut hid_iface) = (0usize, None);
        while off + 2 <= cfg.len() && cfg[off] != 0 {
            if cfg[off + 1] == 4 && off + 9 <= cfg.len() && cfg[off + 5] == 3 && cfg[off + 6] == 1 && cfg[off + 7] == 1 {
                hid_iface = Some(cfg[off + 2]);
                break;
            }
            off += cfg[off] as usize;
        }
        if let (Some(iface), true) = (hid_iface, self.kbd.is_none()) {
            let cfgval = cfg.get(5).copied().unwrap_or(1);
            if self.control(dev, setup_pkt(0, 9, cfgval as u16, 0, 0), false, 0).is_err() {
                return false;
            }
            let _ = self.control(dev, setup_pkt(0x21, 0x0B, 0, iface as u16, 0), false, 0); // SET_PROTOCOL(boot)
            let _ = self.control(dev, setup_pkt(0x21, 0x0A, 0, iface as u16, 0), false, 0); // SET_IDLE(0)
            self.kbd = Some(Kbd { dev, iface });
            return true;
        }
        if class == 9 {
            // a hub: configure and look behind it
            return self.hub_ports(dev).is_ok() && (self.kbd.is_some() || self.msc.is_some());
        }
        if self.msc.is_none() {
            self.try_msc(dev, &cfg);
        }
        false
    }

    /// Is `dev` a Bulk-Only mass-storage device (class 8, subclass 6 SCSI, protocol 0x50)? If so bring it up.
    fn try_msc(&mut self, dev: Dev, cfg: &[u8]) {
        let (mut off, mut in_iface) = (0usize, false);
        let (mut ep_in, mut ep_out, mut mps_in, mut mps_out) = (0u8, 0u8, 0u16, 0u16);
        while off + 2 <= cfg.len() && cfg[off] != 0 {
            let len = cfg[off] as usize;
            if cfg[off + 1] == 4 && off + 9 <= cfg.len() {
                in_iface = cfg[off + 5] == 8 && cfg[off + 6] == 6 && cfg[off + 7] == 0x50;
            } else if cfg[off + 1] == 5 && in_iface && off + 7 <= cfg.len() && cfg[off + 3] & 3 == 2 {
                let (a, m) = (cfg[off + 2], u16::from_le_bytes([cfg[off + 4], cfg[off + 5]]));
                if a & 0x80 != 0 && ep_in == 0 {
                    ep_in = a & 0xF;
                    mps_in = m;
                } else if a & 0x80 == 0 && ep_out == 0 {
                    ep_out = a & 0xF;
                    mps_out = m;
                }
            }
            off += len.max(2);
        }
        if ep_in == 0 || ep_out == 0 {
            return;
        }
        let cfgval = cfg.get(5).copied().unwrap_or(1);
        if self.control(dev, setup_pkt(0, 9, cfgval as u16, 0, 0), false, 0).is_err() {
            return;
        }
        let mut m = Msc { dev, ep_in, ep_out, mps_in, mps_out, toggle_in: false, toggle_out: false, tag: 1, sectors: 0 };
        // spin-up: wait until the unit answers TEST UNIT READY
        let mut ready = false;
        for _ in 0..40 {
            if self.scsi(&mut m, &[0x00, 0, 0, 0, 0, 0], None).is_ok() {
                ready = true;
                break;
            }
            let mut sense = [0u8; 18];
            let _ = self.scsi(&mut m, &[0x03, 0, 0, 0, 18, 0], Some(Data::In(&mut sense)));
            ms(250);
        }
        if !ready {
            return;
        }
        let mut cap = [0u8; 8];
        if self.scsi(&mut m, &[0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0], Some(Data::In(&mut cap))).is_err() {
            return;
        }
        let last = u32::from_be_bytes([cap[0], cap[1], cap[2], cap[3]]) as u64;
        let bs = u32::from_be_bytes([cap[4], cap[5], cap[6], cap[7]]);
        if bs != 512 {
            return;
        }
        m.sectors = last + 1;
        self.msc = Some(m);
    }

    // ---- bulk transfers and Bulk-Only Transport ----

    /// One bulk transfer of `len` bytes between the device and the DMA area at offset `off`. Returns the bytes moved.
    fn bulk(&mut self, dev: Dev, ep: u8, is_in: bool, mps: u16, toggle: &mut bool, off: usize, len: usize) -> Result<usize, &'static str> {
        const CHUNK: usize = 16384;
        let n_td = len.div_ceil(CHUNK).max(1);
        if n_td > 8 {
            return Err("EHCI: bulk transfer too large");
        }
        let ph = |o: usize| self.ud_phys + o as u64;
        let w = |s: &Self, o: usize, v: u32| unsafe { core::ptr::write_volatile(s.ud.add(o) as *mut u32, v) };
        let r = |s: &Self, o: usize| unsafe { core::ptr::read_volatile(s.ud.add(o) as *const u32) };
        self.cmd(0, CMD_ASYNC);
        if !self.wait_sts(STS_ASS, false) {
            return Err("EHCI: async schedule did not stop");
        }
        let mut tg = *toggle;
        let mut sent = 0usize;
        for i in 0..n_td {
            let t = 0x100 + i * 64;
            let bytes = (len - sent).min(CHUNK);
            for k in 0..16 {
                w(self, t + k * 4, 0);
            }
            let last = i == n_td - 1;
            w(self, t, if last { 1 } else { ph(0x100 + (i + 1) * 64) as u32 });
            w(self, t + 4, 1);
            let pid: u32 = if is_in { 1 } else { 0 };
            w(self, t + 8, ((tg as u32) << 31) | ((bytes as u32) << 16) | ((last as u32) << 15) | (3 << 10) | (pid << 8) | 0x80);
            let phys = ph(off + sent);
            w(self, t + 12, phys as u32);
            for k in 1..5 {
                w(self, t + 12 + k * 4, ((phys as u32) & !0xFFF).wrapping_add(0x1000 * k as u32));
            }
            let packets = bytes.div_ceil(mps as usize).max(1);
            if packets % 2 == 1 {
                tg = !tg;
            }
            sent += bytes;
        }
        // the endpoint's queue head, linked into the ring behind the head QH of the control path
        let eps = dev.speed as u32;
        w(self, 0x00, self.dma_phys as u32 | 2); // horizontal link: back to the head QH (in the control frame)
        w(self, 0x04, (15 << 28) | ((mps as u32) << 16) | (1 << 14) | (eps << 12) | ((ep as u32) << 8) | dev.addr as u32);
        w(self, 0x08, (1 << 30) | ((dev.hub_addr as u32) << 23) | ((dev.hub_port as u32) << 16));
        w(self, 0x0C, 0);
        w(self, 0x10, ph(0x100) as u32);
        w(self, 0x14, 1);
        for k in 0..12 {
            w(self, 0x18 + k * 4, 0);
        }
        // the control frame's head QH: H=1, horizontal link -> the bulk QH
        self.p32(0x00, (self.ud_phys as u32) | 2);
        self.p32(0x04, (1 << 15) | (1 << 14) | (2 << 12) | 0x40);
        self.p32(0x08, 0);
        self.p32(0x10, 1);
        self.p32(0x14, 1);
        self.p32(0x18, 0x40);
        w32(self.op + ASYNCLIST, self.dma_phys as u32);
        self.cmd(CMD_ASYNC, 0);
        if !self.wait_sts(STS_ASS, true) {
            return Err("EHCI: async schedule did not start");
        }
        let mut result: Result<usize, &'static str> = Err("EHCI: bulk transfer timed out");
        let mut failed = false;
        for _ in 0..5000 {
            let mut all_done = true;
            for i in 0..n_td {
                let tok = r(self, 0x100 + i * 64 + 8);
                if tok & 0x40 != 0 || tok & 0x38 != 0 {
                    result = Err(if tok & 0x40 != 0 && tok & 0x38 == 0 { "EHCI: bulk stall" } else { "EHCI: bulk transfer error" });
                    failed = true;
                    all_done = false;
                    break;
                }
                if tok & 0x80 != 0 {
                    all_done = false;
                    break;
                }
            }
            if failed {
                break;
            }
            if all_done {
                let mut left = 0usize;
                for i in 0..n_td {
                    left += ((r(self, 0x100 + i * 64 + 8) >> 16) & 0x7FFF) as usize;
                }
                result = Ok(len - left);
                break;
            }
            ms(1);
        }
        self.cmd(0, CMD_ASYNC);
        let _ = self.wait_sts(STS_ASS, false);
        if result.is_ok() {
            *toggle = tg;
        }
        result
    }

    /// One Bulk-Only command: CBW, optional data stage (through the bounce buffer), CSW.
    fn scsi(&mut self, m: &mut Msc, cdb: &[u8], data: Option<Data>) -> Result<(), &'static str> {
        const CBW: usize = 0x1000;
        const BUF: usize = 0x2000;
        let (len, is_in) = match &data {
            Some(Data::In(b)) => (b.len(), true),
            Some(Data::Out(b)) => (b.len(), false),
            None => (0, false),
        };
        if len > 0x10000 {
            return Err("USB: transfer too large");
        }
        let mut cbw = [0u8; 31];
        cbw[0..4].copy_from_slice(&0x4342_5355u32.to_le_bytes());
        let tag = m.tag;
        m.tag = m.tag.wrapping_add(1);
        cbw[4..8].copy_from_slice(&tag.to_le_bytes());
        cbw[8..12].copy_from_slice(&(len as u32).to_le_bytes());
        cbw[12] = if is_in { 0x80 } else { 0 };
        cbw[14] = cdb.len() as u8;
        cbw[15..15 + cdb.len()].copy_from_slice(cdb);
        unsafe { core::ptr::copy_nonoverlapping(cbw.as_ptr(), self.ud.add(CBW), 31) };
        if let Some(Data::Out(b)) = &data {
            unsafe { core::ptr::copy_nonoverlapping(b.as_ptr(), self.ud.add(BUF), b.len()) };
        }
        let dev = m.dev;
        let (epo, epi, mo, mi) = (m.ep_out, m.ep_in, m.mps_out, m.mps_in);
        let (mut tin, mut tout) = (m.toggle_in, m.toggle_out);

        // Bulk-Only reset recovery: class request, then clear both halts, toggles back to 0
        macro_rules! fail {
            ($e:expr) => {{
                let _ = self.control(dev, setup_pkt(0x21, 0xFF, 0, 0, 0), false, 0);
                let _ = self.control(dev, setup_pkt(0x02, 1, 0, 0x80 | epi as u16, 0), false, 0);
                let _ = self.control(dev, setup_pkt(0x02, 1, 0, epo as u16, 0), false, 0);
                m.toggle_in = false;
                m.toggle_out = false;
                return Err($e);
            }};
        }

        if let Err(e) = self.bulk(dev, epo, false, mo, &mut tout, CBW, 31) {
            fail!(e);
        }
        if len > 0 {
            let r = if is_in {
                self.bulk(dev, epi, true, mi, &mut tin, BUF, len)
            } else {
                self.bulk(dev, epo, false, mo, &mut tout, BUF, len)
            };
            match r {
                Ok(_) => {}
                Err("EHCI: bulk stall") => {
                    // a stalled data stage: clear the halt, the CSW still follows
                    let ep = if is_in { 0x80 | epi as u16 } else { epo as u16 };
                    let _ = self.control(dev, setup_pkt(0x02, 1, 0, ep, 0), false, 0);
                    if is_in {
                        tin = false;
                    } else {
                        tout = false;
                    }
                }
                Err(e) => fail!(e),
            }
        }
        if let Err(e) = self.bulk(dev, epi, true, mi, &mut tin, CBW, 13) {
            fail!(e);
        }
        m.toggle_in = tin;
        m.toggle_out = tout;
        let mut csw = [0u8; 13];
        unsafe { core::ptr::copy_nonoverlapping(self.ud.add(CBW), csw.as_mut_ptr(), 13) };
        if u32::from_le_bytes([csw[0], csw[1], csw[2], csw[3]]) != 0x5342_5355 || u32::from_le_bytes([csw[4], csw[5], csw[6], csw[7]]) != tag {
            fail!("USB: bad CSW");
        }
        if csw[12] != 0 {
            return Err("USB: command failed");
        }
        if let Some(Data::In(b)) = data {
            unsafe { core::ptr::copy_nonoverlapping(self.ud.add(BUF), b.as_mut_ptr(), b.len()) };
        }
        Ok(())
    }

    pub fn has_msc(&self) -> bool {
        self.msc.is_some()
    }

    pub fn msc_sectors(&self) -> u64 {
        self.msc.as_ref().map_or(0, |m| m.sectors)
    }

    /// READ(10)/WRITE(10) in pieces of at most 64 KiB, each retried a few times (sticks glitch on the first command after a pause).
    fn msc_rw(&mut self, lba: u64, buf: &mut [u8], write: bool) -> Result<(), &'static str> {
        let mut m = self.msc.take().ok_or("USB: no mass-storage device")?;
        let mut res = Ok(());
        let total = buf.len() / 512;
        let mut done = 0usize;
        while done < total {
            let n = (total - done).min(128);
            let at = lba + done as u64;
            let op = if write { 0x2A } else { 0x28 };
            let cdb = [op, 0, (at >> 24) as u8, (at >> 16) as u8, (at >> 8) as u8, at as u8, 0, (n >> 8) as u8, n as u8, 0];
            let mut ok = Err("USB: transfer failed");
            for _ in 0..3 {
                let chunk = &mut buf[done * 512..(done + n) * 512];
                ok = if write { self.scsi(&mut m, &cdb, Some(Data::Out(chunk))) } else { self.scsi(&mut m, &cdb, Some(Data::In(chunk))) };
                if ok.is_ok() {
                    break;
                }
            }
            if let Err(e) = ok {
                res = Err(e);
                break;
            }
            done += n;
        }
        self.msc = Some(m);
        res
    }

    pub fn msc_read(&mut self, lba: u64, buf: &mut [u8]) -> Result<(), &'static str> {
        self.msc_rw(lba, buf, false)
    }

    pub fn msc_write(&mut self, lba: u64, buf: &[u8]) -> Result<(), &'static str> {
        let mut copy = buf.to_vec();
        self.msc_rw(lba, &mut copy, true)
    }

    pub fn msc_flush(&mut self) -> Result<(), &'static str> {
        let mut m = self.msc.take().ok_or("USB: no mass-storage device")?;
        let r = self.scsi(&mut m, &[0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0], None);
        self.msc = Some(m);
        r
    }

    /// Fetch the keyboard's current 8-byte boot report.
    pub fn poll_keyboard(&mut self) -> Option<[u8; 8]> {
        let (dev, iface) = {
            let k = self.kbd.as_ref()?;
            (k.dev, k.iface)
        };
        let r = self.control(dev, setup_pkt(0xA1, 1, 0x0100, iface as u16, 8), true, 8).ok()?;
        if r.len() < 8 {
            return None;
        }
        let mut rep = [0u8; 8];
        rep.copy_from_slice(&r[..8]);
        Some(rep)
    }

    pub fn has_keyboard(&self) -> bool {
        self.kbd.is_some()
    }
}

/// Bring up every EHCI controller until one has the stick (or a keyboard), trying up to three rounds: a stick can need seconds to
/// show up. Every step is logged, because a hang or failure on real hardware can only be diagnosed from the screen.
pub fn init() -> Result<Ehci, &'static str> {
    let locs = pci::find_class_all(0x0C, 0x03, 0x20);
    if locs.is_empty() {
        return Err("no EHCI controller");
    }
    crate::kprintln!("THOS: usb              {} EHCI controller(s) on PCI bus 0", locs.len());
    let mut last = "EHCI: no controller brought up";
    let mut kbd_only: Option<Ehci> = None;
    for round in 0..3 {
        for &loc in &locs {
            match init_at(loc, round) {
                Ok(e) if e.has_msc() => return Ok(e),
                Ok(e) => {
                    if kbd_only.is_none() {
                        kbd_only = Some(e);
                    }
                }
                Err(why) => last = why,
            }
        }
        if round < 2 {
            crate::kprintln!("THOS: usb              no storage yet (round {}), waiting 2 s and trying again", round + 1);
            ms(2000);
        }
    }
    match kbd_only {
        Some(e) => Ok(e),
        None => Err(last),
    }
}

fn init_at(loc: pci::Location, round: u32) -> Result<Ehci, &'static str> {
    pci::enable_bus_master(loc);
    let bar = pci::bar(loc, 0);
    if bar == 0 {
        return Err("EHCI BAR0 is zero");
    }
    let base = crate::vmm::map_mmio(bar, 0x1000);
    let caplen = (r32(base) & 0xFF) as u64;
    let hcs = r32(base + 4);
    let hcc = r32(base + 8);
    let op = base + caplen;
    let ports = hcs & 0xF;
    if round == 0 {
        crate::kprintln!("THOS: usb              EHCI at {:02x}:{:02x}.{} BAR {:#x}, {} ports, port power control {}", loc.bus, loc.dev, loc.func, bar, ports, if hcs & (1 << 4) != 0 { "yes" } else { "no" });
    }

    // BIOS -> OS hand-off (USB legacy support capability in PCI config space)
    let mut eecp = ((hcc >> 8) & 0xFF) as u8;
    while eecp >= 0x40 {
        let cap = pci::read32(loc, eecp);
        if cap & 0xFF == 1 {
            pci::write32(loc, eecp, cap | (1 << 24)); // OS owned semaphore
            for _ in 0..200 {
                if pci::read32(loc, eecp) & (1 << 16) == 0 {
                    break; // BIOS owned cleared
                }
                ms(10);
            }
            pci::write32(loc, eecp + 4, 0); // no SMIs
            break;
        }
        eecp = ((cap >> 8) & 0xFF) as u8;
    }

    // stop + reset
    w32(op + CMD, r32(op + CMD) & !CMD_RUN);
    for _ in 0..50 {
        if r32(op + STS) & STS_HALTED != 0 {
            break;
        }
        ms(1);
    }
    w32(op + CMD, CMD_RESET);
    for _ in 0..100 {
        if r32(op + CMD) & CMD_RESET == 0 {
            break;
        }
        ms(2);
    }
    let wide = hcc & 1 != 0;
    let frame = FRAME_ALLOC.lock().alloc().ok_or("EHCI: no DMA frame")?;
    let dma_phys = frame.start_address().as_u64();
    if !wide && dma_phys >= 1 << 32 {
        return Err("EHCI: DMA frame above 4 GiB on a 32-bit controller");
    }
    let dma = phys_to_virt(frame.start_address()).as_mut_ptr::<u8>();
    unsafe { core::ptr::write_bytes(dma, 0, 4096) };
    w32(op + CTRLDSSEG, 0);
    w32(op + CMD, CMD_RUN | (8 << 16)); // run, 8 micro-frame interrupt threshold (no interrupts used)
    w32(op + CONFIGFLAG, 1); // route all ports to this controller
    ms(20);

    let (ud_phys, _) = crate::mm::usb_dma();
    let ud = phys_to_virt(x86_64::PhysAddr::new(ud_phys)).as_mut_ptr::<u8>();
    if ud_phys >= 1 << 32 {
        return Err("EHCI: USB DMA region above 4 GiB");
    }
    unsafe { core::ptr::write_bytes(ud, 0, 0x2000) };
    let mut e = Ehci { op, ports, wide, dma_phys, dma, kbd: None, msc: None, ud_phys, ud };
    let _ = e.wide;
    for p in 0..ports {
        let a = op + PORTSC + 4 * p as u64;
        if hcs & (1 << 4) != 0 {
            w32(a, r32(a) | (1 << 12)); // port power
        }
    }
    ms(100);
    let mut next = 1u8;
    for p in 0..e.ports {
        let a = op + PORTSC + 4 * p as u64;
        let sc = r32(a);
        if sc & 1 == 0 {
            continue; // nothing connected
        }
        let Some(speed) = e.reset_root_port(p) else {
            crate::kprintln!("THOS: usb              port {} PORTSC {:#x}: a full/low-speed device: handed to the companion controller (not supported)", p + 1, sc);
            continue;
        };
        match e.enumerate(speed, 0, 0, &mut next) {
            Ok(dev) => {
                if let Ok(dd) = e.get_descriptor(dev, 1, 0, 18) {
                    crate::kprintln!("THOS: usb              port {}: device {:02x}{:02x}:{:02x}{:02x} class {}", p + 1, dd.get(9).copied().unwrap_or(0), dd.get(8).copied().unwrap_or(0), dd.get(11).copied().unwrap_or(0), dd.get(10).copied().unwrap_or(0), dd.get(4).copied().unwrap_or(0));
                }
                if e.try_keyboard(dev) && e.has_msc() {
                    break;
                }
            }
            Err(why) => crate::kprintln!("THOS: usb              port {}: could not enumerate the device: {}", p + 1, why),
        }
    }
    if e.has_keyboard() || e.has_msc() {
        Ok(e)
    } else {
        Err("EHCI: controller up, no USB keyboard or storage found")
    }
}
