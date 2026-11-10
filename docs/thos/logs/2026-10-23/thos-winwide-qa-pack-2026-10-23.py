from pathlib import Path
import subprocess,hashlib,json,tarfile,os
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
st=os.statvfs('.');assert st.f_bavail*st.f_frsize>12*2**30
kernel=Path('target/thos-kernel-winwide-2026-10-23.elf');assert not kernel.exists();subprocess.run(['cp','target/x86_64-unknown-none/release/thos-kernel',str(kernel)],check=True)
image=Path('target/thos-winwide-qa-2026-10-23.img');assert not image.exists();subprocess.run(['cp','--sparse=always','--reflink=auto','target/thos-synth-qa-2026-10-23.img',str(image)],check=True)
subprocess.run(['mcopy','-o','-i',str(image)+'@@1048576',str(kernel),'::boot/thos-kernel'],check=True)
p=subprocess.Popen(['mtype','-i',str(image)+'@@1048576','::boot/thos-kernel'],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(kernel)
paths=list(Path('kernel/src').glob('*.rs'))+[Path('kernel/Cargo.toml'),Path('kernel/build.rs')]+list(Path('kernel/font').glob('*'))+[Path('xtask/src/main.rs'),Path('xtask/src/test_scratch.rs'),Path('tools/qemu-case.sh')]+list(Path('xtask/testdata').glob('*.c'))+[Path('xtask/testdata/winframe-test.h'),Path('xtask/testdata/desk-winframe.sh')]
with tarfile.open('target/thos-winwide-source-2026-10-23.tar.gz','w:gz') as out:
 for p in paths:out.add(p,arcname=str(p))
record={'status':'QA only, real PE tests pending','kernel':str(kernel),'kernel_sha256':sha(kernel),'image':str(image),'image_sha256':sha(image),'kernel_features':['interactive'],'sources':{str(p):sha(p) for p in paths},'source_archive':'target/thos-winwide-source-2026-10-23.tar.gz','source_archive_sha256':sha('target/thos-winwide-source-2026-10-23.tar.gz')}
Path('target/thos-winwide-qa-2026-10-23-manifest.json').write_text(json.dumps(record,indent=2)+'\n');print('Unicode-window QA kernel verified',record['kernel_sha256'])
