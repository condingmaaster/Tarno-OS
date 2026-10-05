use std::collections::HashMap;
use std::sync::{mpsc, Arc, Mutex};
use std::thread;
use std::time::Duration;
fn main() {
    let counter = Arc::new(Mutex::new(0u64));
    let mut hs = Vec::new();
    for i in 0..4 {
        let c = Arc::clone(&counter);
        hs.push(thread::spawn(move || {
            for _ in 0..500 { *c.lock().unwrap() += 1; }
            i * 10
        }));
    }
    let rets: Vec<i32> = hs.into_iter().map(|h| h.join().unwrap()).collect();
    println!("threads total={} rets={:?}", *counter.lock().unwrap(), rets);
    let (tx, rx) = mpsc::channel();
    let t = thread::spawn(move || { for i in 0..5 { tx.send(i * i).unwrap(); thread::sleep(Duration::from_millis(5)); } });
    let got: Vec<i32> = rx.iter().collect();
    t.join().unwrap();
    println!("channel got {:?}", got);
    let mut m: HashMap<String, usize> = HashMap::new();
    for w in "the quick brown fox jumps over the lazy dog the end".split(' ') { *m.entry(w.to_string()).or_insert(0) += 1; }
    let mut keys: Vec<_> = m.iter().collect();
    keys.sort();
    println!("words {:?}", &keys[..3]);
    let start = std::time::Instant::now();
    thread::sleep(Duration::from_millis(30));
    println!("slept ok: {}", start.elapsed().as_millis() >= 25);
    match std::fs::read_dir("C:\\") {
        Ok(rd) => println!("read_dir entries: {}", rd.count() > 3),
        Err(e) => println!("read_dir error {:?}", e),
    }
    let r = std::panic::catch_unwind(|| { let v: Vec<i32> = Vec::new(); v[3] });
    println!("caught panic: {}", r.is_err());
    match std::process::Command::new("C:\\whello.exe").arg("from-rust").status() {
        Ok(st) => println!("child status code={:?}", st.code()),
        Err(e) => println!("child spawn error {:?}", e),
    }
    match std::process::Command::new("C:\\whello.exe").arg("captured").output() {
        Ok(o) => println!("child output code={:?} stdout={:?}", o.status.code(), String::from_utf8_lossy(&o.stdout).trim()),
        Err(e) => println!("child output error {:?}", e),
    }
    println!("rustthreads-done");
}
