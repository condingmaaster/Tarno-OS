// SPDX-License-Identifier: GPL-2.0-or-later
//! A mutex for operations that may sleep on device I/O. Contenders park rather
//! than spinning with interrupts disabled and starving the owner's completion.
use core::cell::UnsafeCell;
use core::marker::PhantomData;
use core::ops::{Deref,DerefMut};
use core::sync::atomic::{AtomicBool,Ordering};
use crate::wait::WaitQueue;

pub struct IoMutex<T> {
    held: AtomicBool,
    queue: WaitQueue,
    data: UnsafeCell<T>,
}
unsafe impl<T:Send> Sync for IoMutex<T> {}
impl<T> IoMutex<T> {
    pub const fn new(data:T)->Self {
        Self {held:AtomicBool::new(false),queue:WaitQueue::new(),data:UnsafeCell::new(data)}
    }
    pub fn try_lock(&self)->Option<IoMutexGuard<'_,T>> {
        self.held.compare_exchange(false,true,Ordering::Acquire,Ordering::Relaxed).ok()?;
        Some(IoMutexGuard {mutex:self,_thread_local:PhantomData})
    }
    pub fn lock(&self)->IoMutexGuard<'_,T> {
        loop {
            if let Some(g)=self.try_lock(){return g;}
            if !crate::sched::is_started(){core::hint::spin_loop();continue;}
            self.queue.wait_if(||self.held.load(Ordering::Acquire));
        }
    }
}
pub struct IoMutexGuard<'a,T> {mutex:&'a IoMutex<T>,_thread_local:PhantomData<*mut ()>}
impl<T> Deref for IoMutexGuard<'_,T> {
    type Target=T;
    fn deref(&self)->&T {unsafe {&*self.mutex.data.get()}}
}
impl<T> DerefMut for IoMutexGuard<'_,T> {
    fn deref_mut(&mut self)->&mut T {unsafe {&mut *self.mutex.data.get()}}
}
impl<T> Drop for IoMutexGuard<'_,T> {
    fn drop(&mut self) {
        self.mutex.held.store(false,Ordering::Release);
        self.mutex.queue.wake_all_no_poll();
    }
}
