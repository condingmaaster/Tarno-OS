// SPDX-License-Identifier: GPL-2.0-or-later
//! Advisory ext2 whole-file locks, owned by open descriptions (Arc identity).
use alloc::sync::{Arc, Weak};
use alloc::vec::Vec;
use core::sync::atomic::{AtomicU64,Ordering};
use crate::{file::FileOps,wait::WaitQueue};
use spin::Mutex;
struct Lock {inode:u64,owner:Weak<dyn FileOps>,exclusive:bool}
static LOCKS:Mutex<Vec<Lock>>=Mutex::new(Vec::new());
static GENERATION:AtomicU64=AtomicU64::new(0);
static CHANGED:WaitQueue=WaitQueue::new();
/// Called after the last Arc becomes weak-only, including references held by
/// mappings/sections rather than descriptors. Never retains a file reference.
pub fn notify_closed(){GENERATION.fetch_add(1,Ordering::AcqRel);CHANGED.wake_all_no_poll();}
fn attempt(inode:u64,owner:&Weak<dyn FileOps>,mode:u32)->(i64,bool){
    let mut locks=LOCKS.lock();let before=locks.len();locks.retain(|l|l.owner.strong_count()!=0);let mut changed=before!=locks.len();
    let current=locks.iter().position(|l|Weak::ptr_eq(&l.owner,owner));
    if mode==8 {if let Some(i)=current{locks.swap_remove(i);changed=true;}return(0,changed);}
    if let Some(i)=current {
        if locks[i].exclusive==(mode==2){return(0,changed);}
        // BSD conversion may release the previous lock before waiting.
        locks.swap_remove(i);changed=true;
    }
    if locks.iter().any(|l|l.inode==inode&&(mode==2||l.exclusive)){return(-11,changed);}
    if locks.try_reserve(1).is_err(){return(-37,changed);}
    locks.push(Lock{inode,owner:owner.clone(),exclusive:mode==2});(0,true)
}
pub fn apply(file:Arc<dyn FileOps>,operation:u32)->i64 {
    let mode=operation&!4;if !matches!(mode,1|2|8){return -22;}
    let Some(inode)=file.flock_inode()else{return -95;};let owner=Arc::downgrade(&file);
    loop {
        let generation=GENERATION.load(Ordering::Acquire);
        let(result,changed)=attempt(inode,&owner,mode);if changed{notify_closed();}
        if result != -11 || operation&4!=0{return result;}
        if crate::signal::interrupted(){return -4;}
        // Atomic generation predicate avoids both missed closes and lock-order
        // inversion; no descriptor or lock-table mutex is taken by the queue.
        CHANGED.wait_if_intr(||GENERATION.load(Ordering::Acquire)==generation);
    }
}
