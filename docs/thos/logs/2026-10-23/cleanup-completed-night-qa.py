from pathlib import Path
import subprocess,json,hashlib,shutil,os
base=Path('/home/router/Projekte/Tarno-OS');os.chdir(base)
processes=subprocess.check_output(['ps','-eo','pid,comm,args'],text=True)
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
records=[]
for label in ['frame','synth','winwide','exportbounds','env','futex','pewait','night']:
 image=Path(f'target/thos-{label}-qa-2026-10-23.img');manifest=image.with_name(image.stem+'-manifest.json')
 assert image.is_file() and not image.is_symlink() and str(image.resolve()) not in processes
 r=json.loads(manifest.read_text());assert sha(image)==r['image_sha256'];assert Path(r['kernel']).is_file();assert Path(r['source_archive']).is_file()
 records.append({'path':str(image),'sha256':r['image_sha256'],'allocated_bytes':image.stat().st_blocks*512,'reason':'completed intermediate QA image; frozen source/kernel/manifest/logs retained; latest QA and verified Live retained'})
 image.unlink();r['artifact_disposition']='intermediate QA image removed after completed verification/diagnosis to bound disk usage; recorded checksum describes original; frozen kernel and source archive retained; regenerate a semantic QA seed when needed';manifest.write_text(json.dumps(r,indent=2)+'\n')
failed=[('run-10271-1792791721307588619','pewait','pewait-first-fixture-serial.log'),('run-10763-1792791771209560014','pewait','pewait-signal-failure-serial.log'),('run-11022-1792791842344643229','pewait','pewait-pid-abi-reproduction-serial.log')]
for name,tag,log in failed:
 p=Path('target/test-scratch')/name;assert (Path('docs/thos/logs/2026-10-23')/log).stat().st_size>1000
 assert p.is_dir() and not p.is_symlink() and str(p.resolve()) not in processes
 assert {f.name for f in p.iterdir()}=={f'{tag}.img',f'{tag}-private-root.ext2',f'priv-{tag}-test.img'}
 records.append({'path':str(p),'allocated_bytes':sum(f.stat().st_blocks*512 for f in p.iterdir()),'diagnostic_log':log});shutil.rmtree(p)
Path('docs/thos/logs/2026-10-23/completed-qa-cleanup.json').write_text(json.dumps(records,indent=2)+'\n');print('Removed completed intermediates only; actual bytes',sum(r['allocated_bytes'] for r in records))
