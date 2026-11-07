#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in before:winmodule-probe westmere:winmodule-test args:winargs-test lookup:winlookup-test; do
 label=${item%:*};testcase=${item#*:};image=target/thos-module-qa-2026-10-16.img
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = before ]; then image=target/thos-lock-wait-qa-2026-10-16.img; fi
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash /tmp/thos-one-case-2026-10-16.sh "$image" "$testcase" > "/tmp/thos-module-$label-2026-10-16.log" 2>&1
 tag=winmodule
 if [ "$label" = args ]; then tag=winargs; fi
 if [ "$label" = lookup ]; then tag=winlookup; fi
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-16/module-$label-serial.log"
 cp "/tmp/thos-module-$label-2026-10-16.log" "docs/thos/logs/2026-10-16/module-$label-case.log"
 echo "$label PASS"
done
