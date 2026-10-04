// SPDX-License-Identifier: GPL-2.0-or-later
//! Intel 8254x "e1000" gigabit NIC (QEMU's `-device e1000`, and — because the descriptor rings
//! and the register layout are shared — the e1000e family in many 2008-2013 laptops and desktops).
//! Polled, 16 receive and 16 transmit descriptors with 2 KiB buffers: small on purpose.

use alloc::collections::VecDeque;
use alloc::vec::Vec;
use core::sync::atomic::{fence, Ordering};

use crate::pci;

const VENDOR_INTEL: u16 = 0x8086;
/// 82540EM (QEMU), 82545EM, 82541, 82543GC, 82574L, 82567LM/LF (ICH9/10), 82577, 82578, 82579, I217/I218.
const IDS: &[u16] = &[
    0x100E, 0x100F, 0x1010, 0x1019, 0x101E, 0x1026, 0x1076, 0x10D3, 0x10F5, 0x1502, 0x1503, 0x153A, 0x153B, 0x1559,
    0x10EA, 0x10BD, 0x294C,
];

const CTRL: usize = 0x0000;
const STATUS: usize = 0x0008;
const IMC: usize = 0x00D8;
const RCTL: usize = 0x0100;
const TCTL: usize = 0x0400;
const TIPG: usize = 0x0410;
const RDBAL: usize = 0x2800;
const RDBAH: usize = 0x2804;
const RDLEN: usize = 0x2808;
const RDH: usize = 0x2810;
const RDT: usize = 0x2818;
const TDBAL: usize = 0x3800;
const TDBAH: usize = 0x3804;
const TDLEN: usize = 0x3808;
const TDH: usize = 0x3810;
const TDT: usize = 0x3818;
const MTA: usize = 0x5200;
const RAL0: usize = 0x5400;
const RAH0: usize = 0x5404;

const CTRL_RST: u32 = 1 << 26;
const CTRL_SLU: u32 = 1 << 6;
const CTRL_ASDE: u32 = 1 << 5;
const RCTL_EN: u32 = 1 << 1;
const RCTL_BAM: u32 = 1 << 15;
const RCTL_SECRC: u32 = 1 << 26;
const TCTL_EN: u32 = 1 << 1;
const TCTL_PSP: u32 = 1 << 3;

const N: usize = 16;
const BUF: usize = 2048;
pub const MTU: usize = 1514;

pub struct E1000 {
    mmio: *mut u8,
    pub mac: [u8; 6],
    rx_desc: *mut u8,
    tx_desc: *mut u8,
    rx_bufs: *mut u8,
    tx_bufs: *mut u8,
    rx_next: usize,
    tx_next: usize,
    pub rx_queue: VecDeque<Vec<u8>>,
}

unsafe impl Send for E1000 {}

impl E1000 {
    fn r(&self, off: usize) -> u32 {
        unsafe { (self.mmio.add(off) as *const u32).read_volatile() }
    }
    fn w(&self, off: usize, v: u32) {
        unsafe { (self.mmio.add(off) as *mut u32).write_volatile(v) }
    }

    pub fn probe() -> Result<Self, &'static str> {
        let loc = IDS.iter().find_map(|&id| pci::find_id(VENDOR_INTEL, id)).ok_or("no e1000 device")?;
        let bar0 = pci::bar(loc, 0);
        if bar0 == 0 {
            return Err("e1000 BAR0 missing");
        }
        pci::enable_bus_master(loc);
        let mmio = crate::vmm::map_mmio(bar0, 0x20000) as *mut u8;

        let (dma_phys, dma_len) = crate::mm::net_dma();
        let need = 2 * N * 16 + 2 * N * BUF;
        if (dma_len as usize) < need {
            return Err("e1000: DMA region too small");
        }
        let dma_virt = crate::mm::phys_to_virt(x86_64::PhysAddr::new(dma_phys)).as_mut_ptr::<u8>();
        unsafe { core::ptr::write_bytes(dma_virt, 0, need) };

        let mut dev = Self {
            mmio,
            mac: [0; 6],
            rx_desc: dma_virt,
            tx_desc: unsafe { dma_virt.add(N * 16) },
            rx_bufs: unsafe { dma_virt.add(2 * N * 16) },
            tx_bufs: unsafe { dma_virt.add(2 * N * 16 + N * BUF) },
            rx_next: 0,
            tx_next: 0,
            rx_queue: VecDeque::new(),
        };
        let rx_desc_phys = dma_phys;
        let tx_desc_phys = dma_phys + (N * 16) as u64;
        let rx_buf_phys = dma_phys + (2 * N * 16) as u64;

