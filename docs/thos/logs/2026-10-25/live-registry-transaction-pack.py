from pathlib import Path
import hashlib,json,subprocess,os
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
parent_path=Path('target/thos-live-2026-10-25-thread-times-build-manifest.json')
parent=json.loads(parent_path.read_text());qpath=Path('target/thos-registry-transaction-qa-2026-10-25-manifest.json');qa=json.loads(qpath.read_text())
assert qa['status'].startswith('QA verified')
assert os.statvfs('.').f_bavail*os.statvfs('.').f_frsize>12*2**30
previous=Path('target/thos-live.img');assert sha(previous)==parent['image_sha256']=='623d3c1d15a0b01559f3ef32ff1555fe5e6d1cb3c322599bb5d54bbab027dd06'
for key in ('kernel','kernel32x','source_archive'):assert sha(qa[key])==qa[key+'_sha256']
for p,h in qa['sources'].items():assert sha(p)==h,p
assert qa['kernel32x_sha256']==parent['kernel32x_sha256']
root=Path(parent['root']);assert sha(root)==parent['root_sha256']
image=Path('target/thos-live-registry-transaction-2026-10-25.img');assert not image.exists()
subprocess.run(['cp','--sparse=always','--reflink=auto',str(previous),str(image)],check=True)
subprocess.run(['mcopy','-o','-i',str(image)+'@@1048576',qa['kernel'],'::boot/thos-kernel'],check=True)
for source,destination in [(qa['kernel'],'::boot/thos-kernel'),(str(root),'::boot/thos-root.ext2')]:
 p=subprocess.Popen(['mtype','-i',str(image)+'@@1048576',destination],stdout=subprocess.PIPE);h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==sha(source)
subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
h=sha(image);size=image.stat().st_size;subprocess.run(['fallocate','--dig-holes',str(image)],check=True);assert sha(image)==h and image.stat().st_size==size
r=dict(parent);r.update(status='candidate only; exact packaged Live desktop boot pending',previous_manifest=str(parent_path),previous_live_sha256=sha(previous),kernel=qa['kernel'],kernel_sha256=qa['kernel_sha256'],kernel_sources={p:h for p,h in qa['sources'].items()if p.startswith('kernel/')},image=str(image),image_sha256=h,source_archive=qa['source_archive'],source_archive_sha256=qa['source_archive_sha256'],runtime_source_archive=qa['source_archive'],runtime_source_archive_sha256=qa['source_archive_sha256'],runtime_sources={p:h for p,h in qa['sources'].items()if p.startswith('xtask/msvcrtx/')},physical_disk_updated=False,physical_keyboard_changed=False)
r['verification']=[v for v in parent['verification']if 'pending'not in v]+qa['verification']+['Exact packaged Live UI boot still pending']
r['scope']=parent['scope']+'; bounded native registry UTF16/ULONG/pointer handling and sleepable staged single-hive changes with accepted filesystem replacement before RAM/notification publication.'
r['limitations']=parent['limitations'].replace('ASUS GetThreadTimes and Snake BeginPaint remain actual blockers.','ASUS now fails on a missing System32 DLL (static import order identifies ADVAPI32 first; SHELL32/OLEAUT32/SHLWAPI also absent), Snake remains at BeginPaint. No installer or gameplay. Instrumented CPU interval accounting excludes waiting/unscheduled time but is not a cycle-accurate profiler; ordinary Linux/kernel/idle threads are not measured. Other previous limitations remain.')
r['limitations']+=' Native registry profile max1MiB per value and128 path separators, malformed UTF16/NUL and unsupported class/security/volatile/OA flags refused. UID policy only, full desired-access/ACL/key identity/delete/buffer/notify semantics incomplete; no NtDeleteValueKey or ADVAPI32. Staged operational IO rollback is not journal/crash/physical flush guarantee. Whole-hive clone/serialization still uses infallible allocations; total hive size not globally bounded.'
Path('target/thos-live-2026-10-25-registry-transaction-build-manifest.json').write_text(json.dumps(r,indent=2)+'\n');print('Candidate only',h,'kernel',qa['kernel_sha256'])
