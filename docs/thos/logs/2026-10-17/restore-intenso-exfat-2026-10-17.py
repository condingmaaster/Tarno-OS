from pathlib import Path
import subprocess,json,os,stat,hashlib,time
serial='2608260739440441';device='/dev/disk/by-id/usb-Intenso_cMobile_Line_'+serial+'-0:0'
props=subprocess.check_output(['udevadm','info','--query=property','--name',device],text=True);assert 'ID_SERIAL_SHORT='+serial+'\n' in props and 'ID_BUS=usb\n' in props
real=Path(device).resolve();assert real==Path('/dev/sdb')
size=int(subprocess.check_output(['blockdev','--getsize64',device]));assert size==63333990400
info=json.loads(subprocess.check_output(['lsblk','-J','-o','PATH,MOUNTPOINTS',device],text=True))
def unmounted(nodes):
 for n in nodes:
  assert not any(n.get('mountpoints') or []),'USB in use'
  unmounted(n.get('children',[]))
unmounted(info['blockdevices'])
repo=Path('/home/router/Projekte/Tarno-OS');log=repo/'docs/thos/logs/2026-10-17/intenso-restored-exfat.json';assert not log.exists()
owner=repo.stat();record={'serial':serial,'device_by_id':device,'disk_bytes':size,'status':'formatting','label':'INTENSO','filesystem':'exfat','host_ssd_modified':False}
def save():
 log.write_text(json.dumps(record,indent=2)+'\n');os.chown(log,owner.st_uid,owner.st_gid)
save()
subprocess.run(['sfdisk','--wipe','always','--wipe-partitions','always',device],input='label: dos\nunit: sectors\n\nstart=2048,type=7\n',text=True,check=True)
subprocess.run(['udevadm','settle'],check=True)
part=Path(str(real)+'1');assert stat.S_ISBLK(part.stat().st_mode)
subprocess.run(['/usr/sbin/mkfs.exfat','-L','INTENSO',str(part)],check=True)
mnt=Path('/tmp/intenso-exfat-check-2026-10-17');mnt.mkdir(exist_ok=False)
payload=os.urandom(8*2**20);expected=hashlib.sha256(payload).hexdigest()
subprocess.run(['mount','-t','exfat',str(part),str(mnt)],check=True)
try:
 p=mnt/'THOS-TEMP-USB-CHECK.bin'
 with p.open('xb') as f:f.write(payload);f.flush();os.fsync(f.fileno())
finally:subprocess.run(['umount',str(mnt)],check=True)
subprocess.run(['blockdev','--flushbufs',device],check=True)
subprocess.run(['mount','-t','exfat','-o','ro',str(part),str(mnt)],check=True)
try:assert hashlib.sha256((mnt/'THOS-TEMP-USB-CHECK.bin').read_bytes()).hexdigest()==expected
finally:subprocess.run(['umount',str(mnt)],check=True)
subprocess.run(['mount','-t','exfat',str(part),str(mnt)],check=True)
try:(mnt/'THOS-TEMP-USB-CHECK.bin').unlink();os.sync()
finally:subprocess.run(['umount',str(mnt)],check=True)
mnt.rmdir()
subprocess.run(['/usr/sbin/fsck.exfat','-n',str(part)],check=True)
partbytes=int(subprocess.check_output(['blockdev','--getsize64',str(part)]));assert size-partbytes<=2*2**20
record.update(status='complete_verified',partition_bytes=partbytes,test_bytes=len(payload),readback_sha256=expected,test_file_removed=True,unmounted=True);save()
print('INTENSO exFAT restored, full partition bytes',partbytes,'8 MiB write/read PASS; test file removed; USB unmounted.',flush=True)
