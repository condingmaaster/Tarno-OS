// SPDX-License-Identifier: GPL-2.0-or-later
//! An NT-style configuration registry, grown to persisted hives.
//!
//! One global key tree of typed values, addressed by a `\`-separated path
//! (`\Registry\Machine\Software\...`). Backs `NtCreateKey`, `NtOpenKey`,
//! `NtSetValueKey`, `NtQueryValueKey`, `NtDeleteKey`, `NtEnumerateKey` and
//! `NtEnumerateValueKey`.
//!
//! Each top-level root in [`HIVES`] is a **hive**: its own backing file under
//! `/etc/thos/registry/`, loaded once at boot ([`load_hives`]) and rewritten
//! on every mutation under it ([`create`] / [`set_value`] / [`delete_key`]) —
//! serialized staged hive replacement: reported filesystem failures leave the
//! live tree and notifications unchanged. Every mutation writes its affected
//! hive; no periodic worker. This is not a journal, a physical-drive flush
//! guarantee or recovery from lower layers that fail without reporting it.
//!
//! **Change-notify** (real, not a stub): [`watch`] registers a one-shot
//! signal on an already-created `Event` object, fired the next time a given
//! key (or, with `watch_tree`, anything under it) actually changes —
//! `nt.rs`'s `NtNotifyChangeKey` is the asynchronous, Event-driven shape of
//! the real NT call; THOS already has real event/wait primitives
//! (`wait.rs`), so this is "wire the registry into them", not new blocking
//! machinery. One-shot, same as real NT: a fired watch needs a fresh
//! `NtNotifyChangeKey` to re-arm.
//!
//! **Per-key security** (real, not a stub): every key carries an
//! `owner_uid`, persisted in the hive right alongside its subkeys/values.
//! Seeded roots and anything loaded from an old (pre-owner) hive default to
//! uid `0` (system), matching `ext2::Inode`'s own "unset owner reads as
//! system" convention. Same two-tier model as the filesystem's first DAC
//! slice — uid `0` or the key's own owner may write, anyone may read — no
//! group tier here yet (the registry has no `gid` concept at all, a real,
//! scoped-out gap, same starting point the filesystem's DAC had before its
//! own group-tier slice). `nt.rs` is what actually enforces this on
//! `NtCreateKey`/`NtSetValueKey`/`NtDeleteKey`; this module just carries the
//! ownership data and the policy predicate ([`write_ok`]).

use alloc::collections::BTreeMap;
use alloc::string::{String, ToString};
use alloc::sync::Arc;
use alloc::vec::Vec;
use core::fmt::Write as _;
use core::sync::atomic::{AtomicBool, Ordering};
use spin::Mutex;

use crate::ext2::Ext2;
use crate::wait::Event;

/// One stored value: an `REG_*` type tag plus its raw bytes.
#[derive(Clone)]
pub struct Value {
    pub ty: u32,
    pub data: Vec<u8>,
}

#[derive(Clone)]
struct Key {
    subkeys: BTreeMap<String, Key>,
    values: BTreeMap<String, Value>,
    /// `0` (system) unless created by `create_owned` with a real uid, or
    /// loaded from a hive's own `O` record.
    owner_uid: u32,
}
impl Key {
    const fn new() -> Self {
        Self { subkeys: BTreeMap::new(), values: BTreeMap::new(), owner_uid: 0 }
    }
    fn owned(uid: u32) -> Self {
        Self { owner_uid: uid, ..Self::new() }
    }
}

/// The real DAC check: uid `0` (system) always passes; otherwise the
/// caller's uid must match the key's own owner. Read access (`open`,
/// `query_value`, both `enumerate_*`) is unchecked — anyone may read any
/// key, same as the registry's real-Windows HKLM subtrees are typically
/// world-readable, admin-write.
fn write_ok(owner_uid: u32, uid: u32) -> bool {
    uid == 0 || uid == owner_uid
}

