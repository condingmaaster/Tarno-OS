from pathlib import Path
import subprocess,hashlib,json,tarfile,os,struct
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
assert os.statvfs('.').f_bavail*os.statvfs('.').f_frsize>12*2**30
seed=Path('target/thos-format-qa-2026-10-24.img');kernel=Path('target/thos-kernel-snapshot-thread-2026-10-24.elf');image=Path('target/thos-snapshot-thread-qa-2026-10-24.img');dll=Path('target/kernel32x-snapshot-2026-10-24.dll')
assert not kernel.exists() and not image.exists()
subprocess.run(['cp','target/x86_64-unknown-none/release/thos-kernel',str(kernel)],check=True)
subprocess.run(['cp','--sparse=always','--reflink=auto',str(seed),str(image)],check=True)
subprocess.run(['mcopy','-o','-i',str(image)+'@@1048576',str(kernel),'::boot/thos-kernel'],check=True)
p=subprocess.Popen(['mtype','-i',str(image)+'@@1048576','::boot/thos-kernel'],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(kernel)
root=Path('target/snapshot-thread-root-scratch.ext2');assert not root.exists()
with image.open('r+b')as f:
 f.seek(470);start,size=struct.unpack('<II',f.read(8));start*=512;size*=512;assert start>0 and size>0 and start+size<=image.stat().st_size;f.seek(start)
 with root.open('wb')as out:
  left=size
  while left:
   b=f.read(min(left,1024*1024));assert b;left-=len(b)
   if b.strip(b'\0'):out.write(b)
   else:out.seek(len(b),1)
  out.truncate(size)
 for cmd in ['rm /Windows/System32/kernel32x.dll',f'write {dll} /Windows/System32/kernel32x.dll']:
  subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],check=True,capture_output=True)
 readback=Path('/tmp/thos-locale-dll-readback-2026-10-24.dll');readback.unlink(missing_ok=True)
 subprocess.run(['/usr/sbin/debugfs','-R',f'dump /Windows/System32/kernel32x.dll {readback}',str(root)],check=True,capture_output=True);assert sha(readback)==sha(dll)
 subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
 root_hash=sha(root);f.seek(start)
 with root.open('rb')as source:
  while b:=source.read(1024*1024):f.write(b)
 f.flush();os.fsync(f.fileno());f.seek(start);check=hashlib.sha256();left=size
 while left:
  b=f.read(min(left,1024*1024));assert b;left-=len(b);check.update(b)
 assert check.hexdigest()==root_hash
root.unlink()
paths=list(Path('kernel/src').glob('*.rs'))+[Path('kernel/Cargo.toml'),Path('kernel/build.rs'),Path('Cargo.toml'),Path('Cargo.lock'),Path('.cargo/config.toml')]+list(Path('kernel/font').glob('*'))+[Path('xtask/src/main.rs'),Path('xtask/src/test_scratch.rs'),Path('tools/qemu-case.sh')]+[p for p in Path('xtask/msvcrtx').iterdir()if p.suffix in ['.c','.h','.def']]+[p for p in Path('xtask/testdata').iterdir()if p.suffix in ['.c','.h','.def','.sh']]
a=Path('target/thos-snapshot-thread-source-2026-10-24.tar.gz')
with tarfile.open(a,'w:gz')as t:
 for p in paths:t.add(p,arcname=str(p))
d={'status':'PRIVATE QA only; PRIVATE QA only: native process snapshot budget, corrected PE thread cleanup/publication; pending PE proof','kernel':str(kernel),'image':str(image),'kernel32x':str(dll),'source_archive':str(a),'seed':str(seed),'seed_sha256':sha(seed),'root_partition_sha256':root_hash,'sources':{str(p):sha(p)for p in paths},'source_scope':'Kernel/font/build and xtask/C runtime/fixture sources. Boot and other userland inherited from hashed seed.','notes':'Native immutable process snapshots and genuine process/thread/name fields; unavailable Windows console-session sentinel (no WTS manager claimed); actual file types and close upper-bit guard. Real time picture bridge with locale defaults; all four TIME flags supported; Gregorian date short/long/custom only; alternate calendar/era/layout unavailable. GetLocalTime provider currently UTC; panel has separate CET/CEST environment. Live unchanged.'}
for k in ['kernel','image','kernel32x','source_archive']:d[k+'_sha256']=sha(d[k])
Path('target/thos-snapshot-thread-qa-2026-10-24-manifest.json').write_text(json.dumps(d,indent=2)+'\n');print('Kernel',d['kernel_sha256'],'K32x',d['kernel32x_sha256'])
