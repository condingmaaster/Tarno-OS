// SPDX-License-Identifier: GPL-2.0-or-later
// Actual registry.rs transaction logic with a host filesystem adapter. This
// proves staging/serialization/notifications, not the ext2/block driver itself.
extern crate alloc;
extern crate self as spin;
use std::sync::atomic::{AtomicBool,Ordering};
pub struct Mutex<T>(std::sync::Mutex<T>);
impl<T> Mutex<T>{pub const fn new(v:T)->Self{Self(std::sync::Mutex::new(v))}pub fn lock(&self)->std::sync::MutexGuard<'_,T>{self.0.lock().unwrap()}}
mod io_mutex{pub type IoMutex<T>=crate::Mutex<T>;}
mod wait{use std::sync::atomic::{AtomicBool,Ordering};pub struct Event(AtomicBool);impl Event{pub fn new()->Self{Self(AtomicBool::new(false))}pub fn signal(&self){self.0.store(true,Ordering::Release);}pub fn is_signaled(&self)->bool{self.0.load(Ordering::Acquire)}}}
static DIRECTORY:std::sync::OnceLock<std::path::PathBuf>=std::sync::OnceLock::new();
static FAIL:AtomicBool=AtomicBool::new(false);static BLOCK:AtomicBool=AtomicBool::new(false);static ENTERED:AtomicBool=AtomicBool::new(false);static RELEASE:AtomicBool=AtomicBool::new(false);
mod ext2{
 use super::*;use std::os::unix::fs::MetadataExt;
 static PATHS:Mutex<Vec<std::path::PathBuf>>=Mutex::new(Vec::new());
 pub struct Ext2;pub struct Inode{pub mode:u16}
 fn local(p:&str)->std::path::PathBuf{DIRECTORY.get().unwrap().join(p.trim_start_matches('/'))}
 pub fn open()->Result<Ext2,&'static str>{Ok(Ext2)}
 impl Ext2{
  pub fn path_lookup_nofollow(&self,p:&str)->Option<u32>{let p=local(p);std::fs::symlink_metadata(&p).ok()?;let mut paths=PATHS.lock();paths.push(p);Some(paths.len() as u32)}
  pub fn read_inode(&self,i:u32)->Inode{let p=PATHS.lock()[(i-1) as usize].clone();Inode{mode:std::fs::symlink_metadata(p).unwrap().mode() as u16}}
  pub fn mkdir_path(&self,p:&str)->Result<(),&'static str>{std::fs::create_dir(local(p)).map_err(|_|"mkdir")}
  pub fn read_path(&self,p:&str)->Option<Vec<u8>>{std::fs::read(local(p)).ok()}
  pub fn write_path(&self,p:&str,b:&[u8])->Result<(),&'static str>{
   if BLOCK.load(Ordering::Acquire){ENTERED.store(true,Ordering::Release);while !RELEASE.load(Ordering::Acquire){std::thread::yield_now();}}
   if FAIL.load(Ordering::Acquire){return Err("injected write failure");}
   let p=local(p);let tmp=p.with_extension("txn");std::fs::write(&tmp,b).map_err(|_|"write")?;std::fs::rename(tmp,p).map_err(|_|"rename")
  }
 }
}
#[path="../../kernel/src/registry.rs"]mod registry;
const BASE:&str="\\Registry\\User\\HostTransaction";
fn main(){
 let mut args=std::env::args();let own=args.next().unwrap();let replay=args.next();
 let dir=if replay.as_deref()==Some("--reload")||replay.as_deref()==Some("--oversized"){std::path::PathBuf::from(args.next().unwrap())}else{std::path::PathBuf::from(format!("/tmp/thos-registry-transaction-{}",std::process::id()))};
 DIRECTORY.set(dir.clone()).unwrap();
 if replay.as_deref()==Some("--oversized"){
  assert_eq!(registry::load_hives(&ext2::Ext2),1);let saved=std::fs::read(dir.join("etc/thos/registry/user.hiv")).unwrap();assert!(registry::open(BASE));FAIL.store(true,Ordering::Release);
  assert_eq!(registry::create_for_user(BASE,1000),Ok(false));assert_eq!(registry::set_value_for_user(BASE,"too-many-keys",4,b"x",1000),Err(registry::Error::Resources));assert_eq!(registry::query_value(BASE,"too-many-keys"),None);assert_eq!(std::fs::read(dir.join("etc/thos/registry/user.hiv")).unwrap(),saved);println!("registry-host oversized loaded hive stays readable; no-op create without IO; mutation refused before clone PASS");return;
 }
 if replay.as_deref()==Some("--reload"){
  assert_eq!(registry::load_hives(&ext2::Ext2),1);assert_eq!(registry::owner_of(BASE),Some(1000));
  for i in 0..8u32{for j in 0..32u32{assert_eq!(registry::query_value(BASE,&format!("w{i}-{j}")),Some((4,(i*100+j).to_le_bytes().to_vec())));}}
  println!("registry-host actual persisted hive reload PASS");return;
 }
 assert!(!dir.exists());std::fs::create_dir(&dir).unwrap();assert_eq!(registry::load_hives(&ext2::Ext2),0);
 assert!(registry::create_owned(BASE,1000));assert_eq!(registry::create_for_user(BASE,1000),Ok(false));
 assert_eq!(registry::set_value_for_user(BASE,"Value",4,b"old",1000),Ok(()));
 assert_eq!(registry::set_value_for_user(BASE,"Value",4,b"bad",2000),Err(registry::Error::Denied));
 let hive=dir.join("etc/thos/registry/user.hiv");let saved=std::fs::read(&hive).unwrap();let ev=std::sync::Arc::new(wait::Event::new());assert!(registry::watch(BASE,true,ev.clone()));
 FAIL.store(true,Ordering::Release);
 assert_eq!(registry::set_value_for_user(BASE,"Value",4,b"new",1000),Err(registry::Error::Io));assert_eq!(registry::query_value(BASE,"Value"),Some((4,b"old".to_vec())));assert_eq!(std::fs::read(&hive).unwrap(),saved);assert!(!ev.is_signaled());
 assert_eq!(registry::create_for_user(&format!("{BASE}\\failed"),1000),Err(registry::Error::Io));assert!(!registry::open(&format!("{BASE}\\failed")));
 assert_eq!(registry::delete_key_for_user(BASE,1000),Err(registry::Error::Io));assert!(registry::open(BASE));assert_eq!(std::fs::read(&hive).unwrap(),saved);assert!(!ev.is_signaled());
 FAIL.store(false,Ordering::Release);BLOCK.store(true,Ordering::Release);
 let worker=std::thread::spawn(||registry::set_value_for_user(BASE,"Value",4,b"new",1000));let deadline=std::time::Instant::now()+std::time::Duration::from_secs(5);while !ENTERED.load(Ordering::Acquire){assert!(std::time::Instant::now()<deadline);std::thread::yield_now();}
 let(tx,rx)=std::sync::mpsc::channel();let reader=std::thread::spawn(move||tx.send(registry::query_value(BASE,"Value")).unwrap());assert_eq!(rx.recv_timeout(std::time::Duration::from_secs(2)).unwrap(),Some((4,b"old".to_vec())));reader.join().unwrap();assert!(!ev.is_signaled());
 RELEASE.store(true,Ordering::Release);assert_eq!(worker.join().unwrap(),Ok(()));BLOCK.store(false,Ordering::Release);assert!(ev.is_signaled());assert_eq!(registry::query_value(BASE,"Value"),Some((4,b"new".to_vec())));
 let barrier=std::sync::Arc::new(std::sync::Barrier::new(8));let mut workers=Vec::new();for i in 0..8u32{let b=barrier.clone();workers.push(std::thread::spawn(move||{b.wait();let created=registry::create_for_user(&format!("{BASE}\\shared"),1000).unwrap();for j in 0..32u32{registry::set_value_for_user(BASE,&format!("w{i}-{j}"),4,&(i*100+j).to_le_bytes(),1000).unwrap();}created}));}
 assert_eq!(workers.into_iter().map(|w|w.join().unwrap() as usize).sum::<usize>(),1);
 for i in 0..8u32{for j in 0..32u32{assert_eq!(registry::query_value(BASE,&format!("w{i}-{j}")),Some((4,(i*100+j).to_le_bytes().to_vec())));}}
 assert!(std::process::Command::new(own).arg("--reload").arg(&dir).status().unwrap().success());
 // Bound large staged data and serialized hive output before persistence.
 assert_eq!(registry::set_value_for_user(BASE,"",0,b"",1000),Ok(()));let unicode=format!("{BASE}\\目录😀");assert!(registry::create_owned(&unicode,u32::MAX));assert_eq!(registry::set_value_for_user(&unicode,"Größe",u32::MAX,b"",0),Ok(()));
 let large=vec![0x53;registry::MAX_VALUE_BYTES];assert_eq!(registry::set_value_for_user(BASE,"budget-one",3,&large,1000),Ok(()));let saved=std::fs::read(&hive).unwrap();let ev=std::sync::Arc::new(wait::Event::new());assert!(registry::watch(BASE,true,ev.clone()));
 assert_eq!(registry::set_value_for_user(BASE,"budget-two",3,&large,1000),Err(registry::Error::Resources));assert_eq!(registry::query_value(BASE,"budget-two"),None);assert_eq!(registry::query_value(BASE,"budget-one"),Some((3,large.clone())));assert_eq!(std::fs::read(&hive).unwrap(),saved);assert!(!ev.is_signaled());
 assert_eq!(registry::set_value_for_user(BASE,"oversized-value",3,&vec![0;registry::MAX_VALUE_BYTES+1],1000),Err(registry::Error::Resources));assert_eq!(registry::query_value(BASE,"oversized-value"),None);assert!(!ev.is_signaled());FAIL.store(true,Ordering::Release);assert_eq!(registry::create_for_user(BASE,1000),Ok(false));FAIL.store(false,Ordering::Release);
 assert_eq!(registry::set_value_for_user(BASE,"budget-one",3,b"",1000),Ok(()));assert!(ev.is_signaled());
 let hex=|s:&str|s.as_bytes().iter().map(|b|format!("{b:02x}")).collect::<String>();let mut oversized=format!("thos-hive v1\nK {}\nO {} 1000\n",hex("hosttransaction"),hex("hosttransaction"));for i in 0..4096{oversized.push_str(&format!("K {}\n",hex(&format!("hosttransaction\\node{i}"))));}std::fs::write(&hive,oversized).unwrap();assert!(std::process::Command::new(std::env::current_exe().unwrap()).arg("--oversized").arg(&dir).status().unwrap().success());
 println!("registry-host max value/hive/key budgets, empty/UTF8/max-u32 fields, no false resource notification and retry PASS");
 // A nonregular final hive must be refused, rather than overwriting a directory.
 std::fs::remove_file(&hive).unwrap();std::fs::create_dir(&hive).unwrap();assert_eq!(registry::set_value_for_user(BASE,"Value",4,b"invalid",1000),Err(registry::Error::Io));assert_eq!(registry::query_value(BASE,"Value"),Some((4,b"new".to_vec())));
 std::fs::remove_dir(&hive).unwrap();std::os::unix::fs::symlink("/tmp",&hive).unwrap();assert_eq!(registry::set_value_for_user(BASE,"Value",4,b"invalid",1000),Err(registry::Error::Io));assert_eq!(registry::query_value(BASE,"Value"),Some((4,b"new".to_vec())));
 std::fs::remove_dir_all(&dir).unwrap();println!("registry-host reported IO rollback/create/delete/notify, old readers during IO, 8 writers/256 updates, one creator, uid policy, nonregular hive and actual reload PASS");
}