/// Writers stage one private hive; readers see the old committed tree until
/// the filesystem accepts the replacement. Contenders sleep across device IO.
static WRITER:crate::io_mutex::IoMutex<()> = crate::io_mutex::IoMutex::new(());
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub enum Error {NotFound,Denied,Io,Unsupported,Resources}
static ROOT: Mutex<Key> = Mutex::new(Key::new());
static SEEDED: AtomicBool = AtomicBool::new(false);

/// A registered [`watch`]: `event` is signalled the next time `path`
/// changes (or, with `watch_tree`, anything under it) — removed from
/// [`WATCHES`] the moment it fires (one-shot).
struct WatchEntry {
    path: Vec<String>,
    watch_tree: bool,
    event: Arc<Event>,
}
static WATCHES: Mutex<Vec<WatchEntry>> = Mutex::new(Vec::new());

/// `NtNotifyChangeKey`'s backing call: signal `event` the next time `path`
/// changes. `false` if `path` doesn't currently exist (nothing to watch —
/// real NT requires an open handle to the key, which already implies it
/// exists).
pub fn watch(path: &str, watch_tree: bool, event: Arc<Event>) -> bool {
    let _writer=WRITER.lock();
    let comps = components(path);
    if !run(|root| find(root, &comps).is_some()) {
        return false;
    }
    WATCHES.lock().push(WatchEntry { path: comps, watch_tree, event });
    true
}

/// Does a watch on `watch_path` (optionally covering its whole subtree)
/// cover a change that happened at `changed`? A change *on* the watched key
/// itself always counts (a value changed on it, or a direct subkey
/// appeared/disappeared under it); anything deeper needs `watch_tree`.
fn watch_matches(watch_path: &[String], watch_tree: bool, changed: &[String]) -> bool {
    if changed.len() < watch_path.len() || changed[..watch_path.len()] != *watch_path {
        return false;
    }
    let depth = changed.len() - watch_path.len();
    depth == 0 || depth == 1 || watch_tree
}

/// Fire (and drop — one-shot) every watch that covers a real change at
/// `changed`.
fn fire(changed: &[String]) {
    let mut list = WATCHES.lock();
    let mut i = 0;
    while i < list.len() {
        if watch_matches(&list[i].path, list[i].watch_tree, changed) {
            let w = list.remove(i);
            w.event.signal();
        } else {
            i += 1;
        }
    }
}

/// Split a path into normalised components (lowercased, non-empty). A leading
/// `registry` element is dropped, so `\Registry\Machine` and `Machine` name the
/// same key.
fn components(path: &str) -> Vec<String> {
    let mut c: Vec<String> = path
        .split(|ch| ch == '\\' || ch == '/')
        .filter(|s| !s.is_empty())
        .map(|s| s.to_ascii_lowercase())
        .collect();
    if c.first().map(|s| s.as_str() == "registry").unwrap_or(false) {
        c.remove(0);
    }
    c
}

/// The canonical `\`-joined form of `path` — what a key HANDLE stores.
pub fn canon(path: &str) -> String {
    components(path).join("\\")
}

/// Lock the tree, seeding the standard hive roots on first use.
fn run<R>(f: impl FnOnce(&mut Key) -> R) -> R {
    let mut root = ROOT.lock();
    if !SEEDED.swap(true, Ordering::Relaxed) {
        for h in ["machine", "user", "machine\\software", "machine\\system"] {
            make(&mut root, &components(h), 0);
        }
    }
    f(&mut root)
}

