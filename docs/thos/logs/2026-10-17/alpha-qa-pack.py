from pathlib import Path
import subprocess,struct,hashlib,json,tarfile
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
kernel=Path('target/thos-kernel-alpha-2026-10-17.elf');subprocess.run(['cp','target/x86_64-unknown-none/release/thos-kernel',str(kernel)],check=True)
image=Path('target/thos-alpha-qa-2026-10-17.img');subprocess.run(['cp','--sparse=always','--reflink=auto','target/thos-dcbrush-qa-2026-10-17.img',str(image)],check=True)
root=Path('target/alpha-qa-root-2026-10-17.ext2')
with image.open('rb') as f,root.open('wb') as r:
 f.seek(470);start,sectors=struct.unpack('<II',f.read(8));start*=512;size=sectors*512;assert start and size and start+size<=image.stat().st_size
 f.seek(start);left=size
 while left:
  b=f.read(min(left,262144));assert b
  if any(b):r.write(b)
  else:r.seek(len(b),1)
  left-=len(b)
 r.truncate(size)
for cmd in ['rm /Windows/System32/msimg32.dll','write target/msimg32-alpha-2026-10-17.dll /Windows/System32/msimg32.dll']:
 subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],capture_output=True,check=True)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
subprocess.run(['/usr/sbin/debugfs','-R','dump /Windows/System32/msimg32.dll /tmp/thos-alpha-qa-msimg32.dll',str(root)],capture_output=True,check=True)
assert sha('/tmp/thos-alpha-qa-msimg32.dll')==sha('target/msimg32-alpha-2026-10-17.dll')
with image.open('r+b') as f,root.open('rb') as r:
 f.seek(start)
 while b:=r.read(262144):
  old=f.read(len(b));assert len(old)==len(b)
  if old!=b:f.seek(-len(b),1);f.write(b)
 f.seek(start);h=hashlib.sha256();left=size
 while left:
  b=f.read(min(left,1048576));assert b;h.update(b);left-=len(b)
 assert h.hexdigest()==sha(root)
subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(kernel),'::boot/thos-kernel'],check=True)
p=subprocess.Popen(['mtype','-i',f'{image}@@1048576','::boot/thos-kernel'],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(kernel)
paths=list(Path('kernel').rglob('*.rs'))+[Path('xtask/src/main.rs'),Path('xtask/msvcrtx/msimg32.c'),Path('xtask/testdata/gdi-alpha.c'),Path('xtask/testdata/desk-gdi-alpha.sh')]
with tarfile.open('target/thos-alpha-source-2026-10-17.tar.gz','w:gz') as out:
 for path in paths:out.add(path,arcname=str(path))
Path('target/thos-alpha-source-2026-10-17.json').write_text(json.dumps({str(p):sha(p) for p in paths},indent=2)+'\n')
print('Alpha QA kernel/root/DLL verified',sha(kernel),sha('target/msimg32-alpha-2026-10-17.dll'))
