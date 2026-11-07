from pathlib import Path
import subprocess,hashlib,json,tarfile
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
assert sha('target/thos-live.img')=='3a3a468d715a326947c7d2a5049fd15d95ba9923ed4ec20a8504e3c86c617d51'
stat=Path('.').stat();free=__import__('os').statvfs('.');assert free.f_bavail*free.f_frsize>12*2**30
kernel=Path('target/thos-kernel-frame-2026-10-23.elf');assert not kernel.exists();subprocess.run(['cp','target/x86_64-unknown-none/release/thos-kernel',str(kernel)],check=True)
image=Path('target/thos-frame-qa-2026-10-23.img');assert not image.exists();subprocess.run(['cp','--sparse=always','--reflink=auto','target/thos-commandline-qa-2026-10-17.img',str(image)],check=True)
subprocess.run(['mcopy','-o','-i',str(image)+'@@1048576',str(kernel),'::boot/thos-kernel'],check=True)
p=subprocess.Popen(['mtype','-i',str(image)+'@@1048576','::boot/thos-kernel'],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(kernel)
paths=list(Path('kernel/src').glob('*.rs'))+[Path('kernel/Cargo.toml'),Path('kernel/build.rs')]+list(Path('kernel/font').glob('*'))+[Path('xtask/src/main.rs'),Path('xtask/src/test_scratch.rs'),Path('tools/qemu-case.sh')]+[Path('xtask/testdata')/n for n in ['winframe.c','desk-winframe.sh','winframe-test.h','gdi-blit.c','gdi-pen.c','gdi-alpha.c','gdi-dcbrush.c','gdi-font.c','winresize.c','winfocus.c','winkey.c','wininput.c','winhello.c','gdi-brush.c']]
with tarfile.open('target/thos-frame-source-2026-10-23.tar.gz','w:gz') as out:
 for p in paths:out.add(p,arcname=str(p))
record={'status':'QA only, PE tests pending','kernel':str(kernel),'kernel_sha256':sha(kernel),'image':str(image),'image_sha256':sha(image),'kernel_features':['interactive'],'sources':{str(p):sha(p) for p in paths},'source_archive':'target/thos-frame-source-2026-10-23.tar.gz'}
Path('target/thos-frame-qa-2026-10-23-manifest.json').write_text(json.dumps(record,indent=2)+'\n');print('QA kernel verified',record['kernel_sha256'])
