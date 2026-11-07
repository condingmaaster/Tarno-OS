from pathlib import Path
import subprocess,struct,hashlib
image=Path('target/thos-asus-unicode-qa-2026-10-16.img');root=Path('target/asus-unicode-root-2026-10-16.ext2')
for a,b in [('target/thos-unicode-qa-2026-10-16.img',image),('target/module-root-2026-10-16.ext2',root)]:subprocess.run(['cp','--sparse=always','--reflink=auto',str(a),str(b)],check=True)
for a,b in [('target/vendor-tests/asus/ArmouryCrateInstaller_3.3.6.0/ArmouryCrateInstaller.exe','/ArmouryCrateInstaller.exe'),('xtask/testdata/asus-installer-probe.sh','/asus-installer-probe.sh')]:
 subprocess.run(['/usr/sbin/debugfs','-w','-R',f'write {a} {b}',str(root)],check=True)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
with image.open('r+b') as f,root.open('rb') as r:
 f.seek(470);start,sectors=struct.unpack('<II',f.read(8));start*=512;size=sectors*512
 assert start and size==root.stat().st_size and start+size<=image.stat().st_size
 f.seek(start)
 while b:=r.read(262144):
  old=f.read(len(b));assert len(old)==len(b)
  if old!=b:f.seek(-len(b),1);f.write(b)
 f.seek(start);h=hashlib.sha256();left=size
 while left:
  b=f.read(min(left,1048576));assert b;h.update(b);left-=len(b)
 assert h.digest()==hashlib.file_digest(root.open('rb'),'sha256').digest()
print('ASUS offline QA ready')
