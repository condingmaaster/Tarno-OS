from pathlib import Path
import os,subprocess,json,hashlib,stat
repo=Path('/home/router/Projekte/Tarno-OS');iso=Path('/tmp/gparted-live-1.8.1-6-amd64.iso');tree=Path('/tmp/gparted-live-tree-2026-10-17')
expected='d789c38779f0d6f7026c12f44c2c52a04f66e28a1aea7d51f3045ad1bbf28411'
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
assert sha(iso)==expected
serial='2608260739440441';device='/dev/disk/by-id/usb-Intenso_cMobile_Line_'+serial+'-0:0';real=Path(device).resolve();assert real==Path('/dev/sdc')
assert not Path('/dev/disk/by-id/usb-Intenso_cMobile_Line_900066D1FAB9B821-0:0').exists()
props=subprocess.check_output(['udevadm','info','--query=property','--name',device],text=True);assert 'ID_SERIAL_SHORT='+serial+'\n' in props and 'ID_BUS=usb\n' in props
size=int(subprocess.check_output(['blockdev','--getsize64',device]));assert 58*2**30<size<61*2**30
nodes=json.loads(subprocess.check_output(['lsblk','-J','-o','PATH,MOUNTPOINTS',device],text=True))['blockdevices']
def unmounted(nodes):
 for n in nodes:assert not any(n.get('mountpoints') or []);unmounted(n.get('children',[]))
unmounted(nodes)
assert (tree/'EFI/boot/bootx64.efi').is_file() and (tree/'live/filesystem.squashfs').is_file()
files=[]
for p in tree.rglob('*'):
 assert not p.is_symlink()
 if p.is_file():assert p.stat().st_size<4*2**30;files.append((str(p.relative_to(tree)),sha(p)))
log=repo/'docs/thos/logs/2026-10-17';manifest=log/'gparted-new-intenso-write.json';assert not manifest.exists()
owner=repo.stat();record={'device_by_id':device,'serial':serial,'capacity_bytes':size,'iso_sha256':expected,'method':'GPT 2 GiB FAT32 EFI System Partition with verified unchanged extracted ISO files; UEFI only','files':dict(files),'status':'preparing','host_ssd_modified':False,'thos_stick_modified':False}
def save():
 manifest.write_text(json.dumps(record,indent=2)+'\n');os.chown(manifest,owner.st_uid,owner.st_gid)
save()
old=log/'gparted-new-usb-original-partitions.sfdisk';old.write_text(subprocess.check_output(['sfdisk','--dump',device],text=True));os.chown(old,owner.st_uid,owner.st_gid)
print('Creating GPT/FAT32 EFI partition on new USB serial',serial,flush=True)
subprocess.run(['sfdisk',device],input='label: gpt\nunit: sectors\n\nstart=2048,size=4194304,type=U,name="GParted Live"\n',text=True,check=True)
subprocess.run(['udevadm','settle'],check=True)
part=Path(str(real)+'1');assert stat.S_ISBLK(part.stat().st_mode)
subprocess.run(['/usr/sbin/mkfs.vfat','-F','32','-n','GPARTED',str(part)],check=True)
mnt=Path('/tmp/gparted-new-usb-mount-2026-10-17');mnt.mkdir(exist_ok=False)
subprocess.run(['mount','-o','noatime',str(part),str(mnt)],check=True)
try:
 record['status']='copying';save()
 import shutil
 for p in sorted(tree.rglob('*')):
  dest=mnt/p.relative_to(tree)
  if p.is_dir():dest.mkdir(exist_ok=True)
  elif p.is_file():shutil.copyfile(p,dest)
 os.sync()
finally:subprocess.run(['umount',str(mnt)],check=True)
subprocess.run(['blockdev','--flushbufs',device],check=True)
subprocess.run(['/usr/sbin/fsck.vfat','-n',str(part)],check=True)
print('Files copied; verifying each file from the USB mounted read-only.',flush=True)
subprocess.run(['mount','-o','ro',str(part),str(mnt)],check=True)
try:
 for name,h in files:assert sha(mnt/name)==h,'USB file hash mismatch: '+name
finally:subprocess.run(['umount',str(mnt)],check=True)
mnt.rmdir();record.update(status='complete_verified',verified_files=len(files));save()
print('VERIFIED UEFI USB COMPLETE:',len(files),'file hashes match ISO extraction.',flush=True)
