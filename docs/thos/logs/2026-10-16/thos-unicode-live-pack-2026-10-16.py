from pathlib import Path
from datetime import datetime,timezone
import hashlib,subprocess,json
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
def copy(a,b):subprocess.run(['cp','--sparse=always','--reflink=auto',str(a),str(b)],check=True)
root=Path('target/live-root-unicode-2026-10-16.ext2');image=Path('target/thos-live-unicode-2026-10-16.img')
kernel=Path('target/thos-kernel-unicode-2026-10-16.elf');dll=Path('target/kernel32x-module-2026-10-16.dll')
copy('target/live-root-keyboard-2026-10-16.ext2',root)
for cmd in ['rm /Windows/System32/kernel32x.dll',f'write {dll} /Windows/System32/kernel32x.dll']:
 subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],check=True)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
copy('target/thos-live.img',image)
for a,b in [(root,'::boot/thos-root.ext2'),(kernel,'::boot/thos-kernel')]:
 subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(a),b],check=True)
 proc=subprocess.Popen(['mtype','-i',f'{image}@@1048576',b],stdout=subprocess.PIPE)
 actual=hashlib.file_digest(proc.stdout,'sha256').hexdigest();assert proc.wait()==0 and actual==sha(a),(a,actual)
# mcopy writes logical zeroes too; preserve every byte while recovering holes.
sparse=Path(str(image)+'.sparse');copy(image,sparse);assert sha(image)==sha(sparse);sparse.replace(image)
old=json.loads(Path('target/thos-live-2026-10-16-lock-wait-build-manifest.json').read_text())
source={str(p):sha(p) for p in sorted(Path('kernel').rglob('*.rs'))}
source.update({str(p):sha(p) for p in sorted(Path('xtask').rglob('*.c'))})
source.update({str(p):sha(p) for p in [Path('kernel/Cargo.toml'),Path('xtask/src/main.rs')]})
manifest={'built_at':datetime.now(timezone.utc).isoformat(),'features':['interactive'],'profile':old['profile'],'image':{'path':str(image),'bytes':image.stat().st_size,'sha256':sha(image)},'kernel':{'path':str(kernel),'sha256':sha(kernel)},'root':{'path':str(root),'sha256':sha(root)},'kernel32x':{'path':str(dll),'sha256':sha(dll)},'desktop_sha256':old['desktop_sha256'],'source_sha256':source,'previous_image':old['image'],'inherited_baseline_manifest':'target/thos-live-2026-10-16-lock-wait-build-manifest.json','verification':{'root_e2fsck':'PASS','complete_pack_readback':'frozen kernel and complete RAM-root hashes matched','live_boot':'PENDING','physical_usb_updated':False,'physical_keyboard_changed':False,'new_work':'module full names, process-local launch metadata, bounded CP_UTF8 conversions; legacy code pages still incomplete'},'promotion_status':'candidate only, not promoted'}
Path('target/thos-live-2026-10-16-unicode-build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Candidate packaged and completely read back; Live unchanged',manifest['image'],flush=True)
