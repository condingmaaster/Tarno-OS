// SPDX-License-Identifier: GPL-2.0-or-later
// Process-owned runtime loader. First supported subset: entryless, TLS-free,
// leaf DLLs with no new dependencies. Unsupported initialisation fails explicitly (50).
struct RuntimeModule { meta:LdrFileMod, refs:u32, dynamic:bool, alive:bool }
pub(crate) struct RuntimeState { arena_next:u64, modules:Vec<RuntimeModule>, metadata_bytes:usize, search:u32, app_dir:String }
impl RuntimeState {
 fn new(arena_next:u64,mods:Vec<LdrFileMod>,exe_path:&str,metadata_bytes:usize)->Result<Self,&'static str>{
  // The PEB builder already validated and materialised this table. Carry its
  // actual mapped size forward instead of allocating/zeroing it a second time.
  let mut modules=Vec::new();modules.try_reserve_exact(mods.len()).map_err(|_|"PE: runtime registry allocation")?;
  for meta in mods{modules.push(RuntimeModule{meta,refs:1,dynamic:false,alive:true});}
  let dir=exe_path.rsplit_once('/').map_or("/",|(d,_)|if d.is_empty(){"/"}else{d});
  let mut app_dir=String::new();app_dir.try_reserve_exact(dir.len()).map_err(|_|"PE: runtime directory allocation")?;app_dir.push_str(dir);
  Ok(Self{arena_next,modules,metadata_bytes,search:crate::pe_search::APPLICATION|crate::pe_search::SYSTEM32,app_dir})
 }
 fn metadata(&self)->Result<Vec<u8>,u32>{
  let entries:Vec<_>=self.modules.iter().map(|m|crate::pe_ldr_layout::Module{name:&m.meta.name,full:&m.meta.full,base:m.meta.base,entry:m.meta.entry,size:m.meta.size}).collect();
  crate::pe_ldr_layout::build(PE_LDRDATA_ADDR,&entries).map_err(|_|8u32)
 }
 fn nodes(&self,list:u64)->Vec<u64>{
  let mut v=alloc::vec![PE_PARAMS_ADDR+LDR_OFF+0x10+list*0x10,PE_PARAMS_ADDR+MOD_OFF+list*0x10,PE_PARAMS_ADDR+MOD2_OFF+list*0x10,PE_PARAMS_ADDR+MOD3_OFF+list*0x10];
  for k in 0..3{v.push(PE_PARAMS_ADDR+EXTRA_SYNTH_OFF+k*0x80+list*0x10);}
  let mut files:Vec<_>=self.modules.iter().enumerate().filter(|(_,m)|m.alive).map(|(i,m)|(m.meta.base,i)).collect();files.sort_unstable_by_key(|&(base,_)|base);
  for(_,i)in files{v.push(PE_LDRDATA_ADDR+i as u64*LDR_ENTRY_STRIDE+list*0x10);}
  v
 }
 fn publish(&self,proc:&Process,data:&[u8])->Result<(),u32>{
  // Names stay at the same addresses for the whole process lifetime. Slots of
  // unloaded modules are retained, never shifted/reused for a different DLL.
  if !data.is_empty(){
   for i in 0..self.modules.len(){runtime_write(proc,PE_LDRDATA_ADDR+i as u64*LDR_ENTRY_STRIDE+0x30,&data[i*0x80+0x30..i*0x80+0x80])?;}
   runtime_write(proc,PE_LDRDATA_ADDR+(crate::pe_ldr_layout::MAX_MODULES*crate::pe_ldr_layout::STRIDE)as u64,&data[crate::pe_ldr_layout::MAX_MODULES*crate::pe_ldr_layout::STRIDE..])?;
  }
  // Initialise new nodes before linking the head/tail to them. Each aligned
  // pointer is published as one 64-bit store, not a bytewise list-link copy.
  for list in 0..3{let nodes=self.nodes(list);for p in (0..nodes.len()).rev(){let va=nodes[p];runtime_put64(proc,va,nodes[(p+1)%nodes.len()])?;runtime_put64(proc,va+8,nodes[(p+nodes.len()-1)%nodes.len()])?;}}
  Ok(())
 }
}
fn runtime_write(proc:&Process,mut va:u64,mut bytes:&[u8])->Result<(),u32>{while !bytes.is_empty(){let p=proc.translate(va).ok_or(487u32)?;let n=bytes.len().min(4096-(va as usize&4095));unsafe{core::ptr::copy_nonoverlapping(bytes.as_ptr(),phys_to_virt(PhysAddr::new(p)).as_mut_ptr::<u8>(),n);}va+=n as u64;bytes=&bytes[n..];}Ok(())}
fn runtime_put64(proc:&Process,va:u64,v:u64)->Result<(),u32>{let p=proc.translate(va).ok_or(487u32)?;if va&7!=0{return Err(487);}unsafe{(&*phys_to_virt(PhysAddr::new(p)).as_ptr::<core::sync::atomic::AtomicU64>()).store(v,core::sync::atomic::Ordering::Release);}Ok(())}
// All leaf image and extra metadata frames are owned until the mapping commit.
struct RuntimeFrames(Vec<(u64,x86_64::structures::paging::PhysFrame,bool)>);
impl Drop for RuntimeFrames {fn drop(&mut self){let mut a=FRAME_ALLOC.lock();for(_,f,_)in self.0.drain(..){a.dealloc(f);}}}
impl RuntimeFrames {
 fn add(&mut self,proc:&Process,va:u64,bytes:&[u8],exec:bool)->Result<(),u32>{
  if proc.translate(va).is_some(){return Err(487);}
  let f=FRAME_ALLOC.lock().alloc().ok_or(8u32)?;let dst=phys_to_virt(f.start_address()).as_mut_ptr::<u8>();unsafe{core::ptr::write_bytes(dst,0,4096);core::ptr::copy_nonoverlapping(bytes.as_ptr(),dst,bytes.len());}self.0.push((va,f,exec));Ok(())
 }
 fn commit(mut self,proc:&Process){for(va,f,x)in self.0.drain(..){proc.map(va,f.start_address().as_u64(),true,x);}}
}
fn runtime_disk_path(raw:&str)->Result<String,u32>{
 if raw.is_empty()||raw.len()>1024||raw.contains('\0'){return Err(87);}
 let p=raw.replace('\\',"/");
 let p=if p.len()>2&&p.as_bytes()[1]==b':'{if !p[..1].eq_ignore_ascii_case("c")||p.as_bytes()[2]!=b'/'{return Err(50);}String::from(&p[2..])}else if p.starts_with('/') {p}else if !p.contains('/') {format!("{SYSTEM32_DIR}/{}",normalize_dll_name(&p))}else{return Err(50);};
 if p.split('/').any(|c|c==".."||c==".")||p.contains(':'){return Err(87);}Ok(p)
}
fn runtime_read(fs:&crate::ext2::Ext2,path:&str)->Result<(String,Vec<u8>),u32>{
 let mut ino=2;let mut exact=String::new();
 for c in path.split('/').filter(|s|!s.is_empty()){
  let parent=fs.read_inode(ino);if parent.mode&0xf000!=0x4000{return Err(126);}if parent.size>2*1024*1024{return Err(8);}
  let entry=fs.read_dir(ino).into_iter().find(|(_,_,n)|n.eq_ignore_ascii_case(c)).ok_or(126u32)?;ino=entry.0;exact.push('/');exact.push_str(&entry.2);
 }
 let inode=fs.read_inode(ino);if inode.mode&0xf000!=0x8000{return Err(126);}if inode.size>16*1024*1024{return Err(8);}let bytes=fs.read_path(&exact).ok_or(126u32)?;Ok((exact,bytes))
}
pub(crate) fn runtime_load(proc:&Process,raw:&str,existing:u64,flags:u32)->Result<u64,u32>{
 let mut guard=proc.pe_runtime.lock();let state=guard.as_mut().ok_or(126u32)?;
 let path=runtime_disk_path(raw)?;let explicit=raw.contains('/')||raw.contains('\\')||raw.contains(':');
 let candidates=crate::pe_search::paths(state.search,flags,explicit,&path,&state.app_dir)?;
 // Respect the earliest still-loaded module, including synthetic modules.
 // Arena bases are monotonically allocated; retired slot reuse must not turn
 // an older slot into an earlier load in the module list/cache.
 if !explicit&&existing!=0{
  if let Some(m)=state.modules.iter_mut().find(|m|m.alive&&m.meta.base==existing){if m.dynamic{m.refs=m.refs.checked_add(1).ok_or(8u32)?;}}
  return Ok(existing);
 }
 if explicit{
  for(name,base)in[("kernel32.dll",PE_KERNEL32_ADDR),("ntdll.dll",PE_NTDLL_ADDR),("msvcrt.dll",PE_MSVCRT_ADDR),("user32.dll",PE_USER32_ADDR),("gdi32.dll",PE_GDI32_ADDR)]{
   if path.eq_ignore_ascii_case(&format!("{SYSTEM32_DIR}/{name}")){return Ok(base);}
  }
 }
 let cached=state.modules.iter().enumerate().filter(|(_,m)|m.alive&&if explicit{m.meta.full.replace('\\',"/").eq_ignore_ascii_case(&format!("C:{path}"))}else{m.meta.name.eq_ignore_ascii_case(&normalize_dll_name(raw))}).min_by_key(|(_,m)|m.meta.base).map(|(i,_)|i);
 if let Some(i)=cached{let m=&mut state.modules[i];if m.dynamic{m.refs=m.refs.checked_add(1).ok_or(8u32)?;}return Ok(m.meta.base);}
 let fs=crate::ext2::open().map_err(|_|126u32)?;let mut found=None;
 for candidate in candidates{match runtime_read(&fs,&candidate){Ok(v)=>{found=Some(v);break;},Err(126)=>{},Err(e)=>return Err(e)}}
 let(exact,bytes)=found.ok_or(126u32)?;
 let size=peek_size_of_image(&bytes).map_err(|_|193u32)?;if size>16*1024*1024{return Err(8);}
 let base=state.arena_next;let end=base.checked_add((size+65535)&!65535).filter(|e|*e<PE_DLL_ARENA_END).ok_or(8u32)?;
 let mut staged=stage_image(&bytes,Some(base)).map_err(|_|193u32)?;
 if staged.entry!=base||staged.tls.is_some(){return Err(50);}
 runtime_bind_imports(state,&mut staged.img,staged.import_rva,staged.import_size)?;
 let(eat,_,_)=parse_export_table(&staged.img,base,staged.export_rva,staged.export_size).map_err(|_|193u32)?;
 if eat.iter().any(|e|matches!(e,Export::Forward(_))){return Err(50);}
 let name=normalize_dll_name(exact.rsplit('/').next().ok_or(87u32)?);let full=format!("C:{}",exact.replace('/',"\\"));
 // Reuse only a retired slot for exactly the same name/path: all existing
 // string pointers stay stable, while the fresh arena base rejects stale handles.
 let retired=state.modules.iter().position(|m|!m.alive&&m.meta.name==name&&m.meta.full==full);
 if retired.is_none()&&state.modules.len()>=crate::pe_ldr_layout::MAX_MODULES{return Err(8);}
 let new=RuntimeModule{meta:LdrFileMod{base,entry:0,size,name,full},refs:1,dynamic:true,alive:true};
 let replaced=if let Some(i)=retired{Some((i,core::mem::replace(&mut state.modules[i],new)))}else{state.modules.try_reserve(1).map_err(|_|8u32)?;state.modules.push(new);None};
 // Any precommit failure restores the registry and frees all staged frames.
 let prepare=(||{
  let data=state.metadata()?;let count=(staged.img.len()+4095)/4096+(data.len().saturating_sub(state.metadata_bytes)+4095)/4096;
  let mut frames=RuntimeFrames(Vec::new());frames.0.try_reserve_exact(count).map_err(|_|8u32)?;
  if FRAME_ALLOC.lock().free_frames()<count as u64+128{return Err(8);}
  // Verify metadata pages are still present before modifying any module state.
  for at in (0..state.metadata_bytes).step_by(4096){if proc.translate(PE_LDRDATA_ADDR+at as u64).is_none(){return Err(487);}}
  if proc.translate(PE_PARAMS_ADDR).is_none(){return Err(487);}
  for(at,b)in staged.img.chunks(4096).enumerate(){let rva=at as u64*4096;let x=staged.segs.iter().any(|&(s,n,x)|x&&s<rva+4096&&s+n>rva);frames.add(proc,base+rva,b,x)?;}
  for at in (state.metadata_bytes..data.len()).step_by(4096){frames.add(proc,PE_LDRDATA_ADDR+at as u64,&data[at..(at+4096).min(data.len())],false)?;}
  Ok((data,frames))
 })();
 let(data,frames)=match prepare{Ok(v)=>v,Err(e)=>{if let Some((i,old))=replaced{state.modules[i]=old;}else{state.modules.pop();}return Err(e);}};
 frames.commit(proc);
 if let Err(e)=state.publish(proc,&data){if let Some((i,old))=replaced{state.modules[i]=old;}else{state.modules.pop();}proc.munmap(base,size);if data.len()>state.metadata_bytes{proc.munmap(PE_LDRDATA_ADDR+state.metadata_bytes as u64,(data.len()-state.metadata_bytes)as u64);}return Err(e);}
 state.metadata_bytes=data.len();state.arena_next=end;Ok(base)
}
pub(crate) fn runtime_free(proc:&Process,base:u64)->Result<(),u32>{
 let mut guard=proc.pe_runtime.lock();let state=guard.as_mut().ok_or(6u32)?;
 let Some(i)=state.modules.iter().position(|m|m.alive&&m.meta.base==base)else{return if [PE_KERNEL32_ADDR,PE_NTDLL_ADDR,PE_MSVCRT_ADDR,PE_USER32_ADDR,PE_GDI32_ADDR].contains(&base){Ok(())}else{Err(6)};};
 if !state.modules[i].dynamic{return Ok(());}if state.modules[i].refs>1{state.modules[i].refs-=1;return Ok(());}
 let data=state.metadata()?;let size=state.modules[i].meta.size;state.modules[i].alive=false;
 if let Err(e)=state.publish(proc,&data){state.modules[i].alive=true;return Err(e);}
 proc.munmap(base,size);Ok(())
}

