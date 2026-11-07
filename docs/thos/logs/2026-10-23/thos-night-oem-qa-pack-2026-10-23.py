from pathlib import Path
import subprocess,hashlib,json,tarfile,tempfile,os,shutil,struct
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
st=os.statvfs('.');assert st.f_bavail*st.f_frsize>12*2**30
parent=Path('target/thos-night-qa-2026-10-23.img');image=Path('target/thos-night-oem-qa-2026-10-23.img');assert not image.exists()
kernel=Path('target/thos-kernel-night-2026-10-23.elf');assert sha(kernel)=='715de45641d9f66d1105123eaa043d6d372da461502a6bd6c499c580a86829bb'
dll=Path('target/kernel32x-env-oem-2026-10-23.dll');assert dll.is_file()
subprocess.run(['cp','--sparse=always','--reflink=auto',str(parent),str(image)],check=True)
with image.open('rb') as f:
 f.seek(470);entry=f.read(8)
start,sectors=struct.unpack('<II',entry);offset=start*512;size=sectors*512
assert start>2048 and size>500*1024*1024 and offset+size<=image.stat().st_size
with tempfile.TemporaryDirectory(prefix='thos-env-qa-',dir='/tmp') as d:
 root=Path(d)/'root.ext2';readback=Path(d)/'kernel32x.dll'
 with image.open('rb') as src,root.open('wb') as out:
  src.seek(offset);remaining=size
  while remaining:
   block=src.read(min(remaining,1024*1024));assert block;out.write(block);remaining-=len(block)
 for cmd in ['rm /Windows/System32/kernel32x.dll',f'write {dll} /Windows/System32/kernel32x.dll']:
  subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],check=True,capture_output=True)
 subprocess.run(['/usr/sbin/debugfs','-R',f'dump /Windows/System32/kernel32x.dll {readback}',str(root)],check=True,capture_output=True)
 assert sha(readback)==sha(dll)
 subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
 with image.open('r+b') as out,root.open('rb') as src:
  out.seek(offset);shutil.copyfileobj(src,out,1024*1024);out.flush();os.fsync(out.fileno())
 p=subprocess.Popen(['mtype','-i',str(image)+'@@1048576','::boot/thos-kernel'],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(kernel)
paths=list(Path('kernel/src').glob('*.rs'))+[Path('kernel/Cargo.toml'),Path('kernel/build.rs')]+list(Path('kernel/font').glob('*'))+list(Path('xtask/msvcrtx').glob('*'))+[Path('xtask/src/main.rs'),Path('xtask/src/test_scratch.rs'),Path('tools/qemu-case.sh')]+list(Path('xtask/testdata').glob('*.c'))+list(Path('xtask/testdata').glob('*.h'))+list(Path('xtask/testdata').glob('*.sh'))
paths=[p for p in paths if p.is_file()]
archive=Path('target/thos-night-oem-source-2026-10-23.tar.gz')
with tarfile.open(archive,'w:gz') as out:
 for p in paths:out.add(p,arcname=str(p))
r={'status':'QA only; new environment DLL not yet PE-verified','kernel':str(kernel),'kernel_sha256':sha(kernel),'kernel_features':['interactive'],'image':str(image),'image_sha256':sha(image),'kernel32x':str(dll),'kernel32x_sha256':sha(dll),'sources':{str(p):sha(p) for p in paths},'source_archive':str(archive),'source_archive_sha256':sha(archive),'parent':str(parent),'parent_sha256':sha(parent)}
Path('target/thos-night-oem-qa-2026-10-23-manifest.json').write_text(json.dumps(r,indent=2)+'\n');print('Environment QA DLL verified',r['kernel32x_sha256'])
