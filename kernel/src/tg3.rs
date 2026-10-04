// SPDX-License-Identifier: GPL-2.0-or-later
//! Broadcom NetLink BCM57780 gigabit Ethernet (PCI 14e4:1692; the "tg3" family) — the Acer Aspire 5742G's NIC.
//!
//! Written for THOS after the register map and bring-up order of Linux's `tg3.c` (GPL-2.0), restricted to what
//! this one chip needs: no interrupts (the net thread polls the status block), one transmit ring, one receive
//! return ring, a single standard receive producer ring, no offloads, no jumbo frames, no ASF/APE, no NVRAM
//! access (the MAC address comes from the bootcode's SRAM mailbox or the MAC registers).
//!
//! The chip is not emulated by QEMU, so this driver is only exercised on the real machine; every wait is
//! bounded and `probe` fails with a message instead of hanging when the device does not behave.

use alloc::collections::VecDeque;
use alloc::vec::Vec;
use core::sync::atomic::{fence, Ordering};

use crate::pci::{self, Location};

const VENDOR_BROADCOM: u16 = 0x14E4;
/// BCM57780 (0x1692), BCM57760 (0x1690), BCM57790 (0x1694), BCM57788 (0x1691): all ASIC rev 57780.
const IDS: &[u16] = &[0x1692, 0x1690, 0x1694, 0x1691];
const ASIC_REV_57780: u32 = 0x57780;

// PCI configuration space (mirrored in the first 0x100 bytes of the register window)
const PCI_MISC_HOST_CTRL: u8 = 0x68;
const PCI_PCISTATE: u8 = 0x70;
const PCI_PRODID_ASICREV: u8 = 0xBC;
/// MASK_PCI_INT | WORD_SWAP | INDIR_ACCESS | PCISTATE_RW (little-endian host: no byte swap)
const MISC_HOST_CTRL: u32 = 0x02 | 0x08 | 0x80 | 0x10;
const PCISTATE_ROM_ENABLE: u32 = 0x20;
const PCISTATE_ROM_RETRY_ENABLE: u32 = 0x40;

// registers (offsets into BAR0)
const MEM_WIN_BASE: usize = 0x7C;
const MEM_WIN_DATA: usize = 0x84;
const REG_BASE_ADDR: usize = 0x78;
const DMA_RW_CTRL: usize = 0x6C;
const TG3_CORR_ERR_STAT: usize = 0x110;
const MBX_INT0: usize = 0x200 + 4;
const MBX_RCV_STD_PROD: usize = 0x268 + 4;
const MBX_RCVRET_CON0: usize = 0x280 + 4;
const MBX_SNDHOST_PROD0: usize = 0x300 + 4;
const MAC_MODE: usize = 0x400;
const MAC_ADDR_0_HIGH: usize = 0x410;
const MAC_TX_BACKOFF_SEED: usize = 0x438;
const MAC_RX_MTU_SIZE: usize = 0x43C;
const MAC_MI_COM: usize = 0x44C;
const MAC_MI_STAT: usize = 0x450;
const MAC_MI_MODE: usize = 0x454;
const MAC_TX_MODE: usize = 0x45C;
const MAC_TX_LENGTHS: usize = 0x464;
const MAC_RX_MODE: usize = 0x468;
const MAC_HASH_REG_0: usize = 0x470;
const MAC_RCV_RULE_0: usize = 0x480;
const MAC_RCV_RULE_CFG: usize = 0x500;
const MAC_LOW_WMARK_MAX_RX_FRAME: usize = 0x504;
const MAC_LED_CTRL: usize = 0x40C;
const SNDDATAI_MODE: usize = 0xC00;
const SNDDATAI_STATSCTRL: usize = 0xC08;
const SNDDATAI_STATSENAB: usize = 0xC0C;
const SNDDATAC_MODE: usize = 0x1000;
const SNDBDS_MODE: usize = 0x1400;
const SNDBDI_MODE: usize = 0x1800;
const SNDBDC_MODE: usize = 0x1C00;
const RCVLPC_MODE: usize = 0x2000;
const RCVLPC_CONFIG: usize = 0x2010;
const RCVLPC_STATSCTRL: usize = 0x2014;
const RCVLPC_STATS_ENABLE: usize = 0x2018;
const RCVDBDI_MODE: usize = 0x2400;
const RCVDBDI_STD_BD: usize = 0x2450;
const RCVDCC_MODE: usize = 0x2800;
const RCVBDI_MODE: usize = 0x2C00;
const RCVBDI_STD_THRESH: usize = 0x2C18;
const RCVCC_MODE: usize = 0x3000;
const HOSTCC_MODE: usize = 0x3C00;
const HOSTCC_RXCOL_TICKS: usize = 0x3C08;
const HOSTCC_TXCOL_TICKS: usize = 0x3C0C;
const HOSTCC_RXMAX_FRAMES: usize = 0x3C10;
const HOSTCC_TXMAX_FRAMES: usize = 0x3C14;
const HOSTCC_RXCOAL_MAXF_INT: usize = 0x3C20;
const HOSTCC_TXCOAL_MAXF_INT: usize = 0x3C24;
const HOSTCC_STATUS_BLK_HOST_ADDR: usize = 0x3C38;
const MEMARB_MODE: usize = 0x4000;
const BUFMGR_MODE: usize = 0x4400;
const BUFMGR_MB_RDMA_LOW_WATER: usize = 0x4410;
const BUFMGR_MB_MACRX_LOW_WATER: usize = 0x4414;
const BUFMGR_MB_HIGH_WATER: usize = 0x4418;
const BUFMGR_DMA_LOW_WATER: usize = 0x4434;
const BUFMGR_DMA_HIGH_WATER: usize = 0x4438;
const RDMAC_MODE: usize = 0x4800;
const RDMA_RSRVCTRL: usize = 0x4900;
const WDMAC_MODE: usize = 0x4C00;
const GRC_MODE: usize = 0x6800;
const GRC_MISC_CFG: usize = 0x6804;
const GRC_LOCAL_CTRL: usize = 0x6808;
const NVRAM_SWARB: usize = 0x7020;
const PCIE_PWR_MGMT_THRESH: usize = 0x7D28;
const PCIE_LNKCTL: usize = 0x7D54;
const PCIE_EIDLE_DELAY: usize = 0x7E70;

