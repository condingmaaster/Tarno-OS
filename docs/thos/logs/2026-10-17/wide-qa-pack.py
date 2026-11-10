from pathlib import Path
import subprocess,struct,hashlib,json,tarfile
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
image=Path('target/thos-wide-qa-2026-10-17.img');subprocess.run(['cp','--sparse=always','--reflink=auto','target/thos-pen-qa-2026-10-17.img',str(image)],check=True)
root=Path('target/wide-qa-root-2026-10-17.ext2')
with image.open('rb') as f,root.open('wb') as r:
 f.seek(470);start,sectors=struct.unpack('<II',f.read(8));start*=512;size=sectors*512;assert start and size and start+size<=image.stat().st_size;f.seek(start);left=size
 while left:
  b=f.read(min(left,262144));assert b
  if any(b):r.write(b)
  else:r.seek(len(b),1)
  left-=len(b)
 r.truncate(size)
for local,name in [('target/msvcrtx-wide-2026-10-17.dll','msvcrtx.dll'),('target/kernel32x-leadbyte-2026-10-17.dll','kernel32x.dll')]:
 for cmd in [f'rm /Windows/System32/{name}',f'write {local} /Windows/System32/{name}']:subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],capture_output=True,check=True)
 subprocess.run(['/usr/sbin/debugfs','-R',f'dump /Windows/System32/{name} /tmp/thos-wide-{name}',str(root)],capture_output=True,check=True);assert sha('/tmp/thos-wide-'+name)==sha(local)
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
paths=[Path('xtask/msvcrtx')/n for n in ['msvcrtx.c','kernel32x.c','winargv16.h']]+[Path('xtask/testdata')/n for n in ['wargv.c','wargv.sh','wide-main.c','winargv16-host.c','winunicode.c']]+[Path('xtask/src/main.rs')]
with tarfile.open('target/thos-wide-source-2026-10-17.tar.gz','w:gz') as out:
 for p in paths:out.add(p,arcname=str(p))
Path('target/thos-wide-source-2026-10-17.json').write_text(json.dumps({str(p):sha(p) for p in paths},indent=2)+'\n');print('Wide QA DLL/root verified',sha('target/msvcrtx-wide-2026-10-17.dll'),sha('target/kernel32x-leadbyte-2026-10-17.dll'))
