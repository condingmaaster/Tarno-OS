// SPDX-License-Identifier: GPL-2.0-or-later
//! BCM43225/BCMA diagnostic bring-up, not yet an operational wireless NIC.
//! Linux BCMA register definitions: scan.h, bcma_regs.h, bcma_driver_chipcommon.h.
use alloc::{format, string::String};
use spin::Mutex;
use crate::{bcma_core, bcma_erom, pci};
#[derive(Clone, Copy)]
struct Status { present: bool, phase: &'static str, detail: &'static str, revision: u8, cores: usize, firmware: &'static str, microcode_bytes: usize, d11_revision: u8, core: Option<bcma_core::Snapshot> }
static STATUS: Mutex<Status> = Mutex::new(Status { present: false, phase: "absent", detail: "No Wi-Fi adapter detected", revision: 0, cores: 0, firmware: "unchecked", microcode_bytes: 0, d11_revision: 0, core: None });
#[derive(Clone, Copy)]
struct Hw { loc: pci::Location, mmio: usize, d11: u32, wrapper: u32 }
static HW: Mutex<Option<Hw>> = Mutex::new(None);
pub fn present() -> bool { STATUS.lock().present }
pub fn info() -> String {
    let s = *STATUS.lock();
    let mut out = format!("present {}\nadapter {}\nstate {}\ndetail {}\nconnected 0\nchip_revision {}\ncores {}\nfirmware {}\nfirmware_loaded 0\nmicrocode_bytes {}\ncore_state {}\n", s.present as u8, if s.present { "Broadcom BCM43225" } else { "none" }, s.phase, s.detail, s.revision, s.cores, s.firmware, s.microcode_bytes, s.core.map_or(if s.present { "unknown" } else { "absent" }, |c| c.state()));
    if let Some(c) = s.core { out.push_str(&format!("d11_revision {}\ncore_ioctl {:#x}\ncore_iost {:#x}\ncore_reset {:#x}\ncore_busy {:#x}\ndma64 {}\n", s.d11_revision, c.ioctl, c.iost, c.reset, c.busy, c.dma64() as u8)); }
    out
}
pub fn init() {
    let fw = (|| {
        let bin = crate::firmware::read("brcm/bcm43xx-0.fw", crate::brcm_firmware::MAX_BIN)?;
        let index = crate::firmware::read("brcm/bcm43xx_hdr-0.fw", crate::brcm_firmware::MAX_INDEX)?;
        crate::brcm_firmware::Firmware::parse(&bin, &index)?.mimo16_bytes()
    })();
    let (firmware, microcode_bytes) = match fw {
        Ok(bytes) => ("validated", bytes),
        Err(e) => { crate::kprintln!("THOS: wlan firmware    {}", e); (if e == "firmware file missing" { "missing" } else { "invalid" }, 0) }
    };
    { let mut s = STATUS.lock(); s.firmware = firmware; s.microcode_bytes = microcode_bytes; }
    crate::kprintln!("THOS: wlan firmware    {} ({} MIMO16 bytes), not uploaded", firmware, microcode_bytes);
    let Some(loc) = pci::find_id(0x14e4, 0x4357) else { crate::kprintln!("THOS: wlan             BCM43225 not present"); return; };
    let mut s = Status { present: true, phase: "probe-failed", detail: "Wi-Fi hardware could not be inspected", revision: 0, cores: 0, firmware, microcode_bytes, d11_revision: 0, core: None };
    let result = probe(loc);
    match result {
        Ok((revision, cores, d11_revision, core)) => { s.revision = revision; s.cores = cores; s.d11_revision = d11_revision; s.core = Some(core); s.phase = "bcma-ready"; s.detail = "Detected; Wi-Fi connection support pending"; }
        Err(e) => s.detail = e,
    }
    crate::kprintln!("THOS: wlan             14e4:4357 {:02x}:{:02x}.{} {}: {} ({} cores)", loc.bus, loc.dev, loc.func, s.phase, s.detail, s.cores);
    *STATUS.lock() = s;
}
struct Wrapper { mmio: usize }
impl bcma_core::Registers for Wrapper {
    fn read(&mut self, offset: u16) -> Result<u32, &'static str> {
        if offset >= 4096 || offset & 3 != 0 { return Err("invalid BCMA wrapper offset"); }
        Ok(unsafe { core::ptr::read_volatile((self.mmio + 4096 + offset as usize) as *const u32) })
    }
    fn write(&mut self, offset: u16, value: u32) -> Result<(), &'static str> {
        if offset >= 4096 || offset & 3 != 0 { return Err("invalid BCMA wrapper offset"); }
        unsafe { core::ptr::write_volatile((self.mmio + 4096 + offset as usize) as *mut u32, value); }
        Ok(())
    }
    fn delay_us(&mut self, us: u32) {
        let start = unsafe { core::arch::x86_64::_rdtsc() };
        let ticks = us as u64 * crate::timer::tsc_mhz().max(1);
        while unsafe { core::arch::x86_64::_rdtsc() }.wrapping_sub(start) < ticks { core::hint::spin_loop(); }
    }
}
fn probe(loc: pci::Location) -> Result<(u8, usize, u8, bcma_core::Snapshot), &'static str> {
    if let Some(pm) = pci::find_cap(loc, 1) {
        if pm > 0xf8 || pci::read16(loc, pm + 4) & 3 != 0 { return Err("Wi-Fi adapter is not powered in D0"); }
    }
    let raw = pci::read32(loc, 0x10);
    if raw & 1 != 0 || raw & 6 == 6 { return Err("Invalid Wi-Fi memory BAR"); }
    let bar = pci::bar(loc, 0);
    if bar == 0 || bar == 0xfffffff0 { return Err("Wi-Fi memory BAR is unassigned"); }
    let mmio = crate::vmm::map_mmio(bar, 8192) as usize;
    let command = pci::read16(loc, 4);
    let window = pci::read32(loc, 0x80);
    let wrapper_window = pci::read32(loc, 0xac);
    // Enable only memory decoding; do not start DMA or touch radio/reset registers.
    pci::write16(loc, 4, command | 2);
    let result = (|| {
        let select = |address| -> Result<(), &'static str> {
            pci::write32(loc, 0x80, address);
            if pci::read32(loc, 0x80) != address { return Err("BCMA window selection failed"); }
            Ok(())
        };
        select(0x18000000)?;
        let read = |offset| unsafe { core::ptr::read_volatile((mmio + offset) as *const u32) };
        let chip = read(0);
        if chip == u32::MAX || chip & 0xffff != 43225 { return Err("BCMA chip ID does not match BCM43225"); }
        if chip >> 28 != 1 { return Err("Unsupported Broadcom interconnect type"); }
        let erom = read(0xfc);
        if erom & 0xfff != 0 || !(0x18000000..0x19000000).contains(&erom) { return Err("Invalid BCMA enumeration address"); }
        select(erom)?;
        let mut words = [0u32; 1024];
        for (i, w) in words.iter_mut().enumerate() { *w = read(i * 4); }
        let scan = bcma_erom::parse(&words)?;
        let d11 = scan.cores[..scan.count].iter().find(|c| c.manufacturer == 0x4bf && c.id == 0x812).ok_or("BCMA Wi-Fi core not found")?;
        if d11.wrapper & 0xfff != 0 || !(0x18100000..0x19000000).contains(&d11.wrapper) { return Err("BCMA Wi-Fi reset wrapper not found"); }
        *HW.lock() = Some(Hw { loc, mmio, d11: d11.address, wrapper: d11.wrapper });
        pci::write32(loc, 0xac, d11.wrapper);
        if pci::read32(loc, 0xac) != d11.wrapper { return Err("BCMA wrapper window selection failed"); }
        let core = bcma_core::snapshot(&mut Wrapper { mmio })?;
        crate::kprintln!("THOS: wlan bcma        D11 rev {} registers {:#x} wrapper {:#x}", d11.revision, d11.address, d11.wrapper);
        crate::kprintln!("THOS: wlan core        {}, ioctl {:#x} iost {:#x} reset {:#x} busy {:#x}, DMA64 {}", core.state(), core.ioctl, core.iost, core.reset, core.busy, core.dma64());
        Ok((((chip >> 16) & 15) as u8, scan.count, d11.revision, core))
    })();
    pci::write32(loc, 0xac, wrapper_window);
    pci::write32(loc, 0x80, window);
    pci::write16(loc, 4, command);
    result
}

