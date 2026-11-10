// SPDX-License-Identifier: GPL-2.0-or-later
//! The kernel network service (stage N0 of `docs/thos/network-plan.md`): a NIC driver
//! behind `smoltcp`'s `Device` trait, an interface with a static address, and a kernel
//! thread that polls it. The first user is a one-shot ICMP ping of the gateway at boot.
//! Sockets for the two personalities come on top of this (N1+).

use alloc::vec;
use alloc::vec::Vec;

use smoltcp::iface::{Config, Interface, SocketHandle, SocketSet};
use smoltcp::phy::{self, ChecksumCapabilities, Device, DeviceCapabilities, Medium};
use smoltcp::socket::{dhcpv4, icmp, udp};
use smoltcp::time::Instant;
use smoltcp::wire::{
    EthernetAddress, HardwareAddress, Icmpv4Packet, Icmpv4Repr, IpAddress, IpCidr, Ipv4Address, Ipv4Cidr,
};
use spin::Mutex;

use crate::kprintln;
use crate::e1000::E1000;
use crate::tg3::Tg3;
use crate::virtio_net::{VirtioNet, MTU};

/// The NIC drivers behind one interface.
enum Driver {
    Virtio(VirtioNet),
    E1000(E1000),
    Tg3(Tg3),
}

impl Driver {
    fn link_up(&self) -> bool { match self { Self::Virtio(d) => d.link_up(), Self::E1000(d) => d.link_up(), Self::Tg3(d) => d.link_up() } }
    fn poll_rx(&mut self) {
        match self {
            Driver::Virtio(d) => d.poll_rx(),
            Driver::E1000(d) => d.poll_rx(),
            Driver::Tg3(d) => d.poll_rx(),
        }
    }
    fn pop_rx(&mut self) -> Option<Vec<u8>> {
        match self {
            Driver::Virtio(d) => d.rx_queue.pop_front(),
            Driver::E1000(d) => d.rx_queue.pop_front(),
            Driver::Tg3(d) => d.rx_queue.pop_front(),
        }
    }
    fn tx_ready(&mut self) -> bool {
        match self {
            Driver::Virtio(d) => d.tx_ready(),
            Driver::E1000(d) => d.tx_ready(),
            Driver::Tg3(d) => d.tx_ready(),
        }
    }
    fn transmit<F: FnOnce(&mut [u8])>(&mut self, len: usize, fill: F) -> bool {
        match self {
            Driver::Virtio(d) => d.transmit(len, fill),
            Driver::E1000(d) => d.transmit(len, fill),
            Driver::Tg3(d) => d.transmit(len, fill),
        }
    }
    fn mac(&self) -> [u8; 6] {
        match self {
            Driver::Virtio(d) => d.mac,
            Driver::E1000(d) => d.mac,
            Driver::Tg3(d) => d.mac,
        }
    }
    fn name(&self) -> &'static str {
        match self {
            Driver::Virtio(_) => "virtio-net",
            Driver::E1000(_) => "e1000",
            Driver::Tg3(_) => "tg3",
        }
    }
}

/// Our own IPv4 address as a big-endian number (0 until configured): frames to it loop back.
static OWN_IP: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);


/// The stack's `Device` — the driver plus the smoltcp token plumbing.
struct Nic {
    drv: Driver,
    /// Frames the stack sent to a 127.x address: handed straight back as received frames.
    lo: alloc::collections::VecDeque<Vec<u8>>,
    mac: [u8; 6],
}

struct RxTok(Vec<u8>);
struct TxTok<'a> {
    drv: &'a mut Driver,
    lo: &'a mut alloc::collections::VecDeque<Vec<u8>>,
    mac: [u8; 6],
}

impl phy::RxToken for RxTok {
    fn consume<R, F: FnOnce(&[u8]) -> R>(self, f: F) -> R {
        f(&self.0)
    }
}

