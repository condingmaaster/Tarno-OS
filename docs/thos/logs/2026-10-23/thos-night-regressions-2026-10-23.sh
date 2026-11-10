#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in pewait-test:pewait:kvm pewait-test:pewait:westmere pe-launch-race-test:pe-launch-race:launch winenv-test:winenv:environment winwide-test:winwide:wide winlookup-test:winlookup:lookup winc-test:winc:crt desk-winresize-test:winresize:resize; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh target/thos-night-qa-2026-10-23.img "$case_name" > "docs/thos/logs/2026-10-23/night-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-23/night-$label-serial.log"
 printf '%s PASS\n' "$case_name"
done