/// Find-or-create the key at `comps`. `uid` is the owner stamped on any key
/// *newly* created along the way (an already-existing key's owner is left
/// alone) — every implicitly created ancestor gets the same owner as the
/// leaf, matching real Windows `RegCreateKeyEx`'s own ancestor-creation
/// behavior.
fn make<'a>(root: &'a mut Key, comps: &[String], uid: u32) -> &'a mut Key {
    let mut k = root;
    for c in comps {
        k = k.subkeys.entry(c.clone()).or_insert_with(|| Key::owned(uid));
    }
    k
}
fn find<'a>(root: &'a Key, comps: &[String]) -> Option<&'a Key> {
    let mut k = root;
    for c in comps {
        k = k.subkeys.get(c)?;
    }
    Some(k)
}
fn find_mut<'a>(root: &'a mut Key, comps: &[String]) -> Option<&'a mut Key> {
    let mut k = root;
    for c in comps {
        k = k.subkeys.get_mut(c)?;
    }
    Some(k)
}

/// Create `path` (and any missing ancestors), system-owned (uid `0`).
pub fn create(path: &str) -> bool {
    create_owned(path, 0)
}

/// Create `path` (and any missing ancestors) — newly created keys along the
/// way are owned by `uid`; an already-existing key keeps its owner
/// unchanged. Returns false on unsupported paths or reported persistence
/// failures, without publishing a staged mutation.
pub fn create_owned(path: &str, uid: u32) -> bool {
    create_inner(path,uid,false).is_ok()
}
/// Caller permissions are checked inside the same serialized transaction as
/// the mutation, so delete/recreate cannot race a detached permission check.
pub fn create_for_user(path:&str,uid:u32)->Result<bool,Error>{create_inner(path,uid,true)}
fn create_inner(path:&str,uid:u32,enforce:bool)->Result<bool,Error>{
    change(path,Change::Create,|root,relative|{
        if find(root,relative).is_some(){return Ok(false);}
        if enforce{
            let mut ancestor=relative;
            while find(root,ancestor).is_none(){ancestor=&ancestor[..ancestor.len()-1];}
            if !write_ok(find(root,ancestor).unwrap().owner_uid,uid){return Err(Error::Denied);}
        }
        make(root,relative,uid);Ok(true)
    })
}

/// The owner uid of `path`'s key, if it exists.
pub fn owner_of(path: &str) -> Option<u32> {
    let comps = components(path);
    run(|root| find(root, &comps).map(|k| k.owner_uid))
}

/// The write-permission check for *creating something new* at `path`: the
/// registry auto-creates missing ancestors (unlike the filesystem, there is
/// no single guaranteed-existing parent), so this walks up from `path` to
/// the nearest ancestor that already exists and checks that ancestor's
/// owner — generalizing the filesystem's "creating an entry is a write to
/// the parent it lands in" rule. Always terminates: the root itself
/// (`comps == []`) always "exists" (the whole in-memory tree), system-owned
/// by default.
pub fn create_write_ok(path: &str, uid: u32) -> bool {
    let mut comps = components(path);
    loop {
        if let Some(owner) = run(|root| find(root, &comps).map(|k| k.owner_uid)) {
            return write_ok(owner, uid);
        }
        comps.pop();
    }
}

/// Can `uid` write to (set a value on, or delete) the already-existing key
/// at `path`? `false` if the key doesn't exist at all.
pub fn write_key_ok(path: &str, uid: u32) -> bool {
    owner_of(path).is_some_and(|owner| write_ok(owner, uid))
}

/// `true` if `path` names an existing key.
pub fn open(path: &str) -> bool {
    let comps = components(path);
    run(|root| find(root, &comps).is_some())
}

/// The name of the `index`-th direct subkey of `path` (natural — sorted —
/// order, stable as long as the key set doesn't change mid-enumeration).
/// `None` once `index` runs past the last one, for `NtEnumerateKey`.
pub fn enumerate_key(path: &str, index: usize) -> Option<String> {
    let comps = components(path);
    run(|root| find(root, &comps)?.subkeys.keys().nth(index).cloned())
}