impl<'a> phy::TxToken for TxTok<'a> {
    fn consume<R, F: FnOnce(&mut [u8]) -> R>(self, len: usize, f: F) -> R {
        // Build the frame first: traffic for 127.0.0.0/8 never leaves the machine.
        let mut tmp = vec![0u8; len];
        let r = f(&mut tmp);
        let own = OWN_IP.load(core::sync::atomic::Ordering::Relaxed).to_be_bytes();
        let ip_dst_loop = len >= 34 && tmp[12] == 0x08 && tmp[13] == 0x00 && (tmp[30] == 127 || (own != [0; 4] && tmp[30..34] == own));
        let arp_for_loop = len >= 42 && tmp[12] == 0x08 && tmp[13] == 0x06 && tmp[20] == 0 && tmp[21] == 1 && tmp[38] == 127;
        if ip_dst_loop {
            tmp[0..6].copy_from_slice(&self.mac); // addressed to ourselves
            self.lo.push_back(tmp);
        } else if arp_for_loop {
            // answer "who has 127.x" ourselves: it is us
            let mut rep = tmp.clone();
            rep[0..6].copy_from_slice(&tmp[6..12]); // eth dst = asker
            rep[6..12].copy_from_slice(&self.mac);
            rep[21] = 2; // ARP reply
            rep[22..28].copy_from_slice(&self.mac); // sender hw
            rep[28..32].copy_from_slice(&tmp[38..42]); // sender ip = the asked address
            rep[32..38].copy_from_slice(&tmp[22..28]); // target hw = asker
            rep[38..42].copy_from_slice(&tmp[28..32]); // target ip = asker
            self.lo.push_back(rep);
        } else {
            // The driver copies into its DMA buffer; if none is free the frame is dropped
            // (smoltcp retransmits what matters).
            self.drv.transmit(len, |buf| buf.copy_from_slice(&tmp));
        }
        r
    }
}

impl Device for Nic {
    type RxToken<'a> = RxTok;
    type TxToken<'a> = TxTok<'a>;

    fn receive(&mut self, _t: Instant) -> Option<(RxTok, TxTok<'_>)> {
        self.drv.poll_rx();
        let frame = match self.lo.pop_front() {
            Some(f) => f,
            None => self.drv.pop_rx()?,
        };
        Some((RxTok(frame), TxTok { drv: &mut self.drv, lo: &mut self.lo, mac: self.mac }))
    }
    fn transmit(&mut self, _t: Instant) -> Option<TxTok<'_>> {
        if self.drv.tx_ready() {
            Some(TxTok { drv: &mut self.drv, lo: &mut self.lo, mac: self.mac })
        } else {
            None
        }
    }
    fn capabilities(&self) -> DeviceCapabilities {
        let mut c = DeviceCapabilities::default();
        c.medium = Medium::Ethernet;
        c.max_transmission_unit = MTU;
        c.checksum = ChecksumCapabilities::default();
        c
    }
}

pub struct Stack {
    nic: Nic,
    pub iface: Interface,
    pub sockets: SocketSet<'static>,
    dhcp: Option<SocketHandle>,
    /// DNS servers from the lease (or the static fallback).
    pub dns: Vec<[u8; 4]>,
    /// A lease (or the static fallback) has been applied.
    pub configured: bool,
    enabled: bool,
    automatic: bool,
    router: Option<Ipv4Address>,
    /// Netconsole: the kernel log goes out as UDP datagrams to the gateway (port 6666) once there is an address, for machines without a serial port.
    con: SocketHandle,
    con_pos: usize,
}

pub static NET: Mutex<Option<Stack>> = Mutex::new(None);

fn now() -> Instant {
    Instant::from_micros((crate::timer::monotonic_ns() / 1000) as i64)
}

pub fn info() -> alloc::string::String {
    let g = NET.lock();
    let st = g.as_ref();
    let state = crate::net_config::state(st.is_some(), st.is_some_and(|s| s.enabled), st.is_some_and(|s| s.nic.drv.link_up()), st.is_some_and(|s| s.configured));
    let up = state == "connected";
    let ip = if up { st.and_then(|s| s.iface.ipv4_addr()).map_or([0;4], |a| a.octets()) } else { [0;4] };
    let mut dns = alloc::string::String::new();
    if up { for d in &st.unwrap().dns { dns.push_str(&alloc::format!("{}.{}.{}.{} ",d[0],d[1],d[2],d[3])); } }
    alloc::format!("link {}\nip {}.{}.{}.{}\ndns {}\nstate {}\ninterface {}\ndriver {}\naddress_mode {}\nwlan_present {}\n", if up {"up"} else {"down"},ip[0],ip[1],ip[2],ip[3],dns.trim(),state,if st.is_some() {"ethernet"} else {"none"},st.map_or("none",|s|s.nic.drv.name()),st.map_or("none",|s|if s.automatic {"dhcp"} else {"manual"}),crate::wlan::present() as u8)
}

