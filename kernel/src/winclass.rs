// SPDX-License-Identifier: GPL-2.0-or-later
//! Process-local Win32 classes and real class atoms. No callbacks here.
extern crate alloc;
use alloc::{collections::BTreeMap,string::String};
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub struct Class { pub atom:u16, pub wndproc:u64, pub wide:bool, pub instance:u64 }
struct Owner { next:u32, modules:BTreeMap<u64,BTreeMap<String,Class>>, count:usize }
pub struct Registry { owners:BTreeMap<u64,Owner>, active:usize }
impl Registry {
    pub const fn new()->Self { Self{owners:BTreeMap::new(),active:0} }
    pub fn register(&mut self,pid:u64,instance:u64,mut name:String,wndproc:u64,wide:bool)->Result<u16,u32>{
        if name.is_empty()||name.contains('\0')||name.encode_utf16().count()>256||wndproc==0{return Err(87);}
        if self.by_name(pid,instance,&name).is_some(){return Err(1410);}
        if self.active>=8192{return Err(8);}
        // ASCII class names compare without case. Non-ASCII text is preserved;
        // a complete Windows Unicode upcase table is not provided here.
        name.make_ascii_lowercase();
        let owner=self.owners.entry(pid).or_insert_with(||Owner{next:1,modules:BTreeMap::new(),count:0});
        if owner.count>=1024||owner.next>u16::MAX as u32{return Err(8);}
        let atom=owner.next as u16;owner.next+=1;
        owner.modules.entry(instance).or_default().insert(name,Class{atom,wndproc,wide,instance});owner.count+=1;self.active+=1;Ok(atom)
    }
    pub fn by_name(&self,pid:u64,instance:u64,name:&str)->Option<Class>{self.owners.get(&pid)?.modules.get(&instance)?.get(&name.to_ascii_lowercase()).copied()}
    pub fn by_atom(&self,pid:u64,atom:u16)->Option<Class>{if atom==0{return None;}self.owners.get(&pid)?.modules.values().flat_map(|m|m.values()).find(|c|c.atom==atom).copied()}
    pub fn cleanup(&mut self,pid:u64){if let Some(o)=self.owners.remove(&pid){self.active-=o.count;}}
}
#[cfg(test)] mod tests {
    use super::*;
    #[test] fn real_atoms_and_process_namespaces(){
        let mut r=Registry::new();let a=r.register(10,0,String::from("Größe 😀"),123,true).unwrap();
        let b=r.register(10,0,String::from("second"),456,false).unwrap();assert_ne!(a,b);
        assert_eq!(r.by_name(10,0,"SECOND"),r.by_atom(10,b));
        assert_eq!(r.register(10,0,String::from("SECOND"),999,true),Err(1410));
        assert_eq!(r.by_name(10,0,"Größe 😀"),r.by_atom(10,a));assert_eq!(r.by_atom(10,b).unwrap().wide,false);
        assert!(r.by_name(11,0,"Größe 😀").is_none());assert!(r.by_atom(11,a).is_none());
        r.register(11,0,String::from("Größe 😀"),789,false).unwrap();assert_eq!(r.by_name(11,0,"Größe 😀").unwrap().wndproc,789);
        assert_eq!(r.register(10,0,String::from("Größe 😀"),999,false),Err(1410));assert_eq!(r.by_atom(10,a).unwrap().wndproc,123);
        r.register(10,0x1000,String::from("Größe 😀"),456,true).unwrap();assert_eq!(r.by_name(10,0x1000,"Größe 😀").unwrap().wndproc,456);
        r.cleanup(10);assert!(r.by_atom(10,a).is_none());assert!(r.by_name(11,0,"Größe 😀").is_some());
    }
    #[test] fn counted_utf16_names_and_bounded_resources(){
        let mut r=Registry::new();assert!(r.register(1,0,"😀".repeat(128),1,true).is_ok());
        assert_eq!(r.register(1,0,"😀".repeat(129),1,true),Err(87));
        assert_eq!(r.register(1,0,String::from("bad\0name"),1,true),Err(87));
        assert_eq!(r.register(1,0,String::from("empty-proc"),0,true),Err(87));
        assert!(r.by_atom(1,0).is_none());
        for n in 1..1024 {r.register(1,0,alloc::format!("c{n}"),1,false).unwrap();}
        assert_eq!(r.register(1,0,String::from("overflow"),1,false),Err(8));
        r.cleanup(1);assert_eq!(r.active,0);assert!(r.register(2,0,String::from("fresh"),2,true).is_ok());
    }
}