/// `/dev/wlan` commands (root), for bring-up on the real adapter only: `cc` reads ChipCommon/PMU and applies the PMU control bit,
/// `sprom` dumps the SPROM words, `core` enables the D11 core and reads its MAC registers. Results go to the kernel log.
pub fn control(cmd: &str) -> i64 {
    let Some(hw) = *HW.lock() else { return -19 };
    let command = pci::read16(hw.loc, 4);
    let (w0, w1) = (pci::read32(hw.loc, 0x80), pci::read32(hw.loc, 0xac));
    pci::write16(hw.loc, 4, command | 2);
    let rd = |o: usize| unsafe { core::ptr::read_volatile((hw.mmio + o) as *const u32) };
    let rd16 = |o: usize| unsafe { core::ptr::read_volatile((hw.mmio + o) as *const u16) };
    let r = match cmd.trim() {
        "cc" => {
            pci::write32(hw.loc, 0x80, 0x18000000);
            let (chip, caps, pmucap, ctl, stat) = (rd(0), rd(4), rd(0x604), rd(0x600), rd(0x608));
            crate::kprintln!("THOS: wlan cc          chip {:#x} caps {:#x} pmucap {:#x} pmuctl {:#x} pmustatus {:#x} minres {:#x} maxres {:#x}", chip, caps, pmucap, ctl, stat, rd(0x618), rd(0x61c));
            if pmucap & 0xff >= 2 { unsafe { core::ptr::write_volatile((hw.mmio + 0x600) as *mut u32, ctl | 0x200); } }
            crate::kprintln!("THOS: wlan cc          pmuctl now {:#x}", rd(0x600));
            0
        }
        "sprom" => {
            pci::write32(hw.loc, 0x80, 0x18000000);
            for row in 0..16 {
                let mut line = alloc::string::String::new();
                for i in 0..16 { line.push_str(&format!(" {:04x}", rd16(0x800 + (row * 16 + i) * 2))); }
                crate::kprintln!("THOS: wlan sprom {:3x}{}", row * 16, line);
            }
            0
        }
        "core" => {
            pci::write32(hw.loc, 0x80, hw.d11);
            pci::write32(hw.loc, 0xac, hw.wrapper);
            let mut w = Wrapper { mmio: hw.mmio };
            match bcma_core::enable(&mut w, 4) {
                Ok(()) => {
                    let s = bcma_core::snapshot(&mut w).ok();
                    crate::kprintln!("THOS: wlan core        enabled {:?}", s);
                    crate::kprintln!("THOS: wlan core        maccontrol {:#x} macintstatus {:#x} id {:#x}", rd(0x120), rd(0x128), rd(0));
                    0
                }
                Err(e) => { crate::kprintln!("THOS: wlan core        enable failed: {}", e); -5 }
            }
        }
        "ucode" => ucode_start(hw, &rd),
        _ => -22,
    };
    pci::write32(hw.loc, 0xac, w1);
    pci::write32(hw.loc, 0x80, w0);
    pci::write16(hw.loc, 4, command);
    r
}

