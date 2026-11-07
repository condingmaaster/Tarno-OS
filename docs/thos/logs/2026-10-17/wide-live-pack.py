from pathlib import Path
import subprocess,hashlib,json
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
prev=Path('target/thos-live.img');assert sha(prev)=='2b8d13f0eff696182ed1f45141794b23401224ee1165f1d6ae40bdfab7b2bf5f'
kernel=Path('target/thos-kernel-pen-2026-10-17.elf');assert sha(kernel)=='4bab66c28c41bcf6b7bc1023370ac8729d3b15c5e1070372e54385007deefac0'
root=Path('target/live-root-wide-2026-10-17.ext2');subprocess.run(['cp','--sparse=always','--reflink=auto','target/live-root-font-2026-10-17.ext2',str(root)],check=True)
for local,name in [('target/msvcrtx-wide-2026-10-17.dll','msvcrtx.dll'),('target/kernel32x-leadbyte-2026-10-17.dll','kernel32x.dll')]:
 for cmd in [f'rm /Windows/System32/{name}',f'write {local} /Windows/System32/{name}']:subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],capture_output=True,check=True)
 subprocess.run(['/usr/sbin/debugfs','-R',f'dump /Windows/System32/{name} /tmp/thos-wide-live-{name}',str(root)],capture_output=True,check=True);assert sha('/tmp/thos-wide-live-'+name)==sha(local)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
image=Path('target/thos-live-wide-2026-10-17.img');subprocess.run(['cp','--sparse=always','--reflink=auto',str(prev),str(image)],check=True)
subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(root),'::boot/thos-root.ext2'],check=True)
for remote,local in [('::boot/thos-kernel',kernel),('::boot/thos-root.ext2',root)]:
 p=subprocess.Popen(['mtype','-i',f'{image}@@1048576',remote],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(local)
h=sha(image);n=image.stat().st_size;subprocess.run(['fallocate','--dig-holes',str(image)],check=True);assert sha(image)==h and image.stat().st_size==n
record=json.loads(Path('target/thos-live-2026-10-17-pen-build-manifest.json').read_text());record.update({'status':'candidate; exact Live boot pending','previous_manifest':'target/thos-live-2026-10-17-pen-build-manifest.json','previous_live_sha256':sha(prev),'root':str(root),'root_sha256':sha(root),'image':str(image),'image_sha256':sha(image),'kernel32x':'target/kernel32x-leadbyte-2026-10-17.dll','kernel32x_sha256':sha('target/kernel32x-leadbyte-2026-10-17.dll'),'msvcrtx':'target/msvcrtx-wide-2026-10-17.dll','msvcrtx_sha256':sha('target/msvcrtx-wide-2026-10-17.dll'),'runtime_source_archive':'target/thos-wide-source-2026-10-17.tar.gz','runtime_source_archive_sha256':sha('target/thos-wide-source-2026-10-17.tar.gz'),'runtime_sources':json.loads(Path('target/thos-wide-source-2026-10-17.json').read_text()),'scope':'previous verified GDI + bounded UTF16 CRT startup, real wcmdln/winitenv data, initial PEB env snapshot, futex concurrent init, shared errno and validated codepage lead bytes; Linux env inheritance and wildcard/newmode expansion unsupported','verification':['1203 host UTF16 CRT parser cases vs independent Python encoding and buffer canaries','18 actual wide CRT PE checks KVM + Westmere, eight competing initializers and controlled PEB env metadata']})
Path('target/thos-live-2026-10-17-wide-build-manifest.json').write_text(json.dumps(record,indent=2)+'\n');print('Wide candidate only',record['image_sha256'])
