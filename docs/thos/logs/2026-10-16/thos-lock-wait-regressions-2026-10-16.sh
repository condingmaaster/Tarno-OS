#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in posix-lock-test:posix-lock sqlite-lock-test:sqlite-lock record-lock-churn-test:record-lock-churn readonly-file-test:readonly-file flock-test:flock memfd-freeze-test:memfd-freeze winlookup-test:winlookup firefox-downloads-test:firefox-downloads live-desktop-test:live-desktop; do
 name=${item%:*};tag=${item#*:};image=target/thos-lock-wait-qa-2026-10-16.img
 export THOS_MEM=2048M THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$name" = posix-lock-test ] || [ "$name" = sqlite-lock-test ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 if [ "$name" = firefox-downloads-test ]; then image=target/thos-lock-wait-browser-qa-2026-10-16.img; export THOS_MEM=4096M; fi
 if [ "$name" = live-desktop-test ]; then image=target/thos-live-lock-wait-2026-10-16.img; export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 date --iso-8601=seconds
 result=0
 bash /tmp/thos-one-case-2026-10-16.sh "$image" "$name" > "/tmp/thos-lock-wait-$name-2026-10-16.log" 2>&1 || result=$?
 if [ -f "target/$tag-serial.log" ]; then cp "target/$tag-serial.log" "docs/thos/logs/2026-10-16/lock-wait-$name-serial.log"; fi
 cp "/tmp/thos-lock-wait-$name-2026-10-16.log" "docs/thos/logs/2026-10-16/lock-wait-$name-case.log"
 echo "$name exit=$result"
 tail -3 "/tmp/thos-lock-wait-$name-2026-10-16.log"
 date --iso-8601=seconds
 test "$result" = 0
done