/// The authenticated administrator may configure networking from Settings.
/// Reject invalid requests before modifying the running stack.
pub fn control(bytes: &[u8]) -> i64 {
    let uid = crate::process::current_uid();
    #[cfg(feature="interactive")]
    let admin = uid == crate::cred::ADMIN_UID;
    #[cfg(not(feature="interactive"))]
    let admin = false;
    if uid != 0 && !admin { return -13; }
    if bytes == b"wifi-recheck" { crate::wlan::init(); return bytes.len() as i64; }
    let cmd = match crate::net_config::parse(bytes) { Ok(c) => c, Err(e) => return e };
    let mut g = NET.lock(); let Some(st) = g.as_mut() else { return -19; };
    if let Some(h) = st.dhcp.take() { st.sockets.remove(h); }
    st.clear_address(); st.automatic = false;
    match cmd {
        crate::net_config::Command::Dhcp => { st.enabled = true; st.automatic = true; st.dhcp = Some(st.sockets.add(dhcpv4::Socket::new())); }
        crate::net_config::Command::Disconnect => { st.enabled = false; }
        crate::net_config::Command::Static { ip, prefix, gateway, dns } => {
            st.enabled = true;
            st.set_address(Ipv4Cidr::new(Ipv4Address::from(ip),prefix), if gateway == [0;4] {None} else {Some(Ipv4Address::from(gateway))},if dns == [0;4] {Vec::new()} else {vec![dns]});
        }
    }
    bytes.len() as i64
}

impl Stack {
    /// Send new kernel log text as UDP datagrams of up to 1000 bytes (a few per call); the position only advances for what was queued.
    fn netconsole(&mut self) {
        if !self.configured || !self.enabled { return; }
        let o = self.router.map_or([255, 255, 255, 255], |r| r.octets()); // the gateway (the PC that shares its network, or the QEMU host); broadcast without one
        let dst = smoltcp::wire::IpEndpoint::new(IpAddress::v4(o[0], o[1], o[2], o[3]), 6666);
        for _ in 0..4 {
            let mut buf = [0u8; 1000];
            let mut pos = self.con_pos;
            let n = crate::serial::log_read(&mut pos, &mut buf);
            if n == 0 { break; }
            if self.sockets.get_mut::<udp::Socket>(self.con).send_slice(&buf[..n], dst).is_err() { break; }
            self.con_pos = pos;
        }
    }

    pub fn poll(&mut self) {
        self.iface.poll(now(), &mut self.nic, &mut self.sockets);
        if self.automatic { self.service_dhcp(); }
        if !self.nic.drv.link_up() && self.configured && self.automatic {
            self.clear_address();
            if let Some(h) = self.dhcp { self.sockets.get_mut::<dhcpv4::Socket>(h).reset(); }
        }
    }

    fn set_address(&mut self, addr: Ipv4Cidr, router: Option<Ipv4Address>, dns: Vec<[u8; 4]>) {
        self.iface.update_ip_addrs(|a| {
            a.clear();
            OWN_IP.store(u32::from_be_bytes(addr.address().octets()), core::sync::atomic::Ordering::Relaxed);
            let _ = a.push(IpCidr::Ipv4(addr));
            let _ = a.push(IpCidr::Ipv4(Ipv4Cidr::new(Ipv4Address::new(127, 0, 0, 1), 8))); // after the real one: ipv4_addr() = first
        });
        match router {
            Some(r) => {
                let _ = self.iface.routes_mut().add_default_ipv4_route(r);
            }
            None => self.iface.routes_mut().remove_default_ipv4_route().map(|_| ()).unwrap_or(()),
        }
        self.dns = dns;
        self.router = router;
        self.configured = true;
    }

