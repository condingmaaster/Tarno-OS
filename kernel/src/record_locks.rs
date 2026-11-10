// SPDX-License-Identifier: GPL-2.0-or-later
//! Process-owned advisory ext2 byte-range locks (Linux F_GETLK/SETLK/SETLKW).
//! Separate from BSD flock, whose ownership and close rules differ.
use alloc::sync::Arc;
use alloc::vec::Vec;
use spin::Mutex;
use core::sync::atomic::{AtomicU64,Ordering};
use crate::wait::WaitQueue;
use crate::{file::FileOps, process, usercopy};
const EOF: u64 = u64::MAX;
#[derive(Clone, Copy)]
struct Record { inode:u64, pid:u64, lo:u64, hi:u64, kind:u16 }
impl Record {
    fn overlaps(self, other:Self)->bool {self.inode==other.inode&&self.lo<other.hi&&other.lo<self.hi}
    fn conflicts(self, other:Self)->bool {self.pid!=other.pid&&self.overlaps(other)&&(self.kind==1||other.kind==1)}
}
struct Waiting {tid:u64, request:Record}
struct State {locks:Vec<Record>, waiting:Vec<Waiting>}
static STATE:Mutex<State>=Mutex::new(State {locks:Vec::new(),waiting:Vec::new()});
static GENERATION:AtomicU64=AtomicU64::new(0);
static CHANGED:WaitQueue=WaitQueue::new();
fn changed(){GENERATION.fetch_add(1,Ordering::AcqRel);CHANGED.wake_all_no_poll();}
impl State {
    fn cycle(&self, request:Record)->Result<bool,i64> {
        let capacity=self.locks.len().saturating_add(1);
        let mut todo=Vec::new();let mut seen=Vec::new();
        todo.try_reserve(capacity).map_err(|_|-37i64)?;seen.try_reserve(capacity).map_err(|_|-37i64)?;
        for l in &self.locks {if request.conflicts(*l)&&!todo.contains(&l.pid){todo.push(l.pid);}}
        while let Some(pid)=todo.pop() {
            if pid==request.pid{return Ok(true);}
            if seen.contains(&pid){continue;}seen.push(pid);
            for w in self.waiting.iter().filter(|w|w.request.pid==pid) {
                for l in &self.locks {
                    if w.request.conflicts(*l)&&!seen.contains(&l.pid)&&!todo.contains(&l.pid){todo.push(l.pid);}
                }
            }
        }
        Ok(false)
    }
    fn replace(&mut self, request:Record)->i64 {
        // Reserve before changing any lock, so ENOLCK leaves the old set intact.
        let splits=self.locks.iter().filter(|l|l.pid==request.pid&&l.overlaps(request)&&l.lo<request.lo&&l.hi>request.hi).count();
        if self.locks.try_reserve(splits+usize::from(request.kind!=2)).is_err(){return -37;}
        let mut i=0;
        while i<self.locks.len() {
            let old=self.locks[i];
            if old.pid!=request.pid||!old.overlaps(request){i+=1;continue;}
            if old.lo<request.lo {
                self.locks[i].hi=request.lo;
                if old.hi>request.hi {self.locks.push(Record {lo:request.hi,..old});}
                i+=1;
            } else if old.hi>request.hi {self.locks[i].lo=request.hi;i+=1;}
            else {self.locks.swap_remove(i);}
        }
        if request.kind!=2 {self.locks.push(request);}
        self.locks.sort_unstable_by_key(|l|(l.inode,l.pid,l.kind,l.lo));
        let mut used=0;
        for i in 0..self.locks.len() {
            let l=self.locks[i];
            if used>0 {
                let previous=&mut self.locks[used-1];
                if previous.inode==l.inode&&previous.pid==l.pid&&previous.kind==l.kind&&previous.hi>=l.lo {previous.hi=previous.hi.max(l.hi);continue;}
            }
            self.locks[used]=l;used+=1;
        }
        self.locks.truncate(used);0
    }
}
/// Closing ANY description of this inode releases this process's record locks.
pub fn closed(pid:u64,inode:u64) {STATE.lock().locks.retain(|l|l.pid!=pid||l.inode!=inode);changed();}
pub fn exited(pid:u64) {{let mut s=STATE.lock();s.locks.retain(|l|l.pid!=pid);s.waiting.retain(|w|w.request.pid!=pid);}changed();}
struct WaitCleanup(u64);
impl Drop for WaitCleanup {fn drop(&mut self){STATE.lock().waiting.retain(|w|w.tid!=self.0);}}
fn decode(file:&dyn FileOps, inode:u64, pid:u64, bytes:&[u8])->Result<Record,i64> {
    let kind=u16::from_le_bytes(bytes[0..2].try_into().unwrap());if kind>2{return Err(-22);}
    let whence=i16::from_le_bytes(bytes[2..4].try_into().unwrap());
    let start=i64::from_le_bytes(bytes[8..16].try_into().unwrap()) as i128;
    let len=i64::from_le_bytes(bytes[16..24].try_into().unwrap()) as i128;
    let base=match whence {0=>0,1=>{let p=file.seek(0,1);if p<0{return Err(p);}p as i128},2=>file.stat().1 as i128,_=>return Err(-22)};
    let start=base+start;if start<0{return Err(-22);}if start>i64::MAX as i128{return Err(-75);}
    let (lo,hi)=if len==0 {(start as u64,EOF)} else if len<0 {
        if start+len<0{return Err(-22);}((start+len) as u64,start as u64)
    } else {
        let end=start+len;if end>i64::MAX as i128+1{return Err(-75);}
        (start as u64,if end==i64::MAX as i128+1 {EOF}else{end as u64})
    };
    Ok(Record {inode,pid,lo,hi,kind})
}
pub fn fcntl(fd:i32,file:Arc<dyn FileOps>,command:u64,pointer:u64)->i64 {
    let mut bytes=[0u8;32];
    match usercopy::slice(pointer,32) {Ok(b)=>bytes.copy_from_slice(b),Err(e)=>return e}
    let Some(inode)=file.flock_inode() else{return -95;};
    let pid=process::current_pid();
    let request=match decode(&*file,inode,pid,&bytes){Ok(r)=>r,Err(e)=>return e};
    if command==5 {
        if request.kind==2{return -22;}
        let found=STATE.lock().locks.iter().find(|l|request.conflicts(**l)).copied();
        if let Some(l)=found {
            bytes[0..2].copy_from_slice(&l.kind.to_le_bytes());bytes[2..4].copy_from_slice(&0i16.to_le_bytes());
            bytes[8..16].copy_from_slice(&(l.lo as i64).to_le_bytes());
            bytes[16..24].copy_from_slice(&(if l.hi==EOF {0}else{(l.hi-l.lo) as i64}).to_le_bytes());
            bytes[24..28].copy_from_slice(&(l.pid as i32).to_le_bytes());
        } else {bytes[0..2].copy_from_slice(&2u16.to_le_bytes());}
        return match usercopy::slice_mut(pointer,32){Ok(b)=>{b.copy_from_slice(&bytes);0},Err(e)=>e};
    }
    if (request.kind==0&&file.access_mode()==1)||(request.kind==1&&file.access_mode()==0){return -9;}
    let tid=process::posix_tid();let _cleanup=WaitCleanup(tid);
    loop {
        // Lock order: fd table -> STATE; STATE never touches fd tables or does I/O.
        // A concurrent close/replacement cannot strand a new lock on a closed fd.
        let generation=GENERATION.load(Ordering::Acquire);
        let result=process::with_verified_fd(fd,&file,||{
            let mut s=STATE.lock();
            if request.kind!=2&&s.locks.iter().any(|l|request.conflicts(*l)) {
                if command==7 {
                    if let Some(w)=s.waiting.iter_mut().find(|w|w.tid==tid){w.request=request;}
                    else {if s.waiting.try_reserve(1).is_err(){return -37;}s.waiting.push(Waiting {tid,request});}
                    match s.cycle(request){Ok(true)=>return -35,Err(e)=>return e,_=>{}}
                }
                return -11;
            }
            s.waiting.retain(|w|w.tid!=tid);s.replace(request)
        }).unwrap_or(-9);
        if result==0 {changed();}
        if result != -11 || command!=7 {return result;}
        if crate::signal::interrupted(){return -4;}
        CHANGED.wait_if_intr(||GENERATION.load(Ordering::Acquire)==generation);
    }
}
