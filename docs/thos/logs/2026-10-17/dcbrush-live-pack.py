from pathlib import Path
import subprocess,hashlib,json
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
previous=Path('target/thos-live.img');assert sha(previous)=='f3846c634bf6629e839980bb768a9b9c2ed37a2670f4968eb7065cf2f2557763'
kernel=Path('target/thos-kernel-dcbrush-2026-10-17.elf');assert sha(kernel)=='ad5f685606d61e44b079dcdd6496e8d262de7fdde4a447e581c4b3f5d8d8c295'
image=Path('target/thos-live-dcbrush-2026-10-17.img');subprocess.run(['cp','--sparse=always','--reflink=auto',str(previous),str(image)],check=True)
subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(kernel),'::boot/thos-kernel'],check=True)
root=Path('target/live-root-codepage-2026-10-16.ext2')
for remote,local in [('::boot/thos-kernel',kernel),('::boot/thos-root.ext2',root)]:
 p=subprocess.Popen(['mtype','-i',f'{image}@@1048576',remote],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(local)
record=json.loads(Path('target/thos-live-2026-10-17-gdi-pages-build-manifest.json').read_text());record.update({'status':'candidate; exact Live boot pending','previous_manifest':'target/thos-live-2026-10-17-gdi-pages-build-manifest.json','previous_live_sha256':sha(previous),'kernel':str(kernel),'kernel_sha256':sha(kernel),'kernel_sources':json.loads(Path('target/thos-dcbrush-source-2026-10-17.json').read_text()),'source_archive':'target/thos-dcbrush-source-2026-10-17.tar.gz','source_archive_sha256':sha('target/thos-dcbrush-source-2026-10-17.tar.gz'),'image':str(image),'image_sha256':sha(image),'scope':'previous real GDI color/SRCCOPY + DC_BRUSH colors and USER32 FillRect; no mono/pens/fonts/alpha/stretch/system-color pseudobrush support','verification':['57 actual PE checks KVM + Westmere','9 host tests','42 SRCCOPY tests','40 x64 processes, 64 MiB each, cleanup without app release, 2 GiB VM','Windows resize/max/F11/corner pixels/AltF4']})
Path('target/thos-live-2026-10-17-dcbrush-build-manifest.json').write_text(json.dumps(record,indent=2)+'\n');print('DCbrush candidate only',record['image_sha256'])
