from pathlib import Path
import subprocess,struct,hashlib
source=Path('target/thos-lock-wait-qa-2026-10-16.img');image=Path('target/thos-module-qa-2026-10-16.img');root=Path('target/module-root-2026-10-16.ext2')
subprocess.run(['cp','--sparse=always','--reflink=auto',str(source),str(image)],check=True)
with image.open('rb') as f,root.open('wb') as r:
 f.seek(470);start,sectors=struct.unpack('<II',f.read(8));start*=512;size=sectors*512
 assert start and start+size<=image.stat().st_size
 f.seek(start);left=size
 while left:
  b=f.read(min(left,262144));assert b
  if any(b):r.write(b)
  else:r.seek(len(b),1)
  left-=len(b)
 r.truncate(size)
for cmd in ['rm /Windows/System32/kernel32x.dll','write target/kernel32x-module-2026-10-16.dll /Windows/System32/kernel32x.dll']:
 subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],check=True)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
with image.open('r+b') as f,root.open('rb') as r:
 f.seek(start)
 while b:=r.read(262144):
  old=f.read(len(b));assert len(old)==len(b)
  if b!=old:f.seek(-len(b),1);f.write(b)
 f.seek(start);h=hashlib.sha256();left=size
 while left:
  b=f.read(min(left,1048576));assert b;h.update(b);left-=len(b)
 assert h.digest()==hashlib.file_digest(root.open('rb'),'sha256').digest()
kernel=Path('target/thos-kernel-module-2026-10-16.elf')
subprocess.run(['cp','target/x86_64-unknown-none/release/thos-kernel',str(kernel)],check=True)
subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(kernel),'::boot/thos-kernel'],check=True)
print('QA candidate ready, Live unchanged')
