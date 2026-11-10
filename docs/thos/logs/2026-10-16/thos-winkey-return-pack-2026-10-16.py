from pathlib import Path
import subprocess,struct,hashlib,json
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
def copy(a,b):subprocess.run(['cp','--sparse=always','--reflink=auto',str(a),str(b)],check=True)
def update(source,out):
 copy(source,out)
 for name in ['/thosdesk','/desktop']:
  for cmd in [f'rm {name}',f'write target/thosdesk-winkey-return-2026-10-16 {name}']:subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(out)],capture_output=True,check=True)
 subprocess.run(['/usr/sbin/e2fsck','-fn',str(out)],check=True)
root=Path('target/winkey-return-normal-root-2026-10-16.ext2');image=Path('target/thos-winkey-return-normal-qa-2026-10-16.img')
update('target/held-normal-root-2026-10-16.ext2',root);copy('target/thos-held-normal-qa-2026-10-16.img',image)
with image.open('r+b') as f,root.open('rb') as r:
 f.seek(470);start,sectors=struct.unpack('<II',f.read(8));start*=512;size=sectors*512;assert start and size==root.stat().st_size and start+size<=image.stat().st_size
 f.seek(start)
 while b:=r.read(262144):
  old=f.read(len(b));assert len(old)==len(b)
  if old!=b:f.seek(-len(b),1);f.write(b)
 f.seek(start);h=hashlib.sha256();left=size
 while left:
  b=f.read(min(left,1048576));assert b;h.update(b);left-=len(b)
 assert h.hexdigest()==sha(root)
print('Win-key normal QA ready',flush=True)
root=Path('target/live-root-winkey-return-2026-10-16.ext2');image=Path('target/thos-live-winkey-return-2026-10-16.img');update('target/live-root-held-2026-10-16.ext2',root);copy('target/thos-live.img',image)
subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(root),'::boot/thos-root.ext2'],check=True)
for remote,local in [('::boot/thos-root.ext2',root),('::boot/thos-kernel','target/thos-kernel-unicode-2026-10-16.elf')]:
 p=subprocess.Popen(['mtype','-i',f'{image}@@1048576',remote],stdout=subprocess.PIPE);actual=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and actual==sha(local)
sparse=Path(str(image)+'.sparse');copy(image,sparse);assert sha(sparse)==sha(image);sparse.replace(image)
source={str(p):sha(p) for p in [Path('xtask/testdata/thosdesk.c'),Path('xtask/testdata/thoswls.h'),Path('xtask/testdata/winkey.c'),Path('xtask/src/main.rs')]}
Path('target/thos-winkey-return-source-2026-10-16.json').write_text(json.dumps(source,indent=2)+'\n')
print('Win-key Live candidate ready; NOT promoted',sha(image),flush=True)
