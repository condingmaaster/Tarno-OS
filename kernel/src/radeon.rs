// SPDX-License-Identifier: GPL-2.0-or-later
//! Radeon HD 5470 (Cedar, 1002:68e0) diagnostic foundation.
//! CONFIG_MEMSIZE and its MiB units: Linux radeon/evergreend.h, evergreen.c.
//! Retain the boot framebuffer; no modeset, command processor or DMA yet.
use alloc::{format, string::String};
use spin::Mutex;
#[derive(Clone, Copy)]
struct Status { present: bool, vram_mb: u32, detail: &'static str, firmware: &'static str }
static STATUS: Mutex<Status> = Mutex::new(Status { present: false, vram_mb: 0, detail: "Boot framebuffer active", firmware: "unchecked" });
pub fn info() -> String {
    let s = *STATUS.lock();
    format!("adapter {}\nrenderer software\nacceleration 0\nvram_mb {}\ndetail {}\nfirmware {}\nfirmware_loaded 0\nframebuffer_wc_configured {}\n", if s.present { "AMD Radeon HD 5470" } else { "Boot framebuffer" }, s.vram_mb, s.detail, s.firmware, (crate::pat::configured() || crate::mtrr::configured()) as u8)
}
pub fn init() {
    let checked = (|| {
        crate::firmware::check("radeon/CEDAR_me.bin", 5504, "86cec272c18673356c0e8d774d7ed48976baa631f79734225522a928e9089db4")?;
        crate::firmware::check("radeon/CEDAR_pfp.bin", 4480, "bf614bcdd72fd23a919d2bf39ec065824f0c327e50b9ba3d9d39cb675764fcf3")?;
        crate::firmware::check("radeon/CEDAR_rlc.bin", 3072, "937283012c1f07fc597240df91148906f2e6e68c8a2cb66b406db8b014c2f441")
    })();
    let firmware = match checked { Ok(()) => "validated", Err(e) => { crate::kprintln!("THOS: radeon firmware  {}", e); if e == "firmware file missing" { "missing" } else { "invalid" } } };
    STATUS.lock().firmware = firmware;
    crate::kprintln!("THOS: radeon firmware  {}, not uploaded", firmware);
    let Some(loc) = crate::pci::find_id(0x1002, 0x68e0) else { return; };
    let mut s = Status { present: true, vram_mb: 0, detail: "Detected; hardware acceleration pending", firmware };
    let raw = crate::pci::read32(loc, 0x18);
    let bar = crate::pci::bar(loc, 2);
    // Read registers only when the boot firmware already enabled memory decoding.
    let powered = crate::pci::find_cap(loc, 1).map_or(true, |pm| pm <= 0xf8 && crate::pci::read16(loc, pm + 4) & 3 == 0);
    if !powered || raw & 1 != 0 || raw & 6 == 6 || bar == 0 || bar == 0xfffffff0 || crate::pci::read16(loc, 4) & 2 == 0 {
        s.detail = "Detected; GPU register access unavailable";
    } else {
        let mmio = crate::vmm::map_mmio(bar, 0x6000);
        let mb = unsafe { core::ptr::read_volatile((mmio + 0x5428) as *const u32) };
        if mb == 0 || mb > 16384 { s.detail = "Detected; GPU memory information unavailable"; }
        else { s.vram_mb = mb; }
    }
    crate::kprintln!("THOS: radeon           1002:68e0 HD 5470, {} MiB, {}; boot framebuffer retained", s.vram_mb, s.detail);
    *STATUS.lock() = s;
}
