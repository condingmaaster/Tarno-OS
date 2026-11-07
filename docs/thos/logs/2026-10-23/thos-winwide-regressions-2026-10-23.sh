#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in winwide-test:winwide:westmere winframe-test:winframe:frame winexports-test:winexports:exports gdi-font-test:gdi-font:font desk-winresize-test:winresize:resize; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh target/thos-winwide-qa-2026-10-23.img "$case_name" > "docs/thos/logs/2026-10-23/winwide-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-23/winwide-$label-serial.log"
 printf '%s PASS\n' "$case_name"
done
