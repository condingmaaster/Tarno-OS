from pathlib import Path
import subprocess,json,os,hashlib,tempfile,datetime
repo=Path('/home/router/Projekte/Tarno-OS');folder=repo/'backups/pi4-sd-to-intenso-2026-10-17';manifest=folder/'manifest.json';r=json.loads(manifest.read_text())
assert r['status'] in ('full_clone_verified_expansion_pending','expanding_root_partition') and r['target_clone_sha256']==r['source_sha256'] and r['verified_bytes']==r['source_bytes'] and r['expand_root_authorized']
src=r['source'];dst=r['target'];owner=repo.stat()
def save():manifest.write_text(json.dumps(r,indent=2)+'\n');os.chown(manifest,owner.st_uid,owner.st_gid)
for dev,serial in [(src,'121220160204'),(dst,'2608260739440441')]:
 props=subprocess.check_output(['udevadm','info','--query=property','--name',dev],text=True);assert 'ID_SERIAL_SHORT='+serial+'\n' in props and 'ID_BUS=usb\n' in props
 nodes=json.loads(subprocess.check_output(['lsblk','-J','-o','PATH,MOUNTPOINTS',dev],text=True))['blockdevices']
 def check(nodes):
  for n in nodes:assert not any(n.get('mountpoints') or []);check(n.get('children',[]))
 check(nodes)
def table(dev):return json.loads(subprocess.check_output(['sfdisk','--json',dev],text=True))['partitiontable']
a=table(src);b=table(dst);assert a['id']==b['id']=='0x12ef633f' and a['label']==b['label']=='dos'
assert [(p['start'],p['size'],p['type']) for p in a['partitions']]==[(p['start'],p['size'],p['type']) for p in b['partitions']]==[(16384,1048576,'c'),(1064960,60002304,'83')]
part=dst+'-part2';boot=dst+'-part1'
subprocess.run(['/usr/sbin/e2fsck','-fn',part],check=True)
# The verified target alone receives the offline preen/check timestamp needed
# by resize2fs. The source is never checked with writing enabled.
checked=subprocess.run(['/usr/sbin/e2fsck','-f','-p',part]);assert checked.returncode in (0,1),'Target pre-expansion fsck requires attention'
r['status']='expanding_root_partition';save()
new_sectors=r['target_bytes']//512-1064960
subprocess.run(['sfdisk','--wipe','never','--wipe-partitions','never','-N','2',dst],input=f'start=1064960,size={new_sectors},type=83\n',text=True,check=True)
subprocess.run(['blockdev','--rereadpt',dst],check=True);subprocess.run(['udevadm','settle'],check=True)
b=table(dst);assert b['id']=='0x12ef633f' and len(b['partitions'])==2 and b['partitions'][0]['start']==16384 and b['partitions'][0]['size']==1048576 and b['partitions'][1]['start']==1064960 and b['partitions'][1]['size']>60002304
r['status']='expanding_ext4';save();subprocess.run(['/usr/sbin/resize2fs',part],check=True)
subprocess.run(['/usr/sbin/e2fsck','-fn',part],check=True);subprocess.run(['/usr/sbin/fsck.vfat','-n',boot],check=True)
r['final_partition_table']=b;r['boot_file_sha256']={}
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
with tempfile.TemporaryDirectory(prefix='pi-clone-verify-') as d:
 mount=Path(d)/'mount';mount.mkdir()
 for num,names,options in [(1,['cmdline.txt','config.txt','kernel8.img','start4.elf','fixup4.dat'],'ro'),(2,['etc/fstab'],'ro,noload')]:
  checks={}
  for dev in [src,dst]:
   subprocess.run(['mount','-o',options,dev+f'-part{num}',str(mount)],check=True)
   try:
    hashes={name:sha(mount/name) for name in names};checks[dev]=hashes
    if num==2 and dev==dst:
     st=os.statvfs(mount);r['root_fs_bytes']=st.f_blocks*st.f_frsize;r['root_available_bytes']=st.f_bavail*st.f_frsize
   finally:subprocess.run(['umount',str(mount)],check=True)
  assert checks[src]==checks[dst],'Boot configuration/file changed';r['boot_file_sha256'].update(checks[dst])
for path,uuid in [(boot,'1080-F633'),(part,'a3680812-f783-41ac-ab86-ac9e13347b2c')]:assert subprocess.check_output(['blkid','-s','UUID','-o','value',path],text=True).strip()==uuid
p=folder/'target-final.sfdisk';p.write_text(subprocess.check_output(['sfdisk','--dump',dst],text=True));os.chown(p,owner.st_uid,owner.st_gid)
r.update(status='complete_verified_expanded',source_modified=False,finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),unmounted=True,boot_test_on_pi=False);save()
print('COMPLETE: exact raw clone SHA256 matched before expansion; root expanded and checked; source boot/fstab hashes match; both disks unmounted.',flush=True)