/// The `(name, type, byte length)` of the `index`-th value directly on
/// `path`. `None` past the last one, for `NtEnumerateValueKey`.
pub fn enumerate_value(path: &str, index: usize) -> Option<(String, u32, usize)> {
    let comps = components(path);
    run(|root| {
        find(root, &comps)?
            .values
            .iter()
            .nth(index)
            .map(|(n, v)| (n.clone(), v.ty, v.data.len()))
    })
}

/// Set (or replace) a value on an existing key. `false` if the key is missing.
pub fn set_value(path:&str,name:&str,ty:u32,data:&[u8])->bool {
    set_value_for_user(path,name,ty,data,0).is_ok()
}
pub fn set_value_for_user(path:&str,name:&str,ty:u32,data:&[u8],uid:u32)->Result<(),Error>{
    if data.len()>MAX_VALUE_BYTES{return Err(Error::Resources);}
    change(path,Change::Value,|root,relative|{
        let key=find_mut(root,relative).ok_or(Error::NotFound)?;
        if !write_ok(key.owner_uid,uid){return Err(Error::Denied);}
        let mut name=copy_string(name)?;name.make_ascii_lowercase();
        let mut bytes=Vec::new();bytes.try_reserve_exact(data.len()).map_err(|_|Error::Resources)?;bytes.extend_from_slice(data);
        key.values.insert(name,Value{ty,data:bytes});Ok(true)
    }).map(|_|())
}

/// Read a value back as `(type, bytes)`.
pub fn query_value(path: &str, name: &str) -> Option<(u32, Vec<u8>)> {
    let comps = components(path);
    let name = name.to_ascii_lowercase();
    run(|root| {
        let k = find(root, &comps)?;
        let v = k.values.get(&name)?;
        Some((v.ty, v.data.clone()))
    })
}

/// Remove a leaf key from its parent. `false` if the path is empty, the parent
/// is missing, or the key does not exist.
pub fn delete_key(path:&str)->bool {delete_key_for_user(path,0).is_ok()}
pub fn delete_key_for_user(path:&str,uid:u32)->Result<(),Error>{
    change(path,Change::Delete,|root,relative|{
        let key=find(root,relative).ok_or(Error::NotFound)?;
        if !write_ok(key.owner_uid,uid)||relative.is_empty(){return Err(Error::Denied);}
        let(parent,leaf)=relative.split_at(relative.len()-1);
        find_mut(root,parent).ok_or(Error::NotFound)?.subkeys.remove(&leaf[0]).ok_or(Error::NotFound)?;
        Ok(true)
    }).map(|_|())
}

// --- hives: persistence to ext2 ---------------------------------------

/// Top-level hive roots and their backing file. A path under one of these
/// gets rewritten to disk on every mutation; a path outside all of them
/// (there is none today — every seeded root is hived) just stays in memory.
const HIVES: &[(&[&str], &str)] = &[
    (&["machine", "software"], "/etc/thos/registry/software.hiv"),
    (&["machine", "system"], "/etc/thos/registry/system.hiv"),
    (&["user"], "/etc/thos/registry/user.hiv"),
];

fn under_hive(comps: &[String], root: &[&str]) -> bool {
    comps.len() >= root.len() && comps.iter().zip(root.iter()).all(|(a, b)| a == b)
}

