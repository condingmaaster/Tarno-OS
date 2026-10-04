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

pub struct Ehci {
    op: u64,
    ports: u32,
    wide: bool,
    // one page: QH head (0x000), QH device (0x080), qTDs (0x100..), setup (0x400), data (0x440)
    dma_phys: u64,
    dma: *mut u8,
    kbd: Option<Kbd>,
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
            let _ = self.control(hub, setup_pkt(0x23, 3, 4, p, 0), false, 0); // PORT_RESET
            ms(80);
            let st = self.control(hub, setup_pkt(0xA3, 0, 0, p, 4), true, 4)?;
            if st[0] & 2 == 0 {
                continue; // not enabled
            }
            let speed = if st[1] & 4 != 0 { 2 } else if st[1] & 2 != 0 { 1 } else { 0 };
            let _ = self.control(hub, setup_pkt(0x23, 1, 0x10, p, 0), false, 0); // clear C_PORT_RESET
            if let Ok(dev) = self.enumerate(speed, hub.addr, p as u8, &mut next) {
                if self.try_keyboard(dev) {
                    return Ok(());
                }
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
        if let Some(iface) = hid_iface {
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
            return self.hub_ports(dev).is_ok() && self.kbd.is_some();
        }
        false
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

fn find_ehci() -> Option<pci::Location> {
    pci::find_class(0x0C, 0x03, 0x20)
}

/// Bring up every EHCI controller until one has a keyboard.
pub fn init() -> Result<Ehci, &'static str> {
    let loc = find_ehci().ok_or("no EHCI controller")?;
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

    let mut e = Ehci { op, ports, wide, dma_phys, dma, kbd: None };
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
        if r32(a) & 1 == 0 {
            continue; // nothing connected
        }
        let Some(speed) = e.reset_root_port(p) else { continue };
        if let Ok(dev) = e.enumerate(speed, 0, 0, &mut next) {
            if e.try_keyboard(dev) {
                break;
            }
        }
    }
    if e.has_keyboard() {
        Ok(e)
    } else {
        Err("EHCI: controller up, no USB keyboard found")
    }
}
