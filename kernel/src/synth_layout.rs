// SPDX-License-Identifier: GPL-2.0-or-later
//! Plan the entire synthetic DLL page before allocating or writing it.
#[derive(Clone, Copy, Debug)]
pub struct Layout {
    pub directory: usize, pub functions: usize, pub names: usize,
    pub ordinals: usize, pub dll_name: usize, pub strings: usize, pub end: usize,
}
pub fn plan(dll: &str, exports: &[&str], stubs: usize, stride: usize, minimum: usize, bytes: usize) -> Option<Layout> {
    if dll.is_empty() || dll.contains('\0') || exports.is_empty() || exports.len()>u16::MAX as usize || stride<11 { return None; }
    let stub_end=stubs.checked_add(exports.len().checked_mul(stride)?)?;
    let directory=minimum.max(stub_end.checked_add(255)? & !255);
    let functions=directory.checked_add(40)?;
    let names=functions.checked_add(exports.len().checked_mul(4)?)?;
    let ordinals=names.checked_add(exports.len().checked_mul(4)?)?;
    let dll_name=ordinals.checked_add(exports.len().checked_mul(2)?)?;
    let strings=dll_name.checked_add(dll.len().checked_add(1)?)?;
    let mut end=strings;
    for name in exports {
        if name.is_empty() || name.contains('\0') { return None; }
        end=end.checked_add(name.len().checked_add(1)?)?;
    }
    if stubs<0x180 || directory<stub_end || end>bytes { return None; }
    Some(Layout{directory,functions,names,ordinals,dll_name,strings,end})
}
#[cfg(test)] mod tests {
    use super::*;
    #[test] fn ntdll_43_entries_cannot_overlap_export_metadata() {
        let names=["NtExample";43];let p=plan("NTDLL.DLL",&names,0x180,16,0x400,4096).unwrap();
        assert_eq!(p.directory,0x500);
        for i in 0..43 { assert!(0x180+i*16+11<=p.directory); }
        assert!(p.functions>=p.directory+40&&p.names>=p.functions+43*4&&p.ordinals>=p.names+43*4&&p.dll_name>=p.ordinals+43*2&&p.strings>=p.dll_name+10);
    }
    #[test] fn old_smaller_modules_keep_stub_addresses_and_directory() {
        for count in [21,30,31,35,40] { let names=["Export";40];let p=plan("a.dll",&names[..count],0x180,16,0x400,4096).unwrap();assert_eq!(p.directory,0x400); }
    }
    #[test] fn bounds_are_checked_before_writing_any_page() {
        let names=["A comparatively long export function";120];
        assert!(plan("a.dll",&names,0x180,16,0x400,4096).is_none());
        for stub in [usize::MAX,usize::MAX-100] { assert!(plan("a.dll",&["x"],stub,16,0x400,4096).is_none()); }
        assert!(plan("a.dll",&["x\0bad"],0x180,16,0x400,4096).is_none());
        assert!(plan("a.dll",&["x"],0x180,8,0x400,4096).is_none());
        for n in 1..120 { let names=["Export";120]; if let Some(p)=plan("a.dll",&names[..n],0x180,16,0x400,4096) { assert!(p.directory>=0x180+n*16&&p.end<=4096); } }
    }
}
