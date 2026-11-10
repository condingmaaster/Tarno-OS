import struct,subprocess,pathlib
p=pathlib.Path('target/thos-dynamic-qa-2026-10-24.img');root=pathlib.Path('target/dynamic-root-scratch.ext2')
with p.open('r+b') as f:
 f.seek(470);start,size=struct.unpack('<II',f.read(8));start*=512;size*=512;f.seek(start)
 with root.open('wb')as o:
  left=size
  while left:
   data=f.read(min(left,1024*1024));assert data;left-=len(data)
   if data.strip(b'\0'):o.write(data)
   else:o.seek(len(data),1)
  o.truncate(size)
 subprocess.run(['/usr/sbin/debugfs','-w','-R','rm /Windows/System32/kernel32x.dll',str(root)],check=True)
 subprocess.run(['/usr/sbin/debugfs','-w','-R','write target/kernel32x-dynamic-2026-10-24.dll /Windows/System32/kernel32x.dll',str(root)],check=True)
 subprocess.run(['/usr/sbin/e2fsck','-fn',str(root)],check=True)
 f.seek(start)
 with root.open('rb')as i:
  while data:=i.read(1024*1024):f.write(data)
root.unlink()