    /// Apply DHCP lease changes as they arrive.
    fn service_dhcp(&mut self) {
        let Some(dhcp) = self.dhcp else { return; };
        // Copy the lease out of the socket before touching `self` again.
        let lease = match self.sockets.get_mut::<dhcpv4::Socket>(dhcp).poll() {
            Some(dhcpv4::Event::Configured(cfg)) => {
                Some(Some((cfg.address, cfg.router, cfg.dns_servers.iter().map(|d| d.octets()).collect::<Vec<_>>())))
            }
            Some(dhcpv4::Event::Deconfigured) => Some(None),
            None => None,
        };
        match lease {
            Some(Some((addr, router, dns))) => {
                kprintln!(
                    "THOS: net dhcp         lease {} via {} dns {:?}",
                    addr,
                    router.map_or(alloc::string::String::from("-"), |r| alloc::format!("{r}")),
                    dns
                );
                self.set_address(addr, router, dns);
            }
            Some(None) => {
                self.clear_address();
            }
            None => {}
        }
    }

    fn clear_address(&mut self) {
        self.iface.update_ip_addrs(|a| { a.clear(); let _ = a.push(IpCidr::Ipv4(Ipv4Cidr::new(Ipv4Address::new(127,0,0,1),8))); });
        self.iface.routes_mut().remove_default_ipv4_route();
        self.configured = false;
        self.router = None;
        self.dns.clear();
        OWN_IP.store(0, core::sync::atomic::Ordering::Relaxed);
    }
}

/// `/etc/resolv.conf` as the resolver should see it: the lease's DNS servers. `None` while
/// the stack is down or has no DNS server (the file on disk, if any, is used then).
pub fn resolv_conf() -> Option<alloc::string::String> {
    let g = NET.lock();
    let st = g.as_ref()?;
    if !st.configured || !st.enabled || !st.nic.drv.link_up() { return Some(alloc::string::String::new()); }
    if st.dns.is_empty() {
        return None;
    }
    let mut out = alloc::string::String::new();
    for d in &st.dns {
        out.push_str(&alloc::format!("nameserver {}.{}.{}.{}\n", d[0], d[1], d[2], d[3]));
    }
    Some(out)
}

/// The interface address of an already-locked stack.
pub fn local_ip_of(st: &Stack) -> [u8; 4] {
    st.iface.ipv4_addr().map(|a| a.octets()).unwrap_or([0; 4])
}

/// The interface's IPv4 address (0.0.0.0 before `init`).
pub fn local_ip() -> [u8; 4] {
    NET.lock()
        .as_ref()
        .filter(|st| st.configured && st.enabled && st.nic.drv.link_up())
        .and_then(|st| st.iface.ipv4_addr())
        .map(|a| a.octets())
        .unwrap_or([0; 4])
}

/// Bring the NIC and the stack up. `Err` (no virtio-net device) is normal on real hardware
/// until its driver exists.
pub fn init() -> Result<(), &'static str> {
    let drv = match VirtioNet::probe() {
        Ok(d) => Driver::Virtio(d),
        Err(_) => match E1000::probe() {
            Ok(d) => Driver::E1000(d),
            Err(_) => Driver::Tg3(Tg3::probe()?),
        },
    };
    let mac = drv.mac();
    let mut dev = Nic { drv, lo: alloc::collections::VecDeque::new(), mac };
    let cfg = Config::new(HardwareAddress::Ethernet(EthernetAddress(mac)));
    let iface = Interface::new(cfg, &mut dev, now());
    kprintln!(
        "THOS: net nic          {} {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
        dev.drv.name(), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );
    let mut sockets = SocketSet::new(Vec::new());
    let dhcp = sockets.add(dhcpv4::Socket::new());
    let con = {
        let rx = udp::PacketBuffer::new(vec![udp::PacketMetadata::EMPTY; 1], vec![0u8; 16]);
        let tx = udp::PacketBuffer::new(vec![udp::PacketMetadata::EMPTY; 8], vec![0u8; 8 * 1024]);
        let mut s = udp::Socket::new(rx, tx);
        let _ = s.bind(6665);
        sockets.add(s)
    };
    let mut iface = iface;
    iface.update_ip_addrs(|a| {
        let _ = a.push(IpCidr::Ipv4(Ipv4Cidr::new(Ipv4Address::new(127, 0, 0, 1), 8)));
    });
    *NET.lock() = Some(Stack { nic: dev, iface, sockets, dhcp: Some(dhcp), dns: Vec::new(), configured: false, enabled: true, automatic: true, router: None, con, con_pos: 0 });
    Ok(())
}