// NIC-internal SRAM (reached through the memory window)
const SRAM_SEND_RCB: u32 = 0x100;
const SRAM_RCV_RET_RCB: u32 = 0x200;
const SRAM_FIRMWARE_MBOX: u32 = 0xB50;
const SRAM_FIRMWARE_MBOX_MAGIC1: u32 = 0x4B65_7654;
const SRAM_MAC_ADDR_HIGH_MBOX: u32 = 0xC14;
const SRAM_MAC_ADDR_LOW_MBOX: u32 = 0xC18;
const SRAM_RX_BUFFER_DESC: u32 = 0x6000;

// layout of the DMA region
const STD_RING_ENTRIES: usize = 512; // fixed by the chip for the standard ring
const RET_RING_ENTRIES: usize = 512;
const TX_RING_ENTRIES: usize = 512;
const RX_PENDING: usize = 48; // posted receive buffers
const TX_BUFS: usize = 16; // frames in flight (divides TX_RING_ENTRIES)
const BUF: usize = 2048;
const OFF_STD: usize = 0;
const OFF_RET: usize = OFF_STD + STD_RING_ENTRIES * 32;
const OFF_TX: usize = OFF_RET + RET_RING_ENTRIES * 32;
const OFF_STATUS: usize = OFF_TX + TX_RING_ENTRIES * 16;
const OFF_RXBUF: usize = OFF_STATUS + 0x1000;
const OFF_TXBUF: usize = OFF_RXBUF + RX_PENDING * BUF;
const DMA_NEED: usize = OFF_TXBUF + TX_BUFS * BUF;

pub const MTU: usize = 1514;
const RX_DMA_SZ: u32 = 1536;
const RXD_FLAG_END: u32 = 0x0004;
const RXD_OPAQUE_RING_STD: u32 = 0x0001_0000;
const RXD_OPAQUE_RING_MASK: u32 = 0x0007_0000;
const RXD_ERR_MASK: u32 = 0x0001_0000 | 0x0002_0000 | 0x0004_0000 | 0x0008_0000 | 0x0020_0000 | 0x0040_0000 | 0x0080_0000 | 0x0100_0000;
const TXD_FLAG_END: u32 = 0x0004;

const PHY_ADDR: u32 = 1;
/// MAC mode bits that stay on: TX/RX statistics, TX/RX/host-flow-through engines.
const MAC_MODE_BASE: u32 = 0x0000_0800 | 0x0000_4000 | 0x0020_0000 | 0x0040_0000 | 0x0080_0000;

fn udelay(us: u64) {
    let end = crate::timer::monotonic_ns() + us * 1000;
    while crate::timer::monotonic_ns() < end {
        core::hint::spin_loop();
    }
}

