from pathlib import Path
import subprocess,hashlib,json
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
prev=Path('target/thos-live.img');assert sha(prev)=='a5cc2accbed7a7d4c692dea9e141344c97feb1cd12db2eb13b8fed04813a487b'
kernel=Path('target/thos-kernel-font-2026-10-17.elf');assert sha(kernel)=='4ddeae7188653840ca976ded1a8edce63bade3dfe4ed421c300e7dd7d234823f'
root=Path('target/live-root-font-2026-10-17.ext2');subprocess.run(['cp','--sparse=always','--reflink=auto','target/live-root-alpha-2026-10-17.ext2',str(root)],check=True)
for cmd in ['rm /usr/share/fonts/thos/copyright','write kernel/font/gdi-lato-copyright.txt /usr/share/fonts/thos/copyright']:
 subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],capture_output=True,check=True)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
subprocess.run(['/usr/sbin/debugfs','-R','dump /usr/share/fonts/thos/copyright /tmp/thos-font-live-copyright.txt',str(root)],capture_output=True,check=True);assert sha('/tmp/thos-font-live-copyright.txt')==sha('kernel/font/gdi-lato-copyright.txt')
image=Path('target/thos-live-font-2026-10-17.img');subprocess.run(['cp','--sparse=always','--reflink=auto',str(prev),str(image)],check=True)
for remote,local in [('::boot/thos-kernel',kernel),('::boot/thos-root.ext2',root)]:
 subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(local),remote],check=True)
 p=subprocess.Popen(['mtype','-i',f'{image}@@1048576',remote],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(local)
h=sha(image);n=image.stat().st_size;subprocess.run(['fallocate','--dig-holes',str(image)],check=True);assert sha(image)==h and image.stat().st_size==n
record=json.loads(Path('target/thos-live-2026-10-17-alpha-build-manifest.json').read_text());record.update({'status':'candidate; exact Live boot pending','previous_manifest':'target/thos-live-2026-10-17-alpha-build-manifest.json','previous_live_sha256':sha(prev),'kernel':str(kernel),'kernel_sha256':sha(kernel),'kernel_sources':json.loads(Path('target/thos-font-source-2026-10-17.json').read_text()),'source_archive':'target/thos-font-source-2026-10-17.tar.gz','source_archive_sha256':sha('target/thos-font-source-2026-10-17.tar.gz'),'root':str(root),'root_sha256':sha(root),'image':str(image),'image_sha256':sha(image),'font_atlas':'kernel/font/gdi-lato.bin','font_atlas_sha256':sha('kernel/font/gdi-lato.bin'),'font_license':'/usr/share/fonts/thos/copyright','scope':'previous GDI + owned proportional raster fonts from existing THOS Lato, integer AA TextOutW, properties, metrics/extents/current object; limited atlas sizes/Latin/Euro, unsupported styles/fixed fonts fail, no general TTF install or game support','verification':['14 host tests (11 objects/blit + 3 atlas)','38 actual PE font tests KVM + Westmere','136654 actual screenshot pixels, 6147 ink pixels, zero differences from independent FreeType/original TTF rendering on both CPUs','22 alpha regression checks','57 DC brush regression checks']})
Path('target/thos-live-2026-10-17-font-build-manifest.json').write_text(json.dumps(record,indent=2)+'\n');print('Font candidate only',record['image_sha256'])