const MACCONTROL: usize = 0x120;
const MACINTSTATUS: usize = 0x128;
const OBJADDR: usize = 0x160;
const OBJDATA: usize = 0x164;
const MCTL_WAKE: u32 = 1 << 26;
const MCTL_INFRA: u32 = 1 << 17;
const MCTL_IHR_EN: u32 = 1 << 10;
const MCTL_PSM_JMP_0: u32 = 1 << 2;
const MCTL_PSM_RUN: u32 = 1 << 1;
const MI_MACSSPNDD: u32 = 1;
/// brcmsmac brcms_b_coreinit: reset the PSM, upload the MIMO16 microcode, let it run until it suspends itself, write the init tables.
fn ucode_start(hw: Hw, rd: &dyn Fn(usize) -> u32) -> i64 {
    let wr = |o: usize, v: u32| unsafe { core::ptr::write_volatile((hw.mmio + o) as *mut u32, v) };
    let fw = (|| {
        let bin = crate::firmware::read("brcm/bcm43xx-0.fw", crate::brcm_firmware::MAX_BIN)?;
        let index = crate::firmware::read("brcm/bcm43xx_hdr-0.fw", crate::brcm_firmware::MAX_INDEX)?;
        let f = crate::brcm_firmware::Firmware::parse(&bin, &index)?;
        Ok::<_, &'static str>((f.section(10)?.to_vec(), f.section(8)?.to_vec(), f.section(9)?.to_vec()))
    })();
    let Ok((code, bsinit, init)) = fw else { crate::kprintln!("THOS: wlan ucode       firmware unavailable"); return -2 };
    crate::pci::write32(hw.loc, 0x80, hw.d11);
    crate::pci::write32(hw.loc, 0xac, hw.wrapper);
    let delay = |us: u64| { let t = unsafe { core::arch::x86_64::_rdtsc() }; let n = us * crate::timer::tsc_mhz().max(1); while unsafe { core::arch::x86_64::_rdtsc() }.wrapping_sub(t) < n { core::hint::spin_loop(); } };
    crate::kprintln!("THOS: wlan ucode       machwcap {:#x} phyversion {:#x} maccontrol {:#x}", rd(0x15c), rd(0x3e0) & 0xffff, rd(MACCONTROL));
    wr(MACCONTROL, MCTL_IHR_EN | MCTL_PSM_JMP_0 | MCTL_WAKE);
    wr(OBJADDR, 0x0300_0000); let _ = rd(OBJADDR);                       // auto-increment, microcode memory
    for w in code.chunks_exact(4) { wr(OBJDATA, u32::from_le_bytes([w[0], w[1], w[2], w[3]])); }
    wr(OBJADDR, 0x0200_0000); let _ = rd(OBJADDR);                       // read back the first words
    let back = [rd(OBJDATA), rd(OBJDATA), rd(OBJDATA), rd(OBJDATA)];
    crate::kprintln!("THOS: wlan ucode       uploaded {} bytes, readback {:08x?} expected {:02x?}", code.len(), back, &code[..16]);
    wr(MACINTSTATUS, u32::MAX);
    wr(MACCONTROL, MCTL_IHR_EN | MCTL_INFRA | MCTL_PSM_RUN | MCTL_WAKE);
    let mut waited = 0;
    while rd(MACINTSTATUS) & MI_MACSSPNDD == 0 && waited < 1000 { delay(1000); waited += 1; }
    crate::kprintln!("THOS: wlan ucode       macintstatus {:#x} after {} ms ({}), maccontrol {:#x}", rd(MACINTSTATUS), waited, if waited < 1000 { "ucode self-suspended" } else { "ucode did NOT suspend" }, rd(MACCONTROL));
    let write_inits = |table: &[u8]| { let mut n = 0; for e in table.chunks_exact(8) {
        let (addr, size, val) = (u16::from_le_bytes([e[0], e[1]]) as usize, u16::from_le_bytes([e[2], e[3]]), u32::from_le_bytes([e[4], e[5], e[6], e[7]]));
        if addr == 0xffff { break; }
        match size { 2 => unsafe { core::ptr::write_volatile((hw.mmio + addr) as *mut u16, val as u16) }, 4 => wr(addr, val), _ => break }
        n += 1; } n };
    let n = write_inits(&init);
    crate::kprintln!("THOS: wlan ucode       {} init values written", n);
    let _ = bsinit;
    0
}