pub struct Tg3 {
    loc: Location,
    regs: *mut u8,
    pub mac: [u8; 6],
    dma: *mut u8,
    dma_phys: u64,
    buf_of_slot: [u8; STD_RING_ENTRIES],
    std_prod: u16,
    ret_cons: u16,
    tx_prod: u16,
    tx_cons: u16,
    link: bool,
    link_checked_ns: u64,
    pub rx_queue: VecDeque<Vec<u8>>,
}

unsafe impl Send for Tg3 {}

impl Tg3 {
    fn r(&self, off: usize) -> u32 {
        unsafe { (self.regs.add(off) as *const u32).read_volatile() }
    }
    fn w(&self, off: usize, v: u32) {
        unsafe { (self.regs.add(off) as *mut u32).write_volatile(v) }
    }
    /// Write, then read back to push the posted write out (the chip needs ordering for mode registers).
    fn wf(&self, off: usize, v: u32) {
        self.w(off, v);
        let _ = self.r(off);
    }
    fn mem_w(&self, off: u32, v: u32) {
        self.wf(MEM_WIN_BASE, off);
        self.wf(MEM_WIN_DATA, v);
        self.wf(MEM_WIN_BASE, 0);
    }
    fn mem_r(&self, off: u32) -> u32 {
        self.wf(MEM_WIN_BASE, off);
        let v = self.r(MEM_WIN_DATA);
        self.wf(MEM_WIN_BASE, 0);
        v
    }
    fn dma_w32(&self, off: usize, v: u32) {
        unsafe { (self.dma.add(off) as *mut u32).write_volatile(v) }
    }
    fn dma_r32(&self, off: usize) -> u32 {
        unsafe { (self.dma.add(off) as *const u32).read_volatile() }
    }
    fn dma_r16(&self, off: usize) -> u16 {
        unsafe { (self.dma.add(off) as *const u16).read_volatile() }
    }

    /// Poll `reg & mask` until it equals `want` (bounded to about `us` microseconds).
    fn wait(&self, reg: usize, mask: u32, want: u32, us: u64) -> bool {
        let end = crate::timer::monotonic_ns() + us * 1000;
        loop {
            if self.r(reg) & mask == want {
                return true;
            }
            if crate::timer::monotonic_ns() >= end {
                return false;
            }
            core::hint::spin_loop();
        }
    }

    // ---- MDIO ------------------------------------------------------------------------------------

    fn mdio_read(&self, reg: u32) -> Option<u16> {
        let frame = (PHY_ADDR << 21) | (reg << 16) | 0x0800_0000 | 0x2000_0000;
        self.wf(MAC_MI_COM, frame);
        for _ in 0..5000 {
            udelay(10);
            let v = self.r(MAC_MI_COM);
            if v & 0x2000_0000 == 0 {
                udelay(5);
                let v = self.r(MAC_MI_COM);
                return if v & 0x1000_0000 != 0 { None } else { Some((v & 0xFFFF) as u16) };
            }
        }
        None
    }

    fn mdio_write(&self, reg: u32, val: u16) -> bool {
        let frame = (PHY_ADDR << 21) | (reg << 16) | 0x0400_0000 | 0x2000_0000 | val as u32;
        self.wf(MAC_MI_COM, frame);
        for _ in 0..5000 {
            udelay(10);
            if self.r(MAC_MI_COM) & 0x2000_0000 == 0 {
                udelay(5);
                return true;
            }
        }
        false
    }

    // ---- bring-up --------------------------------------------------------------------------------

    pub fn probe() -> Result<Self, &'static str> {
        let loc = IDS.iter().find_map(|&id| pci::find_id(VENDOR_BROADCOM, id)).ok_or("no network device")?;
        let bar0 = pci::bar(loc, 0);
        if bar0 == 0 {
            return Err("tg3: BAR0 not assigned by the BIOS");
        }
        // memory + bus master on, INTx off (this driver polls)
        let cmd = pci::read16(loc, 0x04);
        pci::write32(loc, 0x04, (cmd | 0x6 | 0x400) as u32);

        let misc = pci::read32(loc, PCI_MISC_HOST_CTRL);
        let mut rev = misc >> 16;
        if rev >> 12 == 0x0F {
            rev = pci::read32(loc, PCI_PRODID_ASICREV); // CPMU parts keep the ASIC revision elsewhere
        }
        if rev >> 12 != ASIC_REV_57780 {
            return Err("tg3: not a BCM57780-class chip");
        }
        pci::write32(loc, PCI_MISC_HOST_CTRL, (misc & 0xFFFF_0000) | MISC_HOST_CTRL);

