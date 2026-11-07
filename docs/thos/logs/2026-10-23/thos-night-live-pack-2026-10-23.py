from pathlib import Path
import subprocess,hashlib,json,os,tarfile
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
st=os.statvfs('.');assert st.f_bavail*st.f_frsize>12*2**30
previous=Path('target/thos-live.img');assert sha(previous)=='3a3a468d715a326947c7d2a5049fd15d95ba9923ed4ec20a8504e3c86c617d51'
qa=json.loads(Path('target/thos-synth-qa-2026-10-23-manifest.json').read_text());kernel=Path(qa['kernel']);assert sha(kernel)==qa['kernel_sha256']
dll=Path('target/kernel32x-commandline-2026-10-17.dll');assert sha(dll)=='b2ccb439c2a78e72cea47f1c1b22521e219d2fc451fbba8977c8da71e266c559'
root=Path('target/live-root-synth-2026-10-23.ext2');assert not root.exists();subprocess.run(['cp','--sparse=always','--reflink=auto','target/live-root-wide-2026-10-17.ext2',str(root)],check=True)
for cmd in ['rm /Windows/System32/kernel32x.dll',f'write {dll} /Windows/System32/kernel32x.dll']:subprocess.run(['/usr/sbin/debugfs','-w','-R',cmd,str(root)],capture_output=True,check=True)
subprocess.run(['/usr/sbin/debugfs','-R','dump /Windows/System32/kernel32x.dll /tmp/thos-night-live-dll-readback.dll',str(root)],capture_output=True,check=True);assert sha('/tmp/thos-night-live-dll-readback.dll')==sha(dll)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
image=Path('target/thos-live-synth-2026-10-23.img');assert not image.exists();subprocess.run(['cp','--sparse=always','--reflink=auto',str(previous),str(image)],check=True)
for local,remote in [(kernel,'::boot/thos-kernel'),(root,'::boot/thos-root.ext2')]:
 subprocess.run(['mcopy','-o','-i',str(image)+'@@1048576',str(local),remote],check=True)
 p=subprocess.Popen(['mtype','-i',str(image)+'@@1048576',remote],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(local)
h=sha(image);n=image.stat().st_size;subprocess.run(['fallocate','--dig-holes',str(image)],check=True);assert sha(image)==h and image.stat().st_size==n
runtime=[Path('xtask/msvcrtx')/n for n in ['kernel32x.c','msvcrtx.c','winargv16.h']]+[Path('xtask/testdata')/n for n in ['wargv.c','wargv.sh','wide-main.c','wide-main.sh']]
archive=Path('target/thos-runtime-source-2026-10-23.tar.gz')
with tarfile.open(archive,'w:gz') as out:
 for p in runtime:out.add(p,arcname=str(p))
r=json.loads(Path('target/thos-live-2026-10-17-wide-build-manifest.json').read_text());r.update(status='candidate; exact packed Live boot pending',previous_manifest='target/thos-live-2026-10-17-wide-build-manifest.json',previous_live_sha256=sha(previous),kernel=str(kernel),kernel_sha256=sha(kernel),kernel_sources={p:s for p,s in qa['sources'].items() if p.startswith('kernel/')},root=str(root),root_sha256=sha(root),image=str(image),image_sha256=h,kernel32x=str(dll),kernel32x_sha256=sha(dll),source_archive=qa['source_archive'],source_archive_sha256=qa['source_archive_sha256'],runtime_source_archive=str(archive),runtime_source_archive_sha256=sha(archive),runtime_sources={str(p):sha(p) for p in runtime},scope='Existing THOS appearance preserved; real UTF16 command pointer; coherent client/outer frame sizes; fallible window backing; nonoverlapping bounded synthetic DLL pages with sorted name/ordinal pairs; successful common QA scratch cleanup and disk headroom guard',verification=['25 wide CRT PE checks KVM and Westmere','standard wmain, 39 Unicode checks, module/export/CRT regression','24 frame PE checks KVM/Westmere, displayed client pixels, existing font/pen/alpha/blit/brush/resize/focus regression','12 independent native DLL export PE checks, old-kernel overlap/unsorted baseline reproduced; final synth-kernel regression results in dated logs'],physical_disk_updated=False,physical_keyboard_changed=False,test_log_directory='docs/thos/logs/2026-10-23')
Path('target/thos-live-2026-10-23-synth-build-manifest.json').write_text(json.dumps(r,indent=2)+'\n');print('Candidate only; no promotion yet',h)
