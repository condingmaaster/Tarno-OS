// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded text protocol shared by the settings UI and the network service.
#[derive(Debug, PartialEq, Eq)]
pub enum Command { Dhcp, Disconnect, Static { ip: [u8;4], prefix: u8, gateway: [u8;4], dns: [u8;4] } }
fn ip(s: &str) -> Result<[u8;4], i64> {
    let mut parts = s.split('.'); let mut out = [0;4];
    for x in &mut out { let s = parts.next().ok_or(-22)?; if s.is_empty() || !s.bytes().all(|c| c.is_ascii_digit()) { return Err(-22); } *x = s.parse().map_err(|_| -22)?; }
    if parts.next().is_some() { return Err(-22); } Ok(out)
}
pub fn parse(bytes: &[u8]) -> Result<Command, i64> {
    if bytes.len() > 160 { return Err(-22); }
    let s = core::str::from_utf8(bytes).map_err(|_| -22)?;
    let mut p = s.split_whitespace();
    let cmd = match p.next() {
        Some("dhcp") => Command::Dhcp,
        Some("disconnect") => Command::Disconnect,
        Some("static") => {
            let ip = ip(p.next().ok_or(-22)?)?;
            let prefix: u8 = p.next().ok_or(-22)?.parse().map_err(|_| -22)?;
            let gateway = self::ip(p.next().ok_or(-22)?)?; let dns = self::ip(p.next().ok_or(-22)?)?;
            if !(1..=30).contains(&prefix) || ip[0] == 0 || ip[0] == 127 || ip[0] >= 224 { return Err(-22); }
            let mask = u32::MAX << (32-prefix); let addr = u32::from_be_bytes(ip);
            if addr & !mask == 0 || addr & !mask == !mask { return Err(-22); }
            for endpoint in [gateway,dns] { if endpoint != [0;4] && (endpoint[0] == 0 || endpoint[0] == 127 || endpoint[0] >= 224) { return Err(-22); } }
            if gateway != [0;4] && (u32::from_be_bytes(gateway) & mask != addr & mask || gateway == ip || u32::from_be_bytes(gateway) & !mask == 0 || u32::from_be_bytes(gateway) & !mask == !mask) { return Err(-22); }
            Command::Static { ip, prefix, gateway, dns }
        }
        _ => return Err(-22),
    };
    if p.next().is_some() { return Err(-22); } Ok(cmd)
}
pub fn state(present: bool, enabled: bool, carrier: bool, configured: bool) -> &'static str {
    if !present { "no-adapter" } else if !enabled { "disabled" } else if !carrier { "no-carrier" } else if !configured { "dhcp-pending" } else { "connected" }
}
#[cfg(test)] mod tests {
    use super::*;
    #[test] fn accepts_explicit_configs() { assert_eq!(parse(b"dhcp\n"),Ok(Command::Dhcp)); assert!(parse(b"static 192.168.1.20 24 192.168.1.1 1.1.1.1").is_ok()); assert!(parse(b"static 192.168.1.20 24 0.0.0.0 0.0.0.0").is_ok()); }
    #[test] fn rejects_invalid_and_trailing_fields() { for s in ["dhcp extra","static 127.0.0.1 8 0.0.0.0 0.0.0.0","static 10.0.0.256 24 0.0.0.0 0.0.0.0","static 10.0.0.0 24 0.0.0.0 0.0.0.0","static 10.0.0.255 24 0.0.0.0 0.0.0.0","static 10.0.0.2 24 10.1.0.1 1.1.1.1","static 10.0.0.2 0 0.0.0.0 0.0.0.0"] { assert!(parse(s.as_bytes()).is_err(),"{}",s); } }
    #[test] fn carrier_without_address_is_not_connected() { assert_eq!(state(true,true,true,false),"dhcp-pending"); assert_eq!(state(true,true,false,true),"no-carrier"); assert_eq!(state(true,false,true,true),"disabled"); assert_eq!(state(false,true,true,true),"no-adapter"); }
}
