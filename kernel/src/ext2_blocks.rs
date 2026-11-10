// SPDX-License-Identifier: GPL-2.0-or-later
//! Walk all three ext2 indirect levels, preserving sparse subtrees.
extern crate alloc;
use alloc::vec::Vec;

pub fn append_indirect<F: Fn(u32)->Vec<u8>>(block:u32,depth:u8,per:usize,total:usize,out:&mut Vec<u32>,read:&F) {
    if out.len()>=total {return;}
    if depth==0 {out.push(block);return;}
    if block==0 {
        let count=per.saturating_pow(depth as u32).min(total-out.len());
        out.resize(out.len()+count,0);return;
    }
    for bytes in read(block).chunks_exact(4).take(per) {
        if out.len()>=total {break;}
        append_indirect(u32::from_le_bytes(bytes.try_into().unwrap()),depth-1,per,total,out,read);
    }
}

/// Build one indirect subtree without exposing it before its inode is committed.
pub fn build_indirect<F:FnMut(Vec<u8>)->Option<u32>>(blocks:&[u32],depth:u8,per:usize,next:&mut usize,emit:&mut F)->Option<u32> {
    if *next>=blocks.len(){return Some(0);}
    if depth==0 {let b=blocks[*next];*next+=1;return Some(b);}
    let mut bytes=alloc::vec![0u8;per*4];let mut present=false;
    for i in 0..per {
        if *next>=blocks.len(){break;}
        let b=build_indirect(blocks,depth-1,per,next,emit)?;
        bytes[i*4..i*4+4].copy_from_slice(&b.to_le_bytes());present|=b!=0;
    }
    if present {emit(bytes)}else{Some(0)}
}

/// Copy only changed metadata paths. Old nodes remain intact until inode commit;
/// `retired` contains replaced metadata, never shared children or data blocks.
#[cfg(test)]
pub fn rewrite_indirect<R:Fn(u32)->Vec<u8>,E:FnMut(Vec<u8>)->Option<u32>>(
    old:u32,blocks:&[u32],depth:u8,per:usize,read:&R,emit:&mut E,retired:&mut Vec<u32>
)->Option<(u32,u32)> {
    if depth==0 {return Some((blocks.first().copied().unwrap_or(0),0));}
    if old==0&&blocks.iter().all(|&b|b==0) {return Some((0,0));}
    let previous=if old==0 {alloc::vec![0u8;per*4]} else {read(old)};
    let mut bytes=alloc::vec![0u8;per*4];let mut children=0;let mut present=false;
    let span=per.pow(depth as u32-1);
    for i in 0..per {
        let child=u32::from_le_bytes(previous[i*4..i*4+4].try_into().unwrap());
        let start=(i*span).min(blocks.len());let end=((i+1)*span).min(blocks.len());
        let (b,meta)=rewrite_indirect(child,&blocks[start..end],depth-1,per,read,emit,retired)?;
        bytes[i*4..i*4+4].copy_from_slice(&b.to_le_bytes());children+=meta;present|=b!=0;
    }
    if bytes==previous {return Some((old,children+u32::from(old!=0)));}
    let new=if present {emit(bytes)?} else {0};
    if old!=0 {retired.push(old);}
    Some((new,children+u32::from(new!=0)))
}

/// Replace only paths intersecting newly allocated data pointers. The caller
/// already knows the old metadata count, so untouched subtrees need no reads.
pub fn patch_indirect<R:Fn(u32)->Vec<u8>,E:FnMut(Vec<u8>)->Option<u32>>(
    old:u32,blocks:&[u32],depth:u8,per:usize,base:usize,first:usize,end:usize,
    read:&R,emit:&mut E,retired:&mut Vec<u32>
)->Option<u32> {
    if first>=end||base>=end||base+per.pow(depth as u32)<=first {return Some(old);}
    if depth==0 {return Some(blocks.first().copied().unwrap_or(0));}
    let previous=if old==0 {alloc::vec![0u8;per*4]} else {read(old)};
    let mut bytes=previous.clone();let span=per.pow(depth as u32-1);
    for i in 0..per {
        let child_base=base+i*span;
        if child_base>=end||child_base+span<=first {continue;}
        let child=u32::from_le_bytes(previous[i*4..i*4+4].try_into().unwrap());
        let lo=(i*span).min(blocks.len());let hi=((i+1)*span).min(blocks.len());
        let b=patch_indirect(child,&blocks[lo..hi],depth-1,per,child_base,first,end,read,emit,retired)?;
        bytes[i*4..i*4+4].copy_from_slice(&b.to_le_bytes());
    }
    if bytes==previous {return Some(old);}
    let new=if bytes.chunks_exact(4).any(|b|b!=[0,0,0,0]) {emit(bytes)?} else {0};
    if old!=0 {retired.push(old);}Some(new)
}

