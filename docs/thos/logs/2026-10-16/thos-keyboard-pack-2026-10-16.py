from pathlib import Path
import subprocess,struct,hashlib
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
def copy(a,b):subprocess.run(['cp','--sparse=always','--reflink=never',str(a),str(b)],check=True)
def root_update(source,out):
 copy(source,out)
 for n in ('/thosdesk','/desktop'):
  for cmd in (f'rm {n}',f'write target/thosdesk-keyboard-modifiers-2026-10-16 {n}'):
   subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(out)],capture_output=True,check=True)
 r=subprocess.run(['/usr/sbin/e2fsck','-fn',str(out)],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr

def inject(image,root):
 with image.open('r+b') as f,root.open('rb') as r:
  f.seek(470);start,sectors=struct.unpack('<II',f.read(8));start*=512;size=sectors*512
  assert start and size==root.stat().st_size and start+size<=image.stat().st_size
  f.seek(start)
  while b:=r.read(256*1024):
   old=f.read(len(b));assert len(old)==len(b)
   if b!=old:f.seek(-len(b),1);f.write(b)
  f.seek(start);left=size;h=hashlib.sha256()
  while left:
   b=f.read(min(left,4*1024*1024));assert b;h.update(b);left-=len(b)
  assert h.hexdigest()==sha(root)
for label,source,rootbase in [('normal','target/thos-readonly-qa-2026-10-16.img','target/multipointer-normal-root-2026-10-16.ext2'),('browser','target/thos-browser-readonly-qa-2026-10-16.img','target/browser-regions-root-2026-10-16.ext2')]:
 root=Path(f'target/keyboard-{label}-root-2026-10-16.ext2');image=Path(f'target/thos-keyboard-{label}-qa-2026-10-16.img')
 root_update(rootbase,root);copy(source,image);inject(image,root);print(label,'QA ready',flush=True)
root=Path('target/live-root-keyboard-2026-10-16.ext2');image=Path('target/thos-live-keyboard-2026-10-16.img')
root_update('target/live-root-multipointer-2026-10-16.ext2',root);copy('target/thos-live-readonly-2026-10-16.img',image)
subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(root),'::boot/thos-root.ext2'],check=True)
sparse=Path(str(image)+'.sparse');copy(image,sparse);assert sha(image)==sha(sparse);sparse.replace(image)
print('Live candidate ready; not promoted',flush=True)
