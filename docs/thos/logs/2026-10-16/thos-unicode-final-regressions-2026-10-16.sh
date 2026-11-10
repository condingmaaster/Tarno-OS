#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in desk-winresize-test:winresize asus-installer-probe:asus-installer live-desktop-test:live-desktop; do
 testcase=${item%:*};tag=${item#*:};image=target/thos-unicode-qa-2026-10-16.img
 export THOS_MEM=2048M THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$tag" = asus-installer ]; then image=target/thos-asus-unicode-qa-2026-10-16.img; fi
 if [ "$tag" = live-desktop ]; then image=target/thos-live-unicode-2026-10-16.img; export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash /tmp/thos-one-case-2026-10-16.sh "$image" "$testcase" > "/tmp/thos-unicode-final-$tag-2026-10-16.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-16/unicode-final-$tag-serial.log"
 cp "/tmp/thos-unicode-final-$tag-2026-10-16.log" "docs/thos/logs/2026-10-16/unicode-final-$tag-case.log"
 echo "$testcase PASS (probe only for ASUS)"
done