/// Release children before metadata; metadata-only walks preserve reused data.
pub fn visit_indirect<F:Fn(u32)->Vec<u8>,V:FnMut(u32)>(block:u32,depth:u8,include_data:bool,read:&F,visit:&mut V) {
    if block==0{return;}
    if depth==0 {if include_data {visit(block);}return;}
    for bytes in read(block).chunks_exact(4) {
        visit_indirect(u32::from_le_bytes(bytes.try_into().unwrap()),depth-1,include_data,read,visit);
    }
    visit(block);
}

#[cfg(test)] mod tests {
    use super::*;
    #[test] fn three_levels_and_sparse_branches() {
        let read=|b|match b {1=>alloc::vec![2,0],2=>alloc::vec![3,4],3=>alloc::vec![51,52],4=>alloc::vec![53,54],_=>panic!("unexpected pointer")}.into_iter().flat_map(u32::to_le_bytes).collect();
        let mut out=Vec::new();append_indirect(1,3,2,8,&mut out,&read);
        assert_eq!(out,alloc::vec![51,52,53,54,0,0,0,0]);
    }
    #[test] fn partial_tail_stops_before_extra_metadata_read() {
        let read=|b|{assert_eq!(b,1);alloc::vec![7,8].into_iter().flat_map(u32::to_le_bytes).collect()};
        let mut out=alloc::vec![5];append_indirect(1,1,2,2,&mut out,&read);
        assert_eq!(out,alloc::vec![5,7]);
    }
    #[test] fn full_sized_triple_hole_is_bounded_by_file_size() {
        let mut out=alloc::vec![12];append_indirect(0,3,256,70000,&mut out,&|_|panic!("hole must not read disk"));
        assert_eq!(out.len(),70000);assert_eq!(out[0],12);assert!(out[1..].iter().all(|&b|b==0));
    }
    #[test] fn build_roundtrip_and_free_metadata_only() {
        use std::collections::BTreeMap;
        let blocks=alloc::vec![7,0,9,10,0,0,11];let mut store=BTreeMap::new();let mut id=100;
        let mut next=0;let root=build_indirect(&blocks,3,2,&mut next,&mut |bytes|{id+=1;store.insert(id,bytes);Some(id)}).unwrap();
        let read=|b|store[&b].clone();let mut out=Vec::new();append_indirect(root,3,2,blocks.len(),&mut out,&read);assert_eq!(out,blocks);
        let mut freed=Vec::new();visit_indirect(root,3,false,&read,&mut |b|freed.push(b));
        assert_eq!(freed.len(),store.len());assert!(freed.iter().all(|b|store.contains_key(b)));assert_eq!(freed.last(),Some(&root));
        let mut all=Vec::new();visit_indirect(root,3,true,&read,&mut |b|all.push(b));
        assert_eq!(all.len(),store.len()+4);assert!(alloc::vec![7,9,10,11].iter().all(|b|all.contains(b)));
    }
    #[test] fn sparse_build_needs_no_metadata_and_failure_propagates() {
        let mut next=0;assert_eq!(build_indirect(&alloc::vec![0;8],3,2,&mut next,&mut |_|panic!("empty subtree")),Some(0));assert_eq!(next,8);
        let mut next=0;assert_eq!(build_indirect(&[1,2,3],2,2,&mut next,&mut |_|None),None);
    }
    #[test] fn changed_path_reuses_other_metadata_and_data() {
        use std::{cell::RefCell,collections::BTreeMap};
        let store=RefCell::new(BTreeMap::new());let mut id=100;
        let original=alloc::vec![11,12,13,14,15,16,17,18];let mut next=0;
        let root=build_indirect(&original,3,2,&mut next,&mut |bytes|{id+=1;store.borrow_mut().insert(id,bytes);Some(id)}).unwrap();
        let read=|b|store.borrow()[&b].clone();let mut retired=Vec::new();let before=id;
        let (same,meta)=rewrite_indirect(root,&original,3,2,&read,&mut |_|panic!("unchanged tree allocated"),&mut retired).unwrap();
        assert_eq!((same,meta),(root,7));assert!(retired.is_empty());
        let mut changed=original.clone();changed[6]=99;
        let (new,meta)=rewrite_indirect(root,&changed,3,2,&read,&mut |bytes|{id+=1;store.borrow_mut().insert(id,bytes);Some(id)},&mut retired).unwrap();
        assert_eq!(id-before,3);assert_eq!(meta,7);assert_eq!(retired.len(),3);
        let mut out=Vec::new();append_indirect(new,3,2,8,&mut out,&read);assert_eq!(out,changed);
        let mut old=Vec::new();append_indirect(root,3,2,8,&mut old,&read);assert_eq!(old,original);
        let mut live=Vec::new();visit_indirect(new,3,false,&read,&mut |b|live.push(b));assert!(retired.iter().all(|b|!live.contains(b)));
        assert!(retired.iter().all(|b|!original.contains(b)));
    }
    #[test] fn rewritten_tree_shrinks_and_reports_failure_without_changing_old_nodes() {
        use std::{cell::RefCell,collections::BTreeMap};
        let store=RefCell::new(BTreeMap::new());let mut id=100;let mut next=0;
        let root=build_indirect(&[11,12,13,14],2,2,&mut next,&mut |bytes|{id+=1;store.borrow_mut().insert(id,bytes);Some(id)}).unwrap();
        let read=|b|store.borrow()[&b].clone();let mut retired=Vec::new();
        assert_eq!(rewrite_indirect(root,&[99],2,2,&read,&mut |_|None,&mut retired),None);
        let mut out=Vec::new();append_indirect(root,2,2,4,&mut out,&read);assert_eq!(out,alloc::vec![11,12,13,14]);
        retired.clear();let (new,meta)=rewrite_indirect(root,&[11],2,2,&read,&mut |bytes|{id+=1;store.borrow_mut().insert(id,bytes);Some(id)},&mut retired).unwrap();
        assert_eq!(meta,2);assert_eq!(retired.len(),3);let mut out=Vec::new();append_indirect(new,2,2,4,&mut out,&read);assert_eq!(out,alloc::vec![11,0,0,0]);
    }

    #[test] fn repeated_growth_holes_and_shrink_retire_exactly_unreferenced_nodes() {
        use std::{cell::RefCell,collections::BTreeMap};
        let store=RefCell::new(BTreeMap::<u32,Vec<u8>>::new());let mut id=100;let mut root=0;let mut seed=7u32;
        let read=|b|store.borrow()[&b].clone();
        for step in 0..160 {
            seed=seed.wrapping_mul(1664525).wrapping_add(1013904223);
            let len=if step%9==0 {0} else {1+(seed as usize%8)};
            let data:Vec<u32>=(0..len).map(|i|if (seed>>(i%8))&1==0 {0} else {11+i as u32}).collect();
            let mut before=Vec::new();visit_indirect(root,3,false,&read,&mut |b|before.push(b));
            let mut retired=Vec::new();let (new,meta)=rewrite_indirect(root,&data,3,2,&read,&mut |bytes|{id+=1;store.borrow_mut().insert(id,bytes);Some(id)},&mut retired).unwrap();
            let mut live=Vec::new();visit_indirect(new,3,false,&read,&mut |b|live.push(b));assert_eq!(meta as usize,live.len());
            before.retain(|b|!live.contains(b));before.sort();retired.sort();assert_eq!(retired,before);
            for b in retired {store.borrow_mut().remove(&b);}
            let mut result=Vec::new();append_indirect(new,3,2,8,&mut result,&read);let mut want=data.clone();want.resize(8,0);assert_eq!(result,want);
            assert_eq!(store.borrow().len(),live.len());root=new;
        }
    }

    #[test] fn single_patch_reads_only_its_path_and_keeps_other_branches() {
        use std::{cell::{Cell,RefCell},collections::BTreeMap};
        let store=RefCell::new(BTreeMap::<u32,Vec<u8>>::new());let mut id=100;let mut next=0;
        let data=alloc::vec![11,12,13,14,15,16,17,18];
        let root=build_indirect(&data,3,2,&mut next,&mut |bytes|{id+=1;store.borrow_mut().insert(id,bytes);Some(id)}).unwrap();
        let reads=Cell::new(0);let read=|b|{reads.set(reads.get()+1);store.borrow()[&b].clone()};
        let mut changed=data.clone();changed[6]=99;let mut retired=Vec::new();let before=id;
        let new=patch_indirect(root,&changed,3,2,0,6,7,&read,&mut |bytes|{id+=1;store.borrow_mut().insert(id,bytes);Some(id)},&mut retired).unwrap();
        assert_eq!(reads.get(),3);assert_eq!(id-before,3);assert_eq!(retired.len(),3);
        let mut out=Vec::new();append_indirect(new,3,2,8,&mut out,&read);assert_eq!(out,changed);
        reads.set(0);assert_eq!(patch_indirect(new,&changed,3,2,0,8,8,&read,&mut |_|panic!("no changed pointer"),&mut Vec::new()),Some(new));assert_eq!(reads.get(),0);
    }

}
