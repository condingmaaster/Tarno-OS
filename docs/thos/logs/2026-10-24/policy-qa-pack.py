from pathlib import Path
import subprocess,hashlib,tarfile,json,os
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
assert os.statvfs('.').f_bavail*os.statvfs('.').f_frsize>12*2**30
kernel=Path('target/thos-kernel-policy-2026-10-24.elf');assert not kernel.exists();subprocess.run(['cp','target/x86_64-unknown-none/release/thos-kernel',str(kernel)],check=True)
image=Path('target/thos-policy-qa-2026-10-24.img');assert not image.exists();subprocess.run(['cp','--sparse=always','--reflink=auto','target/thos-dynamic-qa-2026-10-24.img',str(image)],check=True)
subprocess.run(['mcopy','-o','-i',str(image)+'@@1048576',str(kernel),'::boot/thos-kernel'],check=True)
p=subprocess.Popen(['mtype','-i',str(image)+'@@1048576','::boot/thos-kernel'],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(kernel)
paths=list(Path('kernel/src').glob('*.rs'))+[Path('kernel/Cargo.toml'),Path('kernel/build.rs')]+list(Path('kernel/font').glob('*'))+[Path('xtask/src/main.rs'),Path('xtask/src/test_scratch.rs'),Path('tools/qemu-case.sh')]+list(Path('xtask/msvcrtx').glob('*.c'))+list(Path('xtask/msvcrtx').glob('*.h'))+list(Path('xtask/testdata').glob('*.c'))+list(Path('xtask/testdata').glob('*.h'))
a=Path('target/thos-policy-source-2026-10-24.tar.gz')
with tarfile.open(a,'w:gz') as t:
 for p in paths:t.add(p,arcname=str(p))
d={'status':'QA only; actual search-policy tests pending','kernel':str(kernel),'image':str(image),'kernel32x':'target/kernel32x-dynamic-2026-10-24.dll','source_archive':str(a),'sources':{str(p):sha(p)for p in paths},'notes':'Adds process-local SetDefaultDllDirectories, effective app/System32 selection and explicit LoadLibraryExW search overrides. User directories currently empty; AddDllDirectory/SetDllDirectory absent. Leaf-only loading limits retained. Live unchanged.'}
for k in ['kernel','image','kernel32x','source_archive']:d[k+'_sha256']=sha(d[k])
Path('target/thos-policy-qa-2026-10-24-manifest.json').write_text(json.dumps(d,indent=2)+'\n');print(h)
