#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in winexportbounds-test:winexportbounds:westmere winlookup-test:winlookup:lookup winexports-test:winexports:exports winwide-test:winwide:wide wide-main-test:wide-main:crt asus-installer-probe:asus-installer:asus windows-snake-probe:windows-snake:snake; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh target/thos-exportbounds-qa-2026-10-23.img "$case_name" > "docs/thos/logs/2026-10-23/exportbounds-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-23/exportbounds-$label-serial.log"
 printf '%s PASS\n' "$case_name"
done
