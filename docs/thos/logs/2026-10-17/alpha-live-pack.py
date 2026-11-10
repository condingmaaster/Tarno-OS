from pathlib import Path
import subprocess,hashlib,json
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
prev=Path('target/thos-live.img');assert sha(prev)=='09b5ffe15dd3ec2183751d7e982ab48e195e78e3b2cc7607dc4baf1ef29a35e8'
kernel=Path('target/thos-kernel-alpha-2026-10-17.elf');assert sha(kernel)=='115c8ac05833fa4802dc34bb87e186cab88e02c5c407750d938ab70539515da5'
root=Path('target/live-root-alpha-2026-10-17.ext2');subprocess.run(['cp','--sparse=always','--reflink=auto','target/live-root-codepage-2026-10-16.ext2',str(root)],check=True)
for cmd in ['rm /Windows/System32/msimg32.dll','write target/msimg32-alpha-2026-10-17.dll /Windows/System32/msimg32.dll']:
 subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],capture_output=True,check=True)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
subprocess.run(['/usr/sbin/debugfs','-R','dump /Windows/System32/msimg32.dll /tmp/thos-alpha-live-msimg32.dll',str(root)],capture_output=True,check=True)
assert sha('/tmp/thos-alpha-live-msimg32.dll')==sha('target/msimg32-alpha-2026-10-17.dll')
image=Path('target/thos-live-alpha-2026-10-17.img');subprocess.run(['cp','--sparse=always','--reflink=auto',str(prev),str(image)],check=True)
for remote,local in [('::boot/thos-kernel',kernel),('::boot/thos-root.ext2',root)]:
 subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(local),remote],check=True)
 p=subprocess.Popen(['mtype','-i',f'{image}@@1048576',remote],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(local)
# Preserve contents while reclaiming zero blocks written by FAT/root packing.
h=sha(image);n=image.stat().st_size;subprocess.run(['fallocate','--dig-holes',str(image)],check=True);assert sha(image)==h and image.stat().st_size==n
record=json.loads(Path('target/thos-live-2026-10-17-dcbrush-build-manifest.json').read_text());record.update({'status':'candidate; exact Live boot pending','previous_manifest':'target/thos-live-2026-10-17-dcbrush-build-manifest.json','previous_live_sha256':sha(prev),'kernel':str(kernel),'kernel_sha256':sha(kernel),'kernel_sources':json.loads(Path('target/thos-alpha-source-2026-10-17.json').read_text()),'source_archive':'target/thos-alpha-source-2026-10-17.tar.gz','source_archive_sha256':sha('target/thos-alpha-source-2026-10-17.tar.gz'),'root':str(root),'root_sha256':sha(root),'msimg32':'target/msimg32-alpha-2026-10-17.dll','msimg32_sha256':sha('target/msimg32-alpha-2026-10-17.dll'),'image':str(image),'image_sha256':sha(image),'scope':'previous real color GDI plus constant-alpha AC_SRC_OVER, equal-size native color surfaces; scaling and per-pixel alpha explicitly fail; no general game support','verification':['10 host tests, exhaustive channel pairs at 8 alpha values + clipping/ownership','22 actual PE alpha checks KVM + Westmere including MSIMG32/GDI32 ABI and actual pixels','42 actual BitBlt regression checks','19 PE export checks']})
Path('target/thos-live-2026-10-17-alpha-build-manifest.json').write_text(json.dumps(record,indent=2)+'\n');print('Alpha candidate only',record['image_sha256'])
