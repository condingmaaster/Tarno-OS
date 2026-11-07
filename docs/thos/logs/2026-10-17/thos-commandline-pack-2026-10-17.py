from pathlib import Path
import subprocess,struct,hashlib,json,tarfile
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
log=Path('docs/thos/logs/2026-10-17');log.mkdir(exist_ok=True,parents=True)
for source,dest in [('/tmp/thos-commandline-before-2026-10-17.log','commandline-before-runner.log'),('target/wargv-serial.log','commandline-before-serial.log')]:subprocess.run(['cp',source,str(log/dest)],check=True)
image=Path('target/thos-commandline-qa-2026-10-17.img');assert not image.exists()
subprocess.run(['cp','--sparse=always','--reflink=auto','target/thos-wide-qa-2026-10-17.img',str(image)],check=True)
root=Path('target/commandline-qa-root-2026-10-17.ext2');assert not root.exists()
with image.open('rb') as f,root.open('wb') as r:
 f.seek(470);start,sectors=struct.unpack('<II',f.read(8));start*=512;size=sectors*512;assert start and size and start+size<=image.stat().st_size;f.seek(start);left=size
 while left:
  b=f.read(min(left,262144));assert b
  if any(b):r.write(b)
  else:r.seek(len(b),1)
  left-=len(b)
 r.truncate(size)
local='target/kernel32x-commandline-2026-10-17.dll'
for cmd in ['rm /Windows/System32/kernel32x.dll',f'write {local} /Windows/System32/kernel32x.dll']:subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],check=True,capture_output=True)
subprocess.run(['/usr/sbin/debugfs','-R','dump /Windows/System32/kernel32x.dll /tmp/thos-commandline-readback.dll',str(root)],check=True,capture_output=True);assert sha('/tmp/thos-commandline-readback.dll')==sha(local)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
with image.open('r+b') as f,root.open('rb') as r:
 f.seek(start)
 while b:=r.read(262144):
  old=f.read(len(b));assert len(old)==len(b)
  if old!=b:f.seek(-len(b),1);f.write(b)
 f.seek(start);h=hashlib.sha256();left=size
 while left:
  b=f.read(min(left,1048576));assert b;h.update(b);left-=len(b)
 assert h.hexdigest()==sha(root)
print('QA created/readback verified',sha(local),sha(image))
