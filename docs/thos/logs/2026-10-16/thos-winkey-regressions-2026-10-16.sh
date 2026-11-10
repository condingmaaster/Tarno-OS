#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in desk-winkey-test:winkey pe-launch-race-test:pe-launch-race windows-snake-probe:windows-snake desk-winresize-test:winresize wayland-keyboard-test:wayland-keyboard live-desktop-test:live-desktop; do
 testcase=${item%:*};tag=${item#*:};image=target/thos-winkey-return-normal-qa-2026-10-16.img
 export THOS_MEM=2048M THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$tag" = live-desktop ]; then image=target/thos-live-winkey-return-2026-10-16.img;export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 result=0
 bash /tmp/thos-one-case-2026-10-16.sh "$image" "$testcase" > "/tmp/thos-winkey-$tag-2026-10-16.log" 2>&1 || result=$?
 if [ -f "target/$tag-serial.log" ]; then cp "target/$tag-serial.log" "docs/thos/logs/2026-10-16/winkey-$tag-serial.log"; fi
 cp "/tmp/thos-winkey-$tag-2026-10-16.log" "docs/thos/logs/2026-10-16/winkey-$tag-case.log"
 echo "$testcase exit=$result (probe only for Snake)"
 test "$result" = 0
done
