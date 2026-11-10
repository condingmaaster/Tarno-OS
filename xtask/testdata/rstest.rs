// SPDX-License-Identifier: GPL-2.0-or-later
// A Rust std program on THOS: threads, channels, HashMap (getrandom), files, time, loopback TCP, process spawn.
use std::collections::HashMap;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::mpsc;
use std::thread;
use std::time::Instant;

fn main() {
    let t0 = Instant::now();
    // threads + channel
    let (tx, rx) = mpsc::channel();
    let hs: Vec<_> = (0..4).map(|i| { let tx = tx.clone(); thread::spawn(move || { tx.send(i * 10).unwrap(); }) }).collect();
    drop(tx);
    for h in hs { h.join().unwrap(); }
    let mut got: Vec<i32> = rx.iter().collect();
    got.sort();
    assert_eq!(got, vec![0, 10, 20, 30]);
    println!("rs: threads ok {:?}", got);
    // HashMap uses random seeds (getrandom)
    let mut m = HashMap::new();
    for i in 0..1000 { m.insert(i, i * 2); }
    assert_eq!(m[&500], 1000);
    println!("rs: hashmap ok");
    // files
    std::fs::write("/tmp/rs.txt", b"rust-file").unwrap();
    let s = std::fs::read_to_string("/tmp/rs.txt").unwrap();
    assert_eq!(s, "rust-file");
    std::fs::rename("/tmp/rs.txt", "/tmp/rs2.txt").unwrap();
    assert!(std::fs::metadata("/tmp/rs2.txt").unwrap().len() == 9);
    std::fs::remove_file("/tmp/rs2.txt").unwrap();
    println!("rs: fs ok");
    // loopback TCP between two threads
    let l = TcpListener::bind("127.0.0.1:6000").unwrap();
    let srv = thread::spawn(move || {
        let (mut c, _) = l.accept().unwrap();
        let mut b = [0u8; 5];
        c.read_exact(&mut b).unwrap();
        c.write_all(&b.map(|x| x.to_ascii_uppercase())).unwrap();
    });
    let mut c = TcpStream::connect("127.0.0.1:6000").unwrap();
    c.write_all(b"hello").unwrap();
    let mut r = [0u8; 5];
    c.read_exact(&mut r).unwrap();
    assert_eq!(&r, b"HELLO");
    srv.join().unwrap();
    println!("rs: tcp ok");
    // spawn a child process
    let out = std::process::Command::new("/busybox").arg("echo").arg("child-says-hi").output().unwrap();
    assert_eq!(String::from_utf8_lossy(&out.stdout).trim(), "child-says-hi");
    println!("rs: process ok");
    println!("rs ok: {} ms", t0.elapsed().as_millis());
}
