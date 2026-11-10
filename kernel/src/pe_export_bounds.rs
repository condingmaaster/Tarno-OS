// SPDX-License-Identifier: GPL-2.0-or-later
//! Checked RVA ranges for runtime PE exports. Does not dereference user memory.
#[derive(Clone,Copy,Debug)]
pub struct Image { pub base:u64,pub size:u64 }
impl Image {
 pub fn new(base:u64,size:u64)->Option<Self>{
  if base==0||size<120||size>256*1024*1024||base.checked_add(size)?>0x0000_8000_0000_0000{return None;}Some(Self{base,size})
 }
 pub fn span(self,rva:u64,bytes:u64)->Option<u64>{
  if rva==0||rva.checked_add(bytes)?>self.size{return None;}self.base.checked_add(rva)
 }
 pub fn table(self,rva:u64,count:u64,stride:u64)->Option<u64>{
  if count>65536{return None;}self.span(rva,count.checked_mul(stride)?)
 }
 pub fn string_limit(self,rva:u64,end:u64)->Option<usize>{
  if end>self.size||rva==0||rva>=end{return None;}Some((end-rva).min(4096) as usize)
 }
}
#[cfg(test)]mod tests{
 use super::*;
 #[test]fn rejects_wrapping_and_kernel_images(){assert!(Image::new(u64::MAX-8,4096).is_none());assert!(Image::new(0x0000_7fff_ffff_f000,8192).is_none());assert!(Image::new(0,4096).is_none());assert!(Image::new(4096,256*1024*1024+1).is_none());}
 #[test]fn validates_full_tables_and_string_bounds(){let i=Image::new(0x10000,4096).unwrap();assert_eq!(i.table(4092,1,4),Some(0x10ffc));assert_eq!(i.table(4092,2,4),None);assert_eq!(i.table(100,0xffffffff,4),None);assert_eq!(i.span(u64::MAX,4),None);assert_eq!(i.string_limit(4095,4096),Some(1));assert_eq!(i.string_limit(4096,4096),None);assert_eq!(i.span(0,4),None);}
}
