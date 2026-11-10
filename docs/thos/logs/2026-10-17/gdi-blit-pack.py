import subprocess,hashlib
from pathlib import Path
kernel=Path('target/thos-kernel-blit-2026-10-17.elf')
subprocess.run(['cp','target/x86_64-unknown-none/release/thos-kernel',str(kernel)],check=True)
image=Path('target/thos-blit-qa-2026-10-17.img')
subprocess.run(['cp','--sparse=always','--reflink=auto','target/thos-gdi-handles-qa-2026-10-16.img',str(image)],check=True)
subprocess.run(['mcopy','-o','-i',f'{image}@@1048576',str(kernel),'::boot/thos-kernel'],check=True)
p=subprocess.Popen(['mtype','-i',f'{image}@@1048576','::boot/thos-kernel'],stdout=subprocess.PIPE)
h=hashlib.file_digest(p.stdout,'sha256').hexdigest();assert p.wait()==0 and h==hashlib.file_digest(kernel.open('rb'),'sha256').hexdigest()
print('candidate kernel readback SHA256',h)
