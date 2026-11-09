// SPDX-License-Identifier: GPL-2.0-or-later
//! The battery-backed CMOS real-time clock (MC146818 / its PCH descendants).
//!
//! Read once at boot: that wall-clock time plus the TSC-based monotonic clock
//! (`timer::monotonic_ns`) is `CLOCK_REALTIME`. The RTC is assumed to hold UTC
//! (what Linux and QEMU assume too). Century comes from register 0x32 when it looks
//! sane, else the year is taken as 20xx.

unsafe fn outb(port: u16, v: u8) {
    core::arch::asm!("out dx, al", in("dx") port, in("al") v, options(nomem, nostack, preserves_flags));
}
unsafe fn inb(port: u16) -> u8 {
    let v: u8;
    core::arch::asm!("in al, dx", out("al") v, in("dx") port, options(nomem, nostack, preserves_flags));
    v
}

fn reg(r: u8) -> u8 {
    unsafe {
        outb(0x70, r); // NMI left enabled (bit 7 clear)
        inb(0x71)
    }
}

fn updating() -> bool {
    reg(0x0A) & 0x80 != 0
}

#[derive(PartialEq, Clone, Copy)]
struct Raw {
    sec: u8,
    min: u8,
    hour: u8,
    day: u8,
    mon: u8,
    year: u8,
    century: u8,
}

fn read_raw() -> Raw {
    while updating() {
        core::hint::spin_loop();
    }
    Raw {
        sec: reg(0x00),
        min: reg(0x02),
        hour: reg(0x04),
        day: reg(0x07),
        mon: reg(0x08),
        year: reg(0x09),
        century: reg(0x32),
    }
}

fn bcd(v: u8) -> u8 {
    (v & 0x0F) + (v >> 4) * 10
}

/// Days since 1970-01-01 for a proleptic-Gregorian civil date (Howard Hinnant's
/// algorithm).
pub fn days_from_civil(y: i64, m: i64, d: i64) -> i64 {
    let y = if m <= 2 { y - 1 } else { y };
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = y - era * 400;
    let mp = (m + 9) % 12;
    let doy = (153 * mp + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146097 + doe - 719468
}

/// Current wall-clock time as seconds since the Unix epoch (UTC), from the CMOS
/// RTC. `None` if the RTC returns nonsense (no RTC, dead battery).
pub fn read_unix() -> Option<u64> {
    // A value is trustworthy once two consecutive reads agree (the clock may tick
    // between reading its fields).
    let mut a = read_raw();
    for _ in 0..8 {
        let b = read_raw();
        if a == b {
            break;
        }
        a = b;
    }
    let status_b = reg(0x0B);
    let binary = status_b & 0x04 != 0;
    let h24 = status_b & 0x02 != 0;

    let pm = !h24 && a.hour & 0x80 != 0;
    let mut hour = a.hour & 0x7F;
    let conv = |v: u8| if binary { v } else { bcd(v) };
    let (sec, min, day, mon, yy) = (conv(a.sec), conv(a.min), conv(a.day), conv(a.mon), conv(a.year));
    hour = conv(hour);
    if !h24 {
        hour %= 12;
        if pm {
            hour += 12;
        }
    }
    let century = if (0x19..=0x21).contains(&a.century) && !binary {
        bcd(a.century)
    } else if (19..=21).contains(&a.century) && binary {
        a.century
    } else {
        20
    };
    let year = century as i64 * 100 + yy as i64;
    if !(1..=12).contains(&mon) || !(1..=31).contains(&day) || hour > 23 || min > 59 || sec > 60 || year < 2020 {
        return None;
    }
    let days = days_from_civil(year, mon as i64, day as i64);
    Some((days * 86400 + hour as i64 * 3600 + min as i64 * 60 + sec as i64) as u64)
}

/// Civil date from days since 1970-01-01 (Howard Hinnant's algorithm), the inverse of `days_from_civil`.
fn civil_from_days(z: i64) -> (i64, i64, i64) {
    let z = z + 719468;
    let era = if z >= 0 { z } else { z - 146096 } / 146097;
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    (if m <= 2 { y + 1 } else { y }, m, d)
}

fn set_reg(r: u8, v: u8) {
    unsafe {
        outb(0x70, r);
        outb(0x71, v);
    }
}

/// Write the CMOS clock (UTC) so a reboot starts with the right time. The update cycle is halted with the SET bit while the
/// fields are written, in whatever format (BCD/binary, 12/24 h) the chip is configured for.
pub fn write_unix(secs: u64) {
    let days = (secs / 86_400) as i64;
    let rem = secs % 86_400;
    let (year, mon, day) = civil_from_days(days);
    let (hour, min, sec) = ((rem / 3600) as u8, ((rem % 3600) / 60) as u8, (rem % 60) as u8);
    let b = reg(0x0B);
    let binary = b & 0x04 != 0;
    let h24 = b & 0x02 != 0;
    let enc = |v: u8| if binary { v } else { (v / 10) << 4 | (v % 10) };
    let hour_enc = if h24 {
        enc(hour)
    } else {
        let pm = hour >= 12;
        let h12 = if hour % 12 == 0 { 12 } else { hour % 12 };
        enc(h12) | if pm { 0x80 } else { 0 }
    };
    while updating() {
        core::hint::spin_loop();
    }
    set_reg(0x0B, b | 0x80);                                   // SET: stop the update cycle
    set_reg(0x00, enc(sec));
    set_reg(0x02, enc(min));
    set_reg(0x04, hour_enc);
    set_reg(0x07, enc(day as u8));
    set_reg(0x08, enc(mon as u8));
    set_reg(0x09, enc((year % 100) as u8));
    let cent = reg(0x32);
    if (0x19..=0x21).contains(&cent) || (19..=21).contains(&cent) {      // only touch the century register when the chip has one
        set_reg(0x32, enc((year / 100) as u8));
    }
    set_reg(0x0B, b & !0x80);                                  // run again
}
