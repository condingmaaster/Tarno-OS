use std::io::Write;
fn main() {
    let args: Vec<String> = std::env::args().collect();
    println!("hello from rust on THOS, {} args", args.len());
    let mut v: Vec<u64> = (1..2000).map(|x| x * x % 977).collect();
    v.sort();
    println!("sorted first={} last={}", v[0], v[v.len() - 1]);
    match std::fs::File::create("C:\\tmp\\rw.txt") {
        Ok(mut f) => {
            println!("created");
            match writeln!(f, "written by rust") {
                Ok(()) => println!("wrote"),
                Err(e) => println!("write error {:?}", e),
            }
        }
        Err(e) => println!("create error {:?}", e),
    }
    match std::fs::read_to_string("C:\\tmp\\rw.txt") {
        Ok(s) => println!("read back: {}", s.trim()),
        Err(e) => println!("read error {:?}", e),
    }
    println!("{:?}", std::time::Instant::now().elapsed().as_secs() < 5);
    println!("env PATH set: {}", std::env::var("PATH").is_ok());
    println!("rust-done");
}