        let regs = crate::vmm::map_mmio(bar0, 0x10000) as *mut u8;
        let (dma_phys, dma_len) = crate::mm::net_dma();
        if (dma_len as usize) < DMA_NEED {
            return Err("tg3: DMA region too small");
        }
        let dma = crate::mm::phys_to_virt(x86_64::PhysAddr::new(dma_phys)).as_mut_ptr::<u8>();
        unsafe { core::ptr::write_bytes(dma, 0, DMA_NEED) };

        let mut t = Self {
            loc,
            regs,
            mac: [0; 6],
            dma,
            dma_phys,
            buf_of_slot: [0; STD_RING_ENTRIES],
            std_prod: 0,
            ret_cons: 0,
            tx_prod: 0,
            tx_cons: 0,
            link: false,
            link_checked_ns: 0,
            rx_queue: VecDeque::new(),
        };
        if t.r(GRC_MISC_CFG) == 0xFFFF_FFFF {
            return Err("tg3: registers read as all-ones (device not responding)");
        }
        // the memory arbiter must run for SRAM access; byte/word swapping for descriptors
        t.w(MEMARB_MODE, t.r(MEMARB_MODE) | 2);
        let grc = (t.r(GRC_MODE) & 0x0001_0000) | Self::GRC_BASE;
        t.w(GRC_MODE, grc);
        t.w(MEM_WIN_BASE, 0);
        t.w(REG_BASE_ADDR, 0);