pub(crate) fn runtime_set_policy(proc:&Process,flags:u32)->Result<(),u32>{let flags=crate::pe_search::default_flags(flags)?;let mut guard=proc.pe_runtime.lock();let state=guard.as_mut().ok_or(87u32)?;state.search=flags;Ok(())}

// No new dependency graph or DllMain is implied here. Only existing pinned
// modules and final target addresses are eligible; dynamic dependencies fail.
fn runtime_pinned_base(state:&RuntimeState,name:&str)->Option<u64>{
 let name=normalize_dll_name(name);
 match name.as_str(){"kernel32.dll"=>Some(PE_KERNEL32_ADDR),"ntdll.dll"=>Some(PE_NTDLL_ADDR),"msvcrt.dll"=>Some(PE_MSVCRT_ADDR),"user32.dll"=>Some(PE_USER32_ADDR),"gdi32.dll"=>Some(PE_GDI32_ADDR),_=>state.modules.iter().find(|m|m.alive&&!m.dynamic&&m.meta.name==name).map(|m|m.meta.base)}
}
fn runtime_pinned_target(state:&RuntimeState,addr:u64)->bool{
 [PE_KERNEL32_ADDR,PE_NTDLL_ADDR,PE_MSVCRT_ADDR,PE_USER32_ADDR,PE_GDI32_ADDR].iter().any(|b|addr>=*b&&addr<*b+4096)||state.modules.iter().any(|m|m.alive&&!m.dynamic&&addr>=m.meta.base&&addr<m.meta.base+m.meta.size)
}
fn runtime_bind_imports(state:&RuntimeState,img:&mut[u8],rva:u32,size:u32)->Result<(),u32>{
 if size==0{return Ok(());}
 let mut at=rva as usize;let end=at.checked_add(size as usize).filter(|e|*e<=img.len()).ok_or(193u32)?;if at==0{return Err(193);}
 let mut budget=65536usize;
 for _ in 0..64{
  if at.checked_add(20).is_none_or(|e|e>end){return Err(193);}
  let ilt=rd_u32(img,at).map_err(|_|193u32)?as usize;let name=rd_u32(img,at+12).map_err(|_|193u32)?;let iat=rd_u32(img,at+16).map_err(|_|193u32)?as usize;
  if name==0&&iat==0{return if img[at..at+20].iter().all(|b|*b==0){Ok(())}else{Err(193)};}
  if name==0||iat==0{return Err(193);}let dll=cstr_at(img,name as u64).map_err(|_|193u32)?;let module=runtime_pinned_base(state,dll).ok_or(126u32)?;
  let mut source=if ilt==0{iat}else{ilt};let mut target=iat;
  loop{
   if budget==0{return Err(193);}budget-=1;let thunk=rd_u64(img,source).map_err(|_|193u32)?;if thunk==0{break;}
   let address=if thunk&0x8000_0000_0000_0000!=0{
    if thunk&0x7fff_ffff_ffff_0000!=0{return Err(193);}crate::nt::export_by_ordinal(module,thunk as u16,0)
   }else{if thunk>u32::MAX as u64{return Err(193);}let name=cstr_at(img,thunk.checked_add(2).ok_or(193u32)?).map_err(|_|193u32)?;crate::nt::export_by_name(module,name,0)};
   if address==0{return Err(127);}if !runtime_pinned_target(state,address as u64){return Err(50);}
   img.get_mut(target..target.checked_add(8).ok_or(193u32)?).ok_or(193u32)?.copy_from_slice(&(address as u64).to_le_bytes());
   source=source.checked_add(8).ok_or(193u32)?;target=target.checked_add(8).ok_or(193u32)?;
  }
  at+=20;
 }
 Err(193)
}
