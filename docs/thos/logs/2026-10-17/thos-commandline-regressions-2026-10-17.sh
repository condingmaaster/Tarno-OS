#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in wargv-test:wargv:westmere wide-main-test:wide-main:wide-main winunicode-test:winunicode:unicode winmodule-test:winmodule:module winlookup-test:winlookup:lookup; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash /tmp/thos-one-case-2026-10-16.sh target/thos-commandline-qa-2026-10-17.img "$case_name" > "docs/thos/logs/2026-10-17/commandline-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-17/commandline-$label-serial.log"
 printf '%s PASS\n' "$case_name"
done
