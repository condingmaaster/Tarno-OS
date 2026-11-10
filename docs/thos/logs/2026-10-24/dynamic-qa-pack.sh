#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
df -h .
cp target/x86_64-unknown-none/release/thos-kernel target/thos-kernel-dynamic-2026-10-24.elf
x86_64-w64-mingw32-gcc -O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe -nostdlib -shared -Wl,--entry=DllMain -Wl,--dynamicbase -o target/kernel32x-dynamic-2026-10-24.dll xtask/msvcrtx/kernel32x.c -lntdll -lkernel32
if [ ! -f target/thos-dynamic-qa-2026-10-24.img ]; then cp --sparse=always target/thos-ldr-qa-2026-10-24.img target/thos-dynamic-qa-2026-10-24.img; fi
mcopy -o -i target/thos-dynamic-qa-2026-10-24.img@@1048576 target/thos-kernel-dynamic-2026-10-24.elf ::boot/thos-kernel
mtype -i target/thos-dynamic-qa-2026-10-24.img@@1048576 ::boot/thos-kernel | sha256sum
mdel -i target/thos-dynamic-qa-2026-10-24.img@@1048576 ::/thos-kernel || true
python3 /tmp/patch-windyn-root.py
sha256sum target/thos-kernel-dynamic-2026-10-24.elf target/kernel32x-dynamic-2026-10-24.dll target/thos-dynamic-qa-2026-10-24.img