/// Stage only the affected hive while holding a sleepable writer lock. No
/// live mutation/notification is published on a reported filesystem failure;
/// concurrent readers keep seeing the previous committed tree during IO.
/// This is operational error rollback, NOT a journal or power-loss guarantee.
#[derive(Clone,Copy)]enum Change {Create,Value,Delete}
fn change(path:&str,kind:Change,operation:impl FnOnce(&mut Key,&[String])->Result<bool,Error>)->Result<bool,Error>{
    let comps=components(path);
    let _writer=WRITER.lock();
    let(hive,file)=HIVES.iter().find(|(root,_)|under_hive(&comps,root)).ok_or(Error::Unsupported)?;
    let root_comps:Vec<String>=hive.iter().map(|s|s.to_string()).collect();
    if matches!(kind,Change::Create)&&run(|r|find(r,&comps).is_some()){return Ok(false);}
    // Reject before constructing a deep staged tree (including its recursive
    // drop), not only later while serializing that temporary tree.
    if comps.len()-hive.len()>MAX_DEPTH{return Err(Error::Resources);}
    let mut staged=run(|r|find(r,&root_comps).map(|h|{measure(h)?;copy_key(h)})).ok_or(Error::NotFound)??;
    if !operation(&mut staged,&comps[hive.len()..])?{return Ok(false);}
    let bytes=serialize_hive(&staged)?;
    persist_hive(file,&bytes)?;
    run(|root|*find_mut(root,&root_comps).expect("seeded hive under serialized writer")=staged);
    fire(if matches!(kind,Change::Delete){&comps[..comps.len()-1]}else{&comps});
    Ok(true)
}
fn persist_hive(path:&str,bytes:&[u8])->Result<(),Error>{
    let fs=crate::ext2::open().map_err(|_|Error::Io)?;
    for directory in ["/etc","/etc/thos","/etc/thos/registry"]{
        if let Some(ino)=fs.path_lookup_nofollow(directory){
            if fs.read_inode(ino).mode&0xf000!=0x4000{return Err(Error::Io);}
        }else{fs.mkdir_path(directory).map_err(|_|Error::Io)?;}
    }
    if let Some(ino)=fs.path_lookup_nofollow(path){
        if fs.read_inode(ino).mode&0xf000!=0x8000{return Err(Error::Io);}
    }
    fs.write_path(path,bytes).map_err(|_|Error::Io)
}

/// Load every hive's backing file (if present — first boot has none) into the
/// in-memory tree. Call once, after ext2 is mounted, before anything else
/// touches the registry (the seeded defaults are harmless to overwrite: a
/// loaded hive fully replaces its root's subtree).
static HIVES_LOADED: AtomicBool = AtomicBool::new(false);

/// Returns how many of [`HIVES`] had a backing file to load (0 on first boot).
pub fn load_hives(fs: &Ext2) -> usize {
    if HIVES_LOADED.swap(true, Ordering::Relaxed) {
        return 0;
    }
    let mut n = 0;
    for (root, path) in HIVES {
        let Some(bytes) = fs.read_path(path) else { continue };
        n += 1;
        let root_comps: Vec<String> = root.iter().map(|s| s.to_string()).collect();
        run(|r| {
            let base = make(r, &root_comps, 0);
            *base = Key::new(); // the file is authoritative — drop the seed
            deserialize_hive(base, &bytes);
        });
    }
    n
}

