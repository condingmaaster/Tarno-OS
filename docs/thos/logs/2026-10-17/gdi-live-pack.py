from pathlib import Path
import subprocess,hashlib,json
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
previous=Path('target/thos-live.img')
assert sha(previous)=='86279c94974681ba9a8ffeb5284b5a739b4afe63fd0af5330ce4a84566c3972f'
kernel=Path('target/thos-kernel-blit-2026-10-17.elf')
assert sha(kernel)=='a50e832eac85690028734693eb419e15c612ff790870ef7cc22e73adbed11004'
image=Path('target/thos-live-gdi-pages-2026-10-17.img')
subprocess.run(['cp','--sparse=always','--reflink=auto',str(previous),str(image)],check=True)
subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(kernel),'::boot/thos-kernel'],check=True)
root=Path('target/live-root-codepage-2026-10-16.ext2')
for remote,local in [('::boot/thos-kernel',kernel),('::boot/thos-root.ext2',root)]:
 p=subprocess.Popen(['mtype','-i',f'{image}@@1048576',remote],stdout=subprocess.PIPE)
 actual=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and actual==sha(local)
source=json.loads(Path('target/thos-gdi-pages-source-2026-10-17.json').read_text())
for path,h in source.items():assert sha(path)==h
record={'status':'candidate; not promoted, exact Live boot pending','previous_manifest':'target/thos-live-2026-10-16-gdi-handles-build-manifest.json','previous_live_sha256':sha(previous),'kernel':str(kernel),'kernel_sha256':sha(kernel),'kernel_features':['interactive'],'kernel_sources':source,'root':str(root),'root_sha256':sha(root),'cui':'target/thosdesk-focus-2026-10-16','cui_sha256':sha('target/thosdesk-focus-2026-10-16'),'kernel32x':'target/kernel32x-codepage-2026-10-16.dll','kernel32x_sha256':sha('target/kernel32x-codepage-2026-10-16.dll'),'image':str(image),'image_sha256':sha(image),'scope':'owned physical-page color bitmaps/memory DCs and native-format SRCCOPY; default mono, other ROPs, DIB, alpha, text and general games remain unsupported','test_log_directory':'docs/thos/logs/2026-10-17','physical_disk_updated':False,'physical_keyboard_changed':False}
Path('target/thos-live-2026-10-17-gdi-pages-build-manifest.json').write_text(json.dumps(record,indent=2)+'\n')
print('Candidate only; full root/kernel readback verified',record['image_sha256'])