        dev.w(IMC, 0xFFFF_FFFF); // no interrupts: the net thread polls
        dev.w(CTRL, dev.r(CTRL) | CTRL_RST);
        for _ in 0..1_000_000 {
            if dev.r(CTRL) & CTRL_RST == 0 {
                break;
            }
            core::hint::spin_loop();
        }
        dev.w(IMC, 0xFFFF_FFFF);
        dev.w(CTRL, (dev.r(CTRL) | CTRL_SLU | CTRL_ASDE) & !(1 << 3) & !(1 << 31)); // link up, auto speed, no LRST/PHY_RST

        // MAC address (the EEPROM contents are mirrored into receive-address register 0)
        let (lo, hi) = (dev.r(RAL0), dev.r(RAH0));
        dev.mac = [lo as u8, (lo >> 8) as u8, (lo >> 16) as u8, (lo >> 24) as u8, hi as u8, (hi >> 8) as u8];
        dev.w(RAH0, hi | (1 << 31)); // address valid
        for i in 0..128 {
            dev.w(MTA + 4 * i, 0);
        }

        // receive ring
        for i in 0..N {
            let d = unsafe { dev.rx_desc.add(i * 16) } as *mut u64;
            unsafe { d.write_volatile(rx_buf_phys + (i * BUF) as u64) };
        }
        dev.w(RDBAL, rx_desc_phys as u32);
        dev.w(RDBAH, (rx_desc_phys >> 32) as u32);
        dev.w(RDLEN, (N * 16) as u32);
        dev.w(RDH, 0);
        dev.w(RDT, (N - 1) as u32);
        dev.w(RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC); // 2048-byte buffers, broadcast accepted

        // transmit ring
        dev.w(TDBAL, tx_desc_phys as u32);
        dev.w(TDBAH, (tx_desc_phys >> 32) as u32);
        dev.w(TDLEN, (N * 16) as u32);
        dev.w(TDH, 0);
        dev.w(TDT, 0);
        dev.w(TIPG, 0x0060_200A);
        dev.w(TCTL, TCTL_EN | TCTL_PSP | (0x0F << 4) | (0x40 << 12));
        let _ = dev.r(STATUS);
        Ok(dev)
    }

    fn rx_status(&self, i: usize) -> u8 {
        unsafe { self.rx_desc.add(i * 16 + 12).read_volatile() }
    }

    /// Move received frames into `rx_queue` and hand the descriptors back to the card.
    pub fn poll_rx(&mut self) {
        let mut any = false;
        while self.rx_status(self.rx_next) & 1 != 0 {
            let i = self.rx_next;
            let len = unsafe { (self.rx_desc.add(i * 16 + 8) as *const u16).read_volatile() } as usize;
            let errors = unsafe { self.rx_desc.add(i * 16 + 13).read_volatile() };
            if errors == 0 && len > 0 && len <= BUF {
                fence(Ordering::Acquire);
                let src = unsafe { self.rx_bufs.add(i * BUF) };
                self.rx_queue.push_back(unsafe { core::slice::from_raw_parts(src, len) }.to_vec());
            }
            unsafe { self.rx_desc.add(i * 16 + 12).write_volatile(0) };
            self.w(RDT, i as u32); // this descriptor may be filled again
            self.rx_next = (i + 1) % N;
            any = true;
        }
        let _ = any;
    }

    fn tx_free(&self) -> bool {
        // a descriptor is free if it was never used or the card finished it (DD set)
        let st = unsafe { self.tx_desc.add(self.tx_next * 16 + 12).read_volatile() };
        let len = unsafe { (self.tx_desc.add(self.tx_next * 16 + 8) as *const u16).read_volatile() };
        len == 0 || st & 1 != 0
    }

    pub fn tx_ready(&mut self) -> bool {
        self.tx_free()
    }

    pub fn transmit<F: FnOnce(&mut [u8])>(&mut self, len: usize, fill: F) -> bool {
        if len > MTU || !self.tx_free() {
            return false;
        }
        let i = self.tx_next;
        let buf = unsafe { core::slice::from_raw_parts_mut(self.tx_bufs.add(i * BUF), len.max(60)) };
        buf.fill(0);
        fill(&mut buf[..len]);
        let (_, tx_buf_phys) = {
            let (p, _) = crate::mm::net_dma();
            (0, p + (2 * N * 16 + N * BUF) as u64)
        };
        let d = unsafe { self.tx_desc.add(i * 16) };
        unsafe {
            (d as *mut u64).write_volatile(tx_buf_phys + (i * BUF) as u64);
            (d.add(8) as *mut u16).write_volatile(len.max(60) as u16);
            d.add(10).write_volatile(0); // cso
            d.add(11).write_volatile(0x0B); // EOP | IFCS | RS
            d.add(12).write_volatile(0); // clear DD
        }
        fence(Ordering::Release);
        self.tx_next = (i + 1) % N;
        self.w(TDT, self.tx_next as u32);
        true
    }
}
