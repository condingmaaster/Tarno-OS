#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in desk-winfocus-panel-test:winfocus desk-winkey-test:winkey desk-winresize-test:winresize desk-tasklist-test:desktasklist live-desktop-test:live-desktop; do
 testcase=${item%:*};tag=${item#*:};image=target/thos-focus-normal-qa-2026-10-16.img
 export THOS_MEM=2048M THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$tag" = winfocus ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 if [ "$tag" = live-desktop ]; then image=target/thos-live-focus-2026-10-16.img;export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 result=0
 bash /tmp/thos-one-case-2026-10-16.sh "$image" "$testcase" > "/tmp/thos-focus-$tag-2026-10-16.log" 2>&1 || result=$?
 if [ -f "target/$tag-serial.log" ]; then cp "target/$tag-serial.log" "docs/thos/logs/2026-10-16/focus-$tag-serial.log"; fi
 cp "/tmp/thos-focus-$tag-2026-10-16.log" "docs/thos/logs/2026-10-16/focus-$tag-case.log"
 echo "$testcase exit=$result"
 test "$result" = 0
done
