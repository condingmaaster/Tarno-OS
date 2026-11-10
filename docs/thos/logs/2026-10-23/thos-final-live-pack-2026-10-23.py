from pathlib import Path
import subprocess,hashlib,json,os,tarfile
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
st=os.statvfs('.');assert st.f_bavail*st.f_frsize>12*2**30
previous=Path('target/thos-live.img');assert sha(previous)=='d465fc857f779bb31316d70281745688db437999246326487f3cde2d0ab5fdd9'
qa=json.loads(Path('target/thos-night-oem-qa-2026-10-23-manifest.json').read_text());kernel=Path(qa['kernel']);assert sha(kernel)==qa['kernel_sha256']
dll=Path('target/kernel32x-env-oem-2026-10-23.dll');assert sha(dll)=='ec0a07a9339aed137bf5482e2b6a3f24300bcca243a861f8e2abaf553d5f73fc'
root=Path('target/live-root-night-2026-10-23.ext2');assert not root.exists();subprocess.run(['cp','--sparse=always','--reflink=auto','target/live-root-synth-2026-10-23.ext2',str(root)],check=True)
for cmd in ['rm /Windows/System32/kernel32x.dll',f'write {dll} /Windows/System32/kernel32x.dll']:subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],capture_output=True,check=True)
subprocess.run(['/usr/sbin/debugfs','-R','dump /Windows/System32/kernel32x.dll /tmp/thos-night-live-dll-readback.dll',str(root)],capture_output=True,check=True);assert sha('/tmp/thos-night-live-dll-readback.dll')==sha(dll)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
image=Path('target/thos-live-night-2026-10-23.img');assert not image.exists();subprocess.run(['cp','--sparse=always','--reflink=auto',str(previous),str(image)],check=True)
for local,remote in [(kernel,'::boot/thos-kernel'),(root,'::boot/thos-root.ext2')]:
 subprocess.run(['mcopy','-o','-i',str(image)+'@@1048576',str(local),remote],check=True)
 p=subprocess.Popen(['mtype','-i',str(image)+'@@1048576',remote],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(local)
h=sha(image);n=image.stat().st_size;subprocess.run(['fallocate','--dig-holes',str(image)],check=True);assert sha(image)==h and image.stat().st_size==n
runtime=[Path('xtask/msvcrtx')/n for n in ['kernel32x.c','msvcrtx.c','winargv16.h','winenv.h']]+[Path('xtask/testdata')/n for n in ['wargv.c','wargv.sh','wide-main.c','wide-main.sh']]
archive=Path('target/thos-runtime-final-source-2026-10-23.tar.gz')
with tarfile.open(archive,'w:gz') as out:
 for p in runtime:out.add(p,arcname=str(p))
r=json.loads(Path('target/thos-live-2026-10-23-synth-build-manifest.json').read_text());r.update(status='candidate; exact packed Live boot pending',previous_manifest='target/thos-live-2026-10-23-synth-build-manifest.json',previous_live_sha256=sha(previous),kernel=str(kernel),kernel_sha256=sha(kernel),kernel_sources={p:s for p,s in qa['sources'].items() if p.startswith('kernel/')},root=str(root),root_sha256=sha(root),image=str(image),image_sha256=h,kernel32x=str(dll),kernel32x_sha256=sha(dll),source_archive=qa['source_archive'],source_archive_sha256=qa['source_archive_sha256'],runtime_source_archive=str(archive),runtime_source_archive_sha256=sha(archive),runtime_sources={str(p):sha(p) for p in runtime},scope='Preserved THOS appearance; prior synth/frame/commandline fixes; genuine Unicode window classes, atoms, titles and message callbacks; bounded binary runtime export lookup; UTF16 runtime environment with independent OEM437/UTF16 snapshots and ACP1252 variable APIs; reference-safe futex queue lifetime; event-driven PE proxy completion and signed Linux pid_t handling',verification=['36 Unicode window checks KVM/Westmere and ANSI/GDI/font/resize regression','25 runtime export bounds checks KVM/Westmere, 19 lookup and 12 native export checks','runtime environment old failures reproduced, new host allocator/thread stress and UBSan, real PE parallel tests; OEM snapshot baseline reproduced','futex queue lookup/enqueue lease regression, real eight-thread PE and Linux pthread tests','72 immediate PE exits with 6 statuses, delayed exit, Linux group kill and raw signed-32-bit wait/kill KVM/Westmere; 64 parallel inherited-tag PE launches','final OEM runtime regression logs in dated directory; exact packed Live boot still pending'],physical_disk_updated=False,physical_keyboard_changed=False,test_log_directory='docs/thos/logs/2026-10-23')
Path('target/thos-live-2026-10-23-night-build-manifest.json').write_text(json.dumps(r,indent=2)+'\n');print('Candidate only; no promotion yet',h)