/// `thos-hive v1` text format: one record per line, all string fields
/// hex-encoded so any byte (a name or data from a hostile PE) round-trips
/// with no escaping rules to get wrong.
///   K <hex relpath>                        -- a subkey exists
///   O <hex relpath> <uid>                  -- that key's owner (omitted if 0)
///   V <hex relpath> <hex name> <type> <hex data>  -- a value on that key
/// `relpath` is `\`-joined, relative to the hive root (empty = the root
/// itself); re-parsed with the same `components()` as every other path here.
/// `O` records are new (per-key security) — an old hive with none loads
/// every key as uid `0`, exactly its prior behavior, so this is backward
/// compatible without a format-version bump.
// A bounded prototype hive profile. Existing larger hives are still readable;
// mutations fail with resources rather than cloning/serializing without bound.
pub const MAX_VALUE_BYTES:usize=1024*1024;
pub const MAX_HIVE_BYTES:usize=4*1024*1024;
const MAX_KEYS:usize=4096;
const MAX_VALUES:usize=16384;
// Recursive tree copying/formatting shares a16KiB kernel stack.
const MAX_DEPTH:usize=32;
fn copy_string(s:&str)->Result<String,Error>{let mut out=String::new();out.try_reserve_exact(s.len()).map_err(|_|Error::Resources)?;out.push_str(s);Ok(out)}
/// Postorder copy with an explicit, fallibly allocated work stack. Recursive
/// copies used about448 bytes per hierarchy level in the optimized kernel;
/// a32-level tree exhausted the16KiB syscall stack before its next allocation.
fn copy_key(key:&Key)->Result<Key,Error>{
    struct Frame<'a>{
        children:alloc::collections::btree_map::Iter<'a,String,Key>,
        out:Key,
        name:Option<String>,
    }
    impl<'a> Frame<'a>{
        fn new(key:&'a Key,name:Option<String>)->Result<Self,Error>{
            let mut out=Key::owned(key.owner_uid);
            for(name,value)in &key.values{
                let mut data=Vec::new();
                data.try_reserve_exact(value.data.len()).map_err(|_|Error::Resources)?;
                data.extend_from_slice(&value.data);
                out.values.insert(copy_string(name)?,Value{ty:value.ty,data});
            }
            Ok(Self{children:key.subkeys.iter(),out,name})
        }
    }
    let mut stack=Vec::new();
    stack.try_reserve_exact(MAX_DEPTH+1).map_err(|_|Error::Resources)?;
    stack.push(Frame::new(key,None)?);
    loop{
        let child=stack.last_mut().unwrap().children.next();
        if let Some((name,key))=child{
            if stack.len()>=MAX_DEPTH+1{return Err(Error::Resources);}
            stack.push(Frame::new(key,Some(copy_string(name)?))?);
        }else{
            let Frame{out,name,..}=stack.pop().unwrap();
            if let Some(parent)=stack.last_mut(){parent.out.subkeys.insert(name.unwrap(),out);}
            else{return Ok(out);}
        }
    }
}
struct Budget{bytes:usize,keys:usize,values:usize}
impl Budget{fn add(&mut self,n:usize)->Result<(),Error>{self.bytes=self.bytes.checked_add(n).filter(|n|*n<=MAX_HIVE_BYTES).ok_or(Error::Resources)?;Ok(())}}
fn hex_length(n:usize)->Result<usize,Error>{n.checked_mul(2).map(|n|n.max(1)).ok_or(Error::Resources)}
fn digits(mut n:u32)->usize{let mut count=1;while n>=10{n/=10;count+=1;}count}
fn measure(base:&Key)->Result<Budget,Error>{
    fn walk(k:&Key,prefix:usize,depth:usize,out:&mut Budget)->Result<(),Error>{
        out.keys+=1;if depth>MAX_DEPTH||out.keys>MAX_KEYS{return Err(Error::Resources);}
        if k.owner_uid!=0{out.add(4+hex_length(prefix)?+digits(k.owner_uid))?;}
        for(name,value)in &k.values{
            out.values+=1;if out.values>MAX_VALUES||value.data.len()>MAX_VALUE_BYTES{return Err(Error::Resources);}
            out.add(6+hex_length(prefix)?+hex_length(name.len())?+digits(value.ty)+hex_length(value.data.len())?)?;
        }
        for(name,child)in &k.subkeys{
            let length=prefix.checked_add(usize::from(prefix!=0)).and_then(|n|n.checked_add(name.len())).ok_or(Error::Resources)?;
            out.add(3+hex_length(length)?)?;walk(child,length,depth+1,out)?;
        }
        Ok(())
    }
    let mut out=Budget{bytes:"thos-hive v1\n".len(),keys:0,values:0};walk(base,0,0,&mut out)?;Ok(out)
}
struct HiveOutput{out:String}
impl core::fmt::Write for HiveOutput{
    fn write_str(&mut self,s:&str)->core::fmt::Result{
        if self.out.len().checked_add(s.len()).filter(|n|*n<=MAX_HIVE_BYTES).is_none(){return Err(core::fmt::Error);}
        self.out.try_reserve(s.len()).map_err(|_|core::fmt::Error)?;self.out.push_str(s);Ok(())
    }
}
struct Hex<'a>(&'a[u8]);
impl core::fmt::Display for Hex<'_>{
    fn fmt(&self,f:&mut core::fmt::Formatter<'_>)->core::fmt::Result{
        const TABLE:&[u8;16]=b"0123456789abcdef";
        if self.0.is_empty(){return f.write_str("-");}
        for chunk in self.0.chunks(64){let mut text=[0u8;128];for(i,&b)in chunk.iter().enumerate(){text[i*2]=TABLE[(b>>4)as usize];text[i*2+1]=TABLE[(b&15)as usize];}f.write_str(core::str::from_utf8(&text[..chunk.len()*2]).unwrap())?;}
        Ok(())
    }
}
fn serialize_hive(base:&Key)->Result<Vec<u8>,Error>{
    fn walk(k:&Key,prefix:&str,out:&mut HiveOutput)->Result<(),Error>{
        if k.owner_uid!=0{writeln!(out,"O {} {}",Hex(prefix.as_bytes()),k.owner_uid).map_err(|_|Error::Resources)?;}
        for(name,value)in &k.values{writeln!(out,"V {} {} {} {}",Hex(prefix.as_bytes()),Hex(name.as_bytes()),value.ty,Hex(&value.data)).map_err(|_|Error::Resources)?;}
        for(name,child)in &k.subkeys{
            let n=prefix.len()+usize::from(!prefix.is_empty())+name.len();let mut path=String::new();path.try_reserve_exact(n).map_err(|_|Error::Resources)?;
            path.push_str(prefix);if !prefix.is_empty(){path.push('\\');}path.push_str(name);
            writeln!(out,"K {}",Hex(path.as_bytes())).map_err(|_|Error::Resources)?;walk(child,&path,out)?;
        }
        Ok(())
    }
    let budget=measure(base)?;let mut out=HiveOutput{out:String::new()};out.out.try_reserve_exact(budget.bytes).map_err(|_|Error::Resources)?;
    out.write_str("thos-hive v1\n").map_err(|_|Error::Resources)?;walk(base,"",&mut out)?;
    if out.out.len()!=budget.bytes{return Err(Error::Resources);}
    Ok(out.out.into_bytes())
}

