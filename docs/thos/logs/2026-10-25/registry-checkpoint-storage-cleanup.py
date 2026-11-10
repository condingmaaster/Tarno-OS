from pathlib import Path
import json,hashlib,subprocess,os,shutil
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
processes=subprocess.check_output(['ps','-eo','comm='],text=True).splitlines();assert not any(p.strip().startswith('qemu')for p in processes)
current=json.loads(Path('target/thos-live-2026-10-25-registry-transaction-build-manifest.json').read_text());fallback=json.loads(Path('target/thos-live-2026-10-25-thread-times-build-manifest.json').read_text())
assert current['status'].startswith('verified exact') and sha('target/thos-live.img')==current['image_sha256']
protected={Path('target/thos-live.img').resolve()}
for d in [current,fallback]:
 for key in ['image','root','kernel','kernel32x','source_archive']:protected.add(Path(d[key]).resolve())
entries=[('target/thos-thread-times-qa-2026-10-25-manifest.json',['image']),('target/thos-thread-times-fast-qa-2026-10-25-manifest.json',['image']),('target/thos-thread-times-peonly-qa-2026-10-25-manifest.json',['image']),('target/thos-profile-final-qa-2026-10-25-manifest.json',['image']),('target/thos-registry-native-qa-2026-10-25-manifest.json',['image']),('target/thos-live-2026-10-25-resource-build-manifest.json',['image','root']),('target/thos-live-2026-10-25-profile-build-manifest.json',['image'])]
planned=[]
for manifest,keys in entries:
 d=json.loads(Path(manifest).read_text())
 for key in keys:
  p=Path(d[key]);assert p.is_file()and not p.is_symlink()and p.resolve()not in protected;h=sha(p);assert h==d[key+'_sha256'];planned.append((p,h,manifest,key,p.stat().st_blocks*512))
old=Path('target/test-scratch/run-4162-1792954081697164223');assert old.is_dir()and not old.is_symlink();assert not any(p.is_symlink()for p in old.rglob('*'))
scratch=[{'path':str(p),'sha256':sha(p),'allocated_bytes':p.stat().st_blocks*512}for p in old.rglob('*')if p.is_file()]
free=lambda:os.statvfs('.').f_bavail*os.statvfs('.').f_frsize
before=free();removed=[]
for p,h,manifest,key,allocated in planned:
 p.unlink();removed.append({'path':str(p),'sha256':h,'allocated_bytes':allocated,'reason':'Own superseded checkpoint after verified registry transaction Live; CPU Live fallback, shared root and frozen small source/kernel/log evidence retained'})
 d=json.loads(Path(manifest).read_text());d[key+'_disposition']='Own superseded large artifact removed after verified registry transaction Live; historical small hashes/sources/evidence retained, no longer bootable';Path(manifest).write_text(json.dumps(d,indent=2)+'\n')
shutil.rmtree(old);proof={'before_free_bytes':before,'after_free_bytes':free(),'observed_freed_bytes':free()-before,'removed':removed,'removed_invalid_perf_scratch':scratch,'protected':sorted(map(str,protected))}
Path('docs/thos/logs/2026-10-25/registry-checkpoint-storage-cleanup.json').write_text(json.dumps(proof,indent=2)+'\n');print('Actual freed bytes',proof['observed_freed_bytes'],'free GiB',round(free()/2**30,2))
