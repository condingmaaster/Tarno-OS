from pathlib import Path
import subprocess,struct,hashlib,json
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
def copy(a,b):subprocess.run(['cp','--sparse=always','--reflink=auto',str(a),str(b)],check=True)
def update(source,destination,files):
 copy(source,destination)
 for a,b in files:
  for cmd in [f'rm {b}',f'write {a} {b}']:
   subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(destination)],capture_output=True,check=True)
 subprocess.run(['/usr/sbin/e2fsck','-fn',str(destination)],check=True)
def inject(image,root):
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
  assert h.hexdigest()==sha(root)
files=[('target/thosdesk-held-2026-10-16','/thosdesk'),('target/thosdesk-held-2026-10-16','/desktop')]
for label,base,rootbase in [('normal','target/thos-unicode-qa-2026-10-16.img','target/module-root-2026-10-16.ext2'),('browser','target/thos-lock-wait-browser-qa-2026-10-16.img','target/keyboard-browser-root-2026-10-16.ext2')]:
 root=Path(f'target/held-{label}-root-2026-10-16.ext2');image=Path(f'target/thos-held-{label}-qa-2026-10-16.img')
 extra=[('target/kernel32x-module-2026-10-16.dll','/Windows/System32/kernel32x.dll')] if label=='browser' else []
 update(rootbase,root,files+extra);copy(base,image);inject(image,root)
 subprocess.run(['mcopy','-o','-i',f'{image}@@1048576','target/thos-kernel-unicode-2026-10-16.elf','::boot/thos-kernel'],check=True)
 print(label,'QA ready',flush=True)
root=Path('target/live-root-held-2026-10-16.ext2');image=Path('target/thos-live-held-2026-10-16.img')
update('target/live-root-unicode-2026-10-16.ext2',root,files);copy('target/thos-live.img',image)
subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(root),'::boot/thos-root.ext2'],check=True)
for remote,local in [('::boot/thos-root.ext2',root),('::boot/thos-kernel','target/thos-kernel-unicode-2026-10-16.elf')]:
 p=subprocess.Popen(['mtype','-i',f'{image}@@1048576',remote],stdout=subprocess.PIPE)
 actual=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and actual==sha(local)
sparse=Path(str(image)+'.sparse');copy(image,sparse);assert sha(sparse)==sha(image);sparse.replace(image)
snapshot={str(p):sha(p) for p in [Path('xtask/testdata/thosdesk.c'),Path('xtask/testdata/thoswls.h'),Path('xtask/testdata/wlkeyboardclient.c'),Path('xtask/src/main.rs')]}
Path('target/thos-held-source-2026-10-16.json').write_text(json.dumps(snapshot,indent=2)+'\n')
print('Live candidate ready; not promoted',sha(image),flush=True)