fn deserialize_hive(base: &mut Key, bytes: &[u8]) {
    let Ok(text) = core::str::from_utf8(bytes) else { return };
    for line in text.lines() {
        let mut it = line.split(' ');
        match it.next() {
            Some("K") => {
                let Some(rp) = it.next().and_then(unhex_string) else { continue };
                make(base, &components(&rp), 0);
            }
            Some("O") => {
                let (Some(rp), Some(uid)) =
                    (it.next().and_then(unhex_string), it.next().and_then(|s| s.parse::<u32>().ok()))
                else {
                    continue;
                };
                // The owning key always has (or will have) its own "K" line
                // too — order between the two doesn't matter, `make` is
                // idempotent and this always sets the final owner explicitly.
                make(base, &components(&rp), 0).owner_uid = uid;
            }
            Some("V") => {
                let (Some(rp), Some(name), Some(ty), Some(data)) = (
                    it.next().and_then(unhex_string),
                    it.next().and_then(unhex_string),
                    it.next().and_then(|s| s.parse::<u32>().ok()),
                    it.next().and_then(unhex_bytes),
                ) else {
                    continue;
                };
                let k = make(base, &components(&rp), 0);
                k.values.insert(name.to_ascii_lowercase(), Value { ty, data });
            }
            _ => {} // blank line, the "thos-hive v1" header, or garbage — skip
        }
    }
}

fn unhex_bytes(s: &str) -> Option<Vec<u8>> {
    if s == "-" {
        return Some(Vec::new());
    }
    if s.len() % 2 != 0 {
        return None;
    }
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).ok())
        .collect()
}

fn unhex_string(s: &str) -> Option<String> {
    String::from_utf8(unhex_bytes(s)?).ok()
}
