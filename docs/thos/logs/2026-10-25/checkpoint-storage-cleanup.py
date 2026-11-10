from pathlib import Path
import subprocess,hashlib,json,os,shutil
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
processes=subprocess.check_output(['ps','-eo','comm='],text=True).splitlines();assert not any(p.strip().startswith('qemu') for p in processes),'VM still present; do not clean'
current=json.loads(Path('target/thos-live-2026-10-25-profile-build-manifest.json').read_text());assert sha('target/thos-live.img')==current['image_sha256']
assert Path('target/thos-live-resource-2026-10-25.img').exists() and Path('target/thos-thread-times-qa-2026-10-25.img').exists()
records=[];before=os.statvfs('.').f_bavail*os.statvfs('.').f_frsize
for date,kind in [('2026-10-24','nls'),('2026-10-24','format'),('2026-10-25','thread')]:
 p=Path(f'target/thos-live-{date}-{kind}-build-manifest.json');m=json.loads(p.read_text());assert m['image']==f'target/thos-live-{kind}-{date}.img';assert m['root']==f'target/live-root-{kind}-{date}.ext2'
 for key in ['image','root']:
  file=Path(m[key]);assert file.is_file() and not file.is_symlink() and sha(file)==m[key+'_sha256'];records.append({'path':str(file),'sha256':m[key+'_sha256'],'reason':'Own historical checkpoint surpassed by current verified profile Live and retained resource fallback; small evidence kept'});file.unlink()
 m['image_disposition']='Historical verified image and root retired2026-10-25 after hash check; current verified profile Live and resource fallback remain';p.write_text(json.dumps(m,indent=2)+'\n')
for stem in ['thos-thread-padding-qa-2026-10-25','thos-profile-qa-2026-10-25']:
 p=Path('target')/(stem+'-manifest.json');m=json.loads(p.read_text());file=Path(m['image']);assert str(file)=='target/'+stem+'.img';assert file.is_file() and not file.is_symlink() and sha(file)==m['image_sha256'];records.append({'path':str(file),'sha256':m['image_sha256'],'reason':'Own intermediate QA; final profile baseline and private CPU QA retained'});file.unlink();m['image_disposition']='Own superseded intermediate removed2026-10-25; frozen small artifacts and proof retained';p.write_text(json.dumps(m,indent=2)+'\n')
for name in ['run-991-1792914285556701193','run-1297-1792914340988294980','run-1630-1792914561460310330']:
 p=Path('target/test-scratch')/name
 if p.exists():assert p.is_dir() and not p.is_symlink();shutil.rmtree(p);records.append({'path':str(p),'reason':'Own failed profile fixture scratch; serial/compiler and actual INI readback already retained'})
after=os.statvfs('.').f_bavail*os.statvfs('.').f_frsize
Path('docs/thos/logs/2026-10-25/checkpoint-storage-cleanup.json').write_text(json.dumps({'before_free_bytes':before,'after_free_bytes':after,'observed_freed_bytes':after-before,'removed':records,'retained':'Current verified profile Live57b00eeb and root, resource rollback90f0d0cb and root, profile-final QA baseline and thread-times private QA; earlier preexisting23Oct rollback/browser caches untouched; small sources/binaries/logs/screenshots retained'},indent=2)+'\n')
print('Observed freed bytes',after-before,'Free GiB',round(after/2**30,2))