/// Ping `dst` once per second, up to `tries` times; the round-trip time in ms of the first
/// reply.
fn ping(dst: [u8; 4], tries: u32) -> Option<u64> {
    let handle: SocketHandle = {
        let mut g = NET.lock();
        let st = g.as_mut()?;
        let rx = icmp::PacketBuffer::new(vec![icmp::PacketMetadata::EMPTY; 4], vec![0u8; 512]);
        let tx = icmp::PacketBuffer::new(vec![icmp::PacketMetadata::EMPTY; 4], vec![0u8; 512]);
        let mut s = icmp::Socket::new(rx, tx);
        s.bind(icmp::Endpoint::Ident(0x7405)).ok()?;
        st.sockets.add(s)
    };
    let dst_ip = IpAddress::v4(dst[0], dst[1], dst[2], dst[3]);
    let mut result = None;
    'tries: for seq in 0..tries as u16 {
        let sent = crate::timer::monotonic_ns();
        {
            let mut g = NET.lock();
            let st = g.as_mut()?;
            let s = st.sockets.get_mut::<icmp::Socket>(handle);
            let repr = Icmpv4Repr::EchoRequest { ident: 0x7405, seq_no: seq, data: b"THOS-ping" };
            if let Ok(buf) = s.send(repr.buffer_len(), dst_ip) {
                repr.emit(&mut Icmpv4Packet::new_unchecked(buf), &ChecksumCapabilities::default());
            }
        }
        // Poll for up to a second (ARP resolution makes the first echo take two round trips).
        for _ in 0..100 {
            {
                let mut g = NET.lock();
                let st = g.as_mut()?;
                st.poll();
                let s = st.sockets.get_mut::<icmp::Socket>(handle);
                while let Ok((payload, _addr)) = s.recv() {
                    if let Ok(p) = Icmpv4Packet::new_checked(payload) {
                        if let Ok(Icmpv4Repr::EchoReply { ident: 0x7405, .. }) =
                            Icmpv4Repr::parse(&p, &ChecksumCapabilities::default())
                        {
                            result = Some((crate::timer::monotonic_ns() - sent) / 1_000_000);
                            break 'tries;
                        }
                    }
                }
            }
            crate::timer::sleep_ns(10_000_000);
        }
    }
    if let Some(st) = NET.lock().as_mut() {
        st.sockets.remove(handle);
    }
    result
}

/// The network thread: boot-time gateway ping, then keep the stack serviced.
pub extern "C" fn net_thread(_: usize) -> ! {
    // Wait briefly for a real lease; keep waiting without a fabricated fallback.
    for _ in 0..300 {
        {
            let mut g = NET.lock();
            let Some(st) = g.as_mut() else { break };
            st.poll();
            if st.configured {
                break;
            }
        }
        crate::timer::sleep_ns(10_000_000);
    }
    let gateway = NET.lock().as_ref().and_then(|st| st.router).map(|r| r.octets());
    if let Some(gateway) = gateway { match ping(gateway, 3) {
        Some(ms) => kprintln!(
            "THOS: net ok           ping {}.{}.{}.{} answered in {} ms (ARP + ICMP over virtio-net + smoltcp)",
            gateway[0], gateway[1], gateway[2], gateway[3], ms
        ),
        None => kprintln!("THOS: net FAIL         gateway did not answer ping"),
    } } else { kprintln!("THOS: net dhcp         no lease/router yet; waiting without a fabricated address"); }
    loop {
        if let Some(st) = NET.lock().as_mut() {
            st.netconsole();
            st.poll();
            crate::net_sock::reap(st);
        }
        crate::timer::sleep_ns(10_000_000);
    }
}