        t.chip_reset(misc)?;
        t.mac = t.read_mac();
        t.init_hw()?;
        Ok(t)
    }

    /// WSWAP_DATA | BSWAP_DATA | WSWAP_NONFRM_DATA: descriptor and packet-data swapping for a little-endian host.
    const GRC_BASE: u32 = 0x20 | 0x10 | 0x04;

    fn nvram_lock(&self) {
        self.w(NVRAM_SWARB, 0x2); // SWARB_REQ_SET1
        for _ in 0..2000 {
            if self.r(NVRAM_SWARB) & 0x200 != 0 {
                return; // SWARB_GNT1
            }
            udelay(20);
        }
        // not granted: reset anyway (the reset drops any arbitration state)
    }

    fn chip_reset(&mut self, misc: u32) -> Result<(), &'static str> {
        let loc = self.loc;
        // pre-reset signature for the bootcode
        self.mem_w(SRAM_FIRMWARE_MBOX, SRAM_FIRMWARE_MBOX_MAGIC1);
        self.nvram_lock();
        let saved_cmd = pci::read16(loc, 0x04);

        // BCM57780: keep the PCIe PLL powered in L1
        let v = self.r(PCIE_LNKCTL) & !0x0000_0008;
        self.w(PCIE_LNKCTL, v | 0x0000_0080);

        // GRC core-clock reset (+ bit 29 on PCIe parts)
        self.w(GRC_MISC_CFG, 1 << 29);
        self.w(GRC_MISC_CFG, 1 | (1 << 29));
        udelay(120);
        let _ = pci::read32(loc, 0x04);
        udelay(120);

        // PCIe: no relaxed ordering / no-snoop, clear latched errors
        if let Some(cap) = pci::find_cap(loc, 0x10) {
            let ctl = pci::read16(loc, cap + 8);
            pci::write16(loc, cap + 8, ctl & !(0x0010 | 0x0800));
            pci::write16(loc, cap + 10, 0x000F);
        }
        // the reset cleared the PCI-side state: put it back
        pci::write32(loc, PCI_MISC_HOST_CTRL, (misc & 0xFFFF_0000) | MISC_HOST_CTRL);
        pci::write32(loc, PCI_PCISTATE, PCISTATE_ROM_ENABLE | PCISTATE_ROM_RETRY_ENABLE);
        pci::write16(loc, 0x04, saved_cmd | 0x400);

        self.w(MEMARB_MODE, 2);
        // wait for the bootcode to finish (it writes the complement of the magic); some boards have none
        let end = crate::timer::monotonic_ns() + 1_000_000_000;
        while self.mem_r(SRAM_FIRMWARE_MBOX) != !SRAM_FIRMWARE_MBOX_MAGIC1 {
            if crate::timer::monotonic_ns() >= end {
                break;
            }
            udelay(100);
        }
        if self.r(GRC_MISC_CFG) == 0xFFFF_FFFF {
            return Err("tg3: device vanished during reset");
        }
        self.w(GRC_MODE, Self::GRC_BASE);
        self.wf(MAC_MODE, 0);
        udelay(40);
        self.wf(MAC_MI_MODE, 0x8000); // MDIO clock: 500 kHz constant, auto-poll off
        udelay(80);
        self.w(0x7C00, self.r(0x7C00) | (1 << 25)); // PCIe: errata bit from the reference driver
        Ok(())
    }

    fn read_mac(&self) -> [u8; 6] {
        let hi = self.mem_r(SRAM_MAC_ADDR_HIGH_MBOX);
        if hi >> 16 == 0x484B {
            let lo = self.mem_r(SRAM_MAC_ADDR_LOW_MBOX);
            let m = [(hi >> 8) as u8, hi as u8, (lo >> 24) as u8, (lo >> 16) as u8, (lo >> 8) as u8, lo as u8];
            if m != [0; 6] && m[0] & 1 == 0 {
                return m;
            }
        }
        let (hi, lo) = (self.r(MAC_ADDR_0_HIGH), self.r(MAC_ADDR_0_HIGH + 4));
        [(hi >> 8) as u8, hi as u8, (lo >> 24) as u8, (lo >> 16) as u8, (lo >> 8) as u8, lo as u8]
    }

    fn init_hw(&mut self) -> Result<(), &'static str> {
        // 57780-specific PCIe tuning
        let v = self.r(PCIE_PWR_MGMT_THRESH) & !0x0000_FF00;
        self.w(PCIE_PWR_MGMT_THRESH, v | 0x0100_0000 | 0x0000_FF00);
        let v = self.r(PCIE_EIDLE_DELAY) & !0x1F;
        self.w(PCIE_EIDLE_DELAY, v | 0x0C);
        self.w(TG3_CORR_ERR_STAT, 0xFFFF_FFFF);
        let v = self.r(PCIE_LNKCTL) & !0x0000_0008;
        self.w(PCIE_LNKCTL, v | 0x0000_0080);

        // DMA read/write control: PCIe watermark bits
        self.w(DMA_RW_CTRL, (0x7 << 28) | (0x6 << 24) | 0x0018_0000);

        // GRC: host send BDs, no TX pseudo-header checksum, attention on MAC events, host stack-up
        self.w(GRC_MODE, Self::GRC_BASE | 0x0002_0000 | 0x0010_0000 | 0x0400_0000 | 0x0001_0000);
        // timer prescaler: the core clock is 66 MHz
        let v = self.r(GRC_MISC_CFG) & !0xFF;
        self.w(GRC_MISC_CFG, v | (65 << 1));

        // buffer manager
        self.w(BUFMGR_MB_RDMA_LOW_WATER, 0x0);
        self.w(BUFMGR_MB_MACRX_LOW_WATER, 0x10);
        self.w(BUFMGR_MB_HIGH_WATER, 0x60);
        self.w(BUFMGR_DMA_LOW_WATER, 5);
        self.w(BUFMGR_DMA_HIGH_WATER, 10);
        self.w(BUFMGR_MODE, 0x2 | 0x4);
        if !self.wait(BUFMGR_MODE, 0x2, 0x2, 20_000) {
            return Err("tg3: buffer manager did not start");
        }

        // standard receive ring: replenish threshold, ring control block, initial buffers
        let thresh = (RX_PENDING as u32 / 8).max(1).min(32 / 2);
        self.w(RCVBDI_STD_THRESH, thresh);
        let std_phys = self.dma_phys + OFF_STD as u64;
        for i in 0..STD_RING_ENTRIES {
            let b = OFF_STD + i * 32;
            self.dma_w32(b + 8, RX_DMA_SZ); // idx_len: buffer length
            self.dma_w32(b + 12, RXD_FLAG_END);
            self.dma_w32(b + 28, RXD_OPAQUE_RING_STD | i as u32);
        }
        for i in 0..RX_PENDING {
            self.buf_of_slot[i] = i as u8;
            self.set_std_addr(i, i);
        }
        self.w(RCVDBDI_STD_BD, (std_phys >> 32) as u32);
        self.w(RCVDBDI_STD_BD + 4, std_phys as u32);
        self.w(RCVDBDI_STD_BD + 8, 512 << 16); // max length / flags (no jumbo ring on this chip)
        self.w(RCVDBDI_STD_BD + 12, SRAM_RX_BUFFER_DESC);
        self.std_prod = RX_PENDING as u16;
        self.w(MBX_RCV_STD_PROD, RX_PENDING as u32);

        // rings: mailboxes to zero, status block, send and return ring control blocks
        self.wf(MBX_INT0, 1); // interrupts off while the rings are set up
        self.w(MBX_SNDHOST_PROD0, 0);
        self.w(MBX_RCVRET_CON0, 0);
        let status_phys = self.dma_phys + OFF_STATUS as u64;
        self.w(HOSTCC_STATUS_BLK_HOST_ADDR, (status_phys >> 32) as u32);
        self.w(HOSTCC_STATUS_BLK_HOST_ADDR + 4, status_phys as u32);
        let tx_phys = self.dma_phys + OFF_TX as u64;
        self.mem_w(SRAM_SEND_RCB, (tx_phys >> 32) as u32);
        self.mem_w(SRAM_SEND_RCB + 4, tx_phys as u32);
        self.mem_w(SRAM_SEND_RCB + 8, (TX_RING_ENTRIES as u32) << 16);
        let ret_phys = self.dma_phys + OFF_RET as u64;
        self.mem_w(SRAM_RCV_RET_RCB, (ret_phys >> 32) as u32);
        self.mem_w(SRAM_RCV_RET_RCB + 4, ret_phys as u32);
        self.mem_w(SRAM_RCV_RET_RCB + 8, (RET_RING_ENTRIES as u32) << 16);

        // MAC address (all four filters), backoff seed, MTU, inter-packet gap
        let m = self.mac;
        let (hi, lo) = (((m[0] as u32) << 8) | m[1] as u32, u32::from_be_bytes([m[2], m[3], m[4], m[5]]));
        for i in 0..4 {
            self.w(MAC_ADDR_0_HIGH + i * 8, hi);
            self.w(MAC_ADDR_0_HIGH + i * 8 + 4, lo);
        }
        self.w(MAC_TX_BACKOFF_SEED, m.iter().map(|&b| b as u32).sum::<u32>() & 0x3FF);
        self.w(MAC_RX_MTU_SIZE, 1500 + 14 + 4 + 4);
        self.w(MAC_TX_LENGTHS, (2 << 12) | (6 << 8) | 32);

        // receive rules and the packet-processing engines
        self.w(MAC_RCV_RULE_CFG, 0x8);
        self.w(RCVLPC_CONFIG, 0x0181);
        let rdmac = 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 // enable + abort/error reporting
            | 0x0800 | 0x1000 | 0x2000 // BD / mbuf corruption checks (57780)
            | 0x0003_0000 // PCIe: long FIFO burst
            | 0x0800_0000 | 0x1000_0000; // IPv4/IPv6 LSO engine enable (as the reference driver)
        let v = self.r(RDMA_RSRVCTRL);
        self.w(RDMA_RSRVCTRL, v | 0x4); // FIFO overflow fix
        let v = self.r(RCVLPC_STATS_ENABLE) & !0x0004_0000;
        self.w(RCVLPC_STATS_ENABLE, v);
        self.w(RCVLPC_STATSCTRL, 1);
        self.w(SNDDATAI_STATSENAB, 0xFF_FFFF);
        self.w(SNDDATAI_STATSCTRL, 1 | 2);

        // host coalescing: update the status block (almost) per frame; we poll it
        self.w(HOSTCC_MODE, 0);
        if !self.wait(HOSTCC_MODE, 0x2, 0, 20_000) {
            return Err("tg3: host coalescing did not reset");
        }
        self.w(HOSTCC_RXCOL_TICKS, 2);
        self.w(HOSTCC_TXCOL_TICKS, 2);
        self.w(HOSTCC_RXMAX_FRAMES, 1);
        self.w(HOSTCC_TXMAX_FRAMES, 1);
        self.w(HOSTCC_RXCOAL_MAXF_INT, 1);
        self.w(HOSTCC_TXCOAL_MAXF_INT, 1);
        self.w(HOSTCC_MODE, 0x2 | 0x100); // ENABLE | 32-byte status block
        self.w(RCVCC_MODE, 0x2 | 0x4);
        self.w(RCVLPC_MODE, 0x2);

        self.wf(MAC_MODE, MAC_MODE_BASE | 0x1000 | 0x8000); // + RX/TX stat clear
        udelay(40);
        // GPIO state: keep what the bootcode set up for the LOM
        let mask = 0x0800 | 0x1000 | 0x2000 | 0x4000 | 0x8000 | 0x0001_0000;
        let cur = self.r(GRC_LOCAL_CTRL);
        self.wf(GRC_LOCAL_CTRL, 0x0000_0008 | 0x0100_0000 | 0x0000_0010 | (cur & mask));
        udelay(100);

        self.wf(WDMAC_MODE, 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x2000_0000);
        udelay(40);
        self.wf(RDMAC_MODE, rdmac);
        udelay(40);
        self.w(RCVDCC_MODE, 0x2 | 0x4);
        self.w(SNDDATAC_MODE, 0x2);
        self.w(SNDBDC_MODE, 0x2 | 0x4);
        self.w(RCVBDI_MODE, 0x2 | 0x4);
        self.w(RCVDBDI_MODE, 0x2 | 0x10);
        self.w(SNDDATAI_MODE, 0x2 | 0x8);
        self.w(SNDBDI_MODE, 0x2 | 0x4);
        self.w(SNDBDS_MODE, 0x2 | 0x4);
        self.wf(MAC_TX_MODE, 0x2 | 0x100); // enable + mbuf lock-up fix
        udelay(100);
        self.wf(MAC_RX_MODE, 0x2 | 0x0100_0000); // enable + IPv6 checksum
        udelay(10);
        self.w(MAC_LED_CTRL, 0x0800); // LED mode: PHY 1
        self.w(MAC_MI_STAT, 1);
        self.wf(MAC_LOW_WMARK_MAX_RX_FRAME, 2);

        // multicast hash filter closed; broadcast and our unicast address pass
        for i in 0..4 {
            self.w(MAC_HASH_REG_0 + i * 4, 0);
        }
        // receive rules: the reference driver programs rule 0/1 disabled and clears the rest
        self.w(MAC_RCV_RULE_0, 0xC200_0000 & 0x7FFF_FFFF);
        self.w(MAC_RCV_RULE_0 + 4, 0xFFFF_FFFF & 0x7FFF_FFFF);
        self.w(MAC_RCV_RULE_0 + 8, 0x8600_0004 & 0x7FFF_FFFF);
        self.w(MAC_RCV_RULE_0 + 12, 0xFFFF_FFFF & 0x7FFF_FFFF);
        for i in 4..8 {
            self.w(MAC_RCV_RULE_0 + i * 8, 0);
            self.w(MAC_RCV_RULE_0 + i * 8 + 4, 0);
        }

        // PHY: restart auto-negotiation (10/100/1000, full and half duplex)
        self.mdio_write(0, 0x8000); // BMCR reset
        for _ in 0..500 {
            match self.mdio_read(0) {
                Some(v) if v & 0x8000 == 0 => break,
                _ => udelay(100),
            }
        }
        self.mdio_write(4, 0x01E1); // advertise 10/100 full+half
        self.mdio_write(9, 0x0300); // advertise 1000 full+half
        self.mdio_write(0, 0x1200); // auto-negotiation enable + restart

        self.w(MBX_INT0, 0); // interrupts (masked at PCI level) on
        Ok(())
    }

    fn set_std_addr(&self, slot: usize, buf: usize) {
        let phys = self.dma_phys + (OFF_RXBUF + buf * BUF) as u64;
        let b = OFF_STD + slot * 32;
        self.dma_w32(b, (phys >> 32) as u32);
        self.dma_w32(b + 4, phys as u32);
    }

    // ---- run time --------------------------------------------------------------------------------

    /// Ask the host-coalescing engine to write the status block now.
    fn refresh_status(&self) {
        self.w(HOSTCC_MODE, 0x2 | 0x100 | 0x8);
    }

    fn check_link(&mut self) {
        let now = crate::timer::monotonic_ns();
        if now.saturating_sub(self.link_checked_ns) < 250_000_000 {
            return;
        }
        self.link_checked_ns = now;
        let _ = self.mdio_read(1); // BMSR link status is latched low: read twice
        let Some(bmsr) = self.mdio_read(1) else { return };
        let up = bmsr & 0x0004 != 0 && bmsr & 0x0020 != 0;
        if up == self.link {
            return;
        }
        self.link = up;
        if !up {
            crate::kprintln!("THOS: tg3 link        down");
            return;
        }
        let adv = self.mdio_read(4).unwrap_or(0);
        let lpa = self.mdio_read(5).unwrap_or(0);
        let ctl1000 = self.mdio_read(9).unwrap_or(0);
        let stat1000 = self.mdio_read(10).unwrap_or(0);
        let c1000 = ctl1000 & (stat1000 >> 2) & 0x0300;
        let common = adv & lpa;
        let (speed, half) = if c1000 & 0x0200 != 0 {
            (1000, false)
        } else if c1000 & 0x0100 != 0 {
            (1000, true)
        } else if common & 0x0100 != 0 {
            (100, false)
        } else if common & 0x0080 != 0 {
            (100, true)
        } else if common & 0x0040 != 0 {
            (10, false)
        } else {
            (10, true)
        };
        let mut mode = MAC_MODE_BASE | if speed == 1000 { 0x8 } else { 0x4 };
        if half {
            mode |= 0x2;
        }
        self.wf(MAC_MODE, mode);
        udelay(40);
        let slot = if speed == 1000 && half { 0xFF } else { 32 };
        self.w(MAC_TX_LENGTHS, (2 << 12) | (6 << 8) | slot);
        crate::kprintln!("THOS: tg3 link        up {} Mb/s {} duplex", speed, if half { "half" } else { "full" });
    }

    /// Move received frames into `rx_queue` and hand their buffers back to the chip.
    pub fn poll_rx(&mut self) {
        self.check_link();
        self.refresh_status();
        let hw_ret = self.dma_r16(OFF_STATUS + 16) % RET_RING_ENTRIES as u16;
        let hw_tx = self.dma_r16(OFF_STATUS + 18) % TX_RING_ENTRIES as u16;
        self.dma_w32(OFF_STATUS, 0);
        self.tx_cons = hw_tx;
        fence(Ordering::Acquire);
        let mut posted = false;
        while self.ret_cons != hw_ret {
            let d = OFF_RET + self.ret_cons as usize * 32;
            let idx_len = self.dma_r32(d + 8);
            let err_vlan = self.dma_r32(d + 20);
            let opaque = self.dma_r32(d + 28);
            if opaque & RXD_OPAQUE_RING_MASK == RXD_OPAQUE_RING_STD {
                let slot = (opaque & 0xFFFF) as usize % STD_RING_ENTRIES;
                let buf = self.buf_of_slot[slot] as usize;
                let len = ((idx_len & 0xFFFF) as usize).saturating_sub(4); // minus FCS
                if err_vlan & RXD_ERR_MASK == 0 && len >= 14 && len <= RX_DMA_SZ as usize {
                    let src = unsafe { self.dma.add(OFF_RXBUF + buf * BUF) };
                    self.rx_queue.push_back(unsafe { core::slice::from_raw_parts(src, len) }.to_vec());
                }
                // give the same buffer back through the next producer slot
                let dest = self.std_prod as usize % STD_RING_ENTRIES;
                self.buf_of_slot[dest] = buf as u8;
                self.set_std_addr(dest, buf);
                self.std_prod = (self.std_prod + 1) % STD_RING_ENTRIES as u16;
                posted = true;
            }
            self.ret_cons = (self.ret_cons + 1) % RET_RING_ENTRIES as u16;
        }
        fence(Ordering::Release);
        if posted {
            self.w(MBX_RCV_STD_PROD, self.std_prod as u32);
        }
        self.w(MBX_RCVRET_CON0, self.ret_cons as u32);
    }

    fn tx_room(&self) -> bool {
        (self.tx_prod.wrapping_sub(self.tx_cons) as usize % TX_RING_ENTRIES) < TX_BUFS - 1
    }

    pub fn tx_ready(&mut self) -> bool {
        if self.tx_room() {
            return true;
        }
        self.refresh_status();
        self.tx_cons = self.dma_r16(OFF_STATUS + 18) % TX_RING_ENTRIES as u16;
        self.tx_room()
    }

    pub fn transmit<F: FnOnce(&mut [u8])>(&mut self, len: usize, fill: F) -> bool {
        if len > MTU || !self.tx_ready() {
            return false;
        }
        let entry = self.tx_prod as usize;
        let slot = entry % TX_BUFS;
        let l = len.max(60);
        let buf = unsafe { core::slice::from_raw_parts_mut(self.dma.add(OFF_TXBUF + slot * BUF), l) };
        buf.fill(0);
        fill(&mut buf[..len]);
        let phys = self.dma_phys + (OFF_TXBUF + slot * BUF) as u64;
        let d = OFF_TX + entry * 16;
        self.dma_w32(d, (phys >> 32) as u32);
        self.dma_w32(d + 4, phys as u32);
        self.dma_w32(d + 8, ((l as u32) << 16) | TXD_FLAG_END);
        self.dma_w32(d + 12, 0);
        fence(Ordering::Release);
        self.tx_prod = ((entry + 1) % TX_RING_ENTRIES) as u16;
        self.w(MBX_SNDHOST_PROD0, self.tx_prod as u32);
        true
    }
}
