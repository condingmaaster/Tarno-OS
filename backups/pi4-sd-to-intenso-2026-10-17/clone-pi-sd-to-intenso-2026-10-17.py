from pathlib import Path
import os,subprocess,json,hashlib,time,datetime,fcntl,stat
src='/dev/disk/by-id/usb-Mass_Storage_Device_121220160204-0:0';dst='/dev/disk/by-id/usb-Intenso_cMobile_Line_2608260739440441-0:0'
for dev,serial in [(src,'121220160204'),(dst,'2608260739440441')]:
 p=subprocess.check_output(['udevadm','info','--query=property','--name',dev],text=True);assert 'ID_SERIAL_SHORT='+serial+'\n' in p and 'ID_BUS=usb\n' in p
assert Path(src).resolve()==Path('/dev/sdc') and Path(dst).resolve()==Path('/dev/sdb')
ss=int(subprocess.check_output(['blockdev','--getsize64',src]));ds=int(subprocess.check_output(['blockdev','--getsize64',dst]));assert ss==31266439168 and ds==63333990400 and ds>=ss
for dev in [src,dst]:
 nodes=json.loads(subprocess.check_output(['lsblk','-J','-o','PATH,MOUNTPOINTS',dev],text=True))['blockdevices']
 def check(nodes):
  for n in nodes:
   assert not any(n.get('mountpoints') or []),'Mounted disk/partition'
   check(n.get('children',[]))
 check(nodes)
layout=json.loads(subprocess.check_output(['sfdisk','--json',src],text=True))['partitiontable'];assert layout['label']=='dos' and layout['id']=='0x12ef633f' and len(layout['partitions'])==2
assert [(p['start'],p['size']) for p in layout['partitions']]==[(16384,1048576),(1064960,60002304)]
repo=Path('/home/router/Projekte/Tarno-OS');folder=repo/'backups/pi4-sd-to-intenso-2026-10-17';folder.mkdir(parents=True,exist_ok=False);owner=repo.stat();os.chown(folder,owner.st_uid,owner.st_gid)
record={'source':src,'target':dst,'source_bytes':ss,'target_bytes':ds,'source_modified':False,'ssd_image_created':False,'expand_root_authorized':True,'status':'cloning','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
manifest=folder/'manifest.json'
def save():
 manifest.write_text(json.dumps(record,indent=2)+'\n');os.chown(manifest,owner.st_uid,owner.st_gid)
for dev,name in [(src,'source-partitions.sfdisk'),(dst,'target-before.sfdisk')]:
 p=folder/name;p.write_text(subprocess.check_output(['sfdisk','--dump',dev],text=True));os.chown(p,owner.st_uid,owner.st_gid)
save();print('SOURCE SD:',src,ss,'bytes; TARGET USB:',dst,ds,'bytes. Direct full clone.',flush=True)
sfd=os.open(src,os.O_RDONLY|os.O_EXCL);dfd=os.open(dst,os.O_RDWR|os.O_EXCL);copied=0;last=time.monotonic();digest=hashlib.sha256();chunk=4*2**20
try:
 while copied<ss:
  b=os.read(sfd,min(chunk,ss-copied));assert b,'Unexpected source EOF';digest.update(b);view=memoryview(b)
  while view:
   n=os.write(dfd,view);assert n>0;view=view[n:]
  copied+=len(b)
  if copied%(256*2**20)==0:
   os.posix_fadvise(sfd,copied-256*2**20,256*2**20,os.POSIX_FADV_DONTNEED)
  if time.monotonic()-last>=20:
   record['copied_bytes']=copied;save();print(f'CLONE {copied/2**30:.2f}/{ss/2**30:.2f} GiB ({copied*100/ss:.1f}%)',flush=True);last=time.monotonic()
 os.fsync(dfd);subprocess.run(['blockdev','--flushbufs',dst],check=True)
 record.update(status='verifying_full_clone',copied_bytes=copied,source_sha256=digest.hexdigest());save();print('Full source copied and synced. Reading entire cloned range back.',flush=True)
 os.lseek(dfd,0,os.SEEK_SET);nread=0;verified=hashlib.sha256();last=time.monotonic()
 while nread<ss:
  b=os.read(dfd,min(chunk,ss-nread));assert b,'Unexpected target EOF';verified.update(b);nread+=len(b)
  if nread%(256*2**20)==0:os.posix_fadvise(dfd,nread-256*2**20,256*2**20,os.POSIX_FADV_DONTNEED)
  if time.monotonic()-last>=20:
   record['verified_bytes']=nread;save();print(f'VERIFY {nread/2**30:.2f}/{ss/2**30:.2f} GiB ({nread*100/ss:.1f}%)',flush=True);last=time.monotonic()
 assert verified.hexdigest()==digest.hexdigest(),'FULL CLONE SHA256 MISMATCH'
 record.update(status='full_clone_verified_expansion_pending',verified_bytes=nread,target_clone_sha256=verified.hexdigest());save();print('FULL CLONE SHA256 VERIFIED',verified.hexdigest(),flush=True)
except BaseException as e:
 record.update(status='incomplete_or_failed',error=str(e),copied_bytes=copied);save();raise
finally:os.close(sfd);os.close(dfd)
subprocess.run(['blockdev','--rereadpt',dst],check=True);subprocess.run(['udevadm','settle'],check=True)
print('Clone complete. Root expansion is the separate next step.',flush=True)
