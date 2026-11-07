from pathlib import Path
import hashlib,json,subprocess,os
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
parent_path=Path('target/thos-live-2026-10-25-profile-build-manifest.json')
parent=json.loads(parent_path.read_text());qpath=Path('target/thos-thread-times-peonly-qa-2026-10-25-manifest.json');qa=json.loads(qpath.read_text())
assert qa['status'].startswith('QA verified')
assert os.statvfs('.').f_bavail*os.statvfs('.').f_frsize>12*2**30
previous=Path('target/thos-live.img');assert sha(previous)==parent['image_sha256']=='57b00eeb728bbbbbcfdda6c191c0282ba23e98032d52ba315ef058f0c573088d'
for key in ('kernel','kernel32x','source_archive'):assert sha(qa[key])==qa[key+'_sha256']
for p,h in qa['sources'].items():assert sha(p)==h,p
assert qa['kernel32x_sha256']==parent['kernel32x_sha256']
root=Path(parent['root']);assert sha(root)==parent['root_sha256']
image=Path('target/thos-live-thread-times-2026-10-25.img');assert not image.exists()
subprocess.run(['cp','--sparse=always','--reflink=auto',str(previous),str(image)],check=True)
subprocess.run(['mcopy','-o','-i',str(image)+'@@1048576',qa['kernel'],'::boot/thos-kernel'],check=True)
for source,destination in [(qa['kernel'],'::boot/thos-kernel'),(str(root),'::boot/thos-root.ext2')]:
 p=subprocess.Popen(['mtype','-i',str(image)+'@@1048576',destination],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(source)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
h=sha(image);size=image.stat().st_size;subprocess.run(['fallocate','--dig-holes',str(image)],check=True);assert sha(image)==h and image.stat().st_size==size
r=dict(parent);r.update(status='candidate only; exact packaged Live desktop boot pending',previous_manifest=str(parent_path),previous_live_sha256=sha(previous),kernel=qa['kernel'],kernel_sha256=qa['kernel_sha256'],kernel_sources={p:h for p,h in qa['sources'].items()if p.startswith('kernel/')},image=str(image),image_sha256=h,source_archive=qa['source_archive'],source_archive_sha256=qa['source_archive_sha256'],runtime_source_archive=qa['source_archive'],runtime_source_archive_sha256=qa['source_archive_sha256'],runtime_sources={p:h for p,h in qa['sources'].items()if p.startswith('xtask/msvcrtx/')},physical_disk_updated=False,physical_keyboard_changed=False)
r['verification']=[v for v in parent['verification']if 'pending'not in v]+qa['verification']+['Exact packaged Live UI boot still pending']
r['scope']=parent['scope']+'; genuine GetThreadTimes over instrumented PE-only user/kernel CPU intervals, actual creation/exit UTC timestamps and retained small records.'
r['limitations']=parent['limitations'].replace('ASUS GetThreadTimes and Snake BeginPaint remain actual blockers.','ASUS now fails on a missing System32 DLL (static import order identifies ADVAPI32 first; SHELL32/OLEAUT32/SHLWAPI also absent), Snake remains at BeginPaint. No installer or gameplay. Instrumented CPU interval accounting excludes waiting/unscheduled time but is not a cycle-accurate profiler; ordinary Linux/kernel/idle threads are not measured. Other previous limitations remain.')
Path('target/thos-live-2026-10-25-thread-times-build-manifest.json').write_text(json.dumps(r,indent=2)+'\n');print('Candidate only',h,'kernel',qa['kernel_sha256'])
