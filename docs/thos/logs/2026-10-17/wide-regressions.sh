#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in wargv-test:wargv:kvm wargv-test:wargv:westmere wide-main-test:wide-main:wide-main winunicode-test:winunicode:unicode winmodule-test:winmodule:module winlookup-test:winlookup:lookup winc-test:winc:winc windows-snake-probe:windows-snake:snake; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash /tmp/thos-one-case-2026-10-16.sh target/thos-wide-qa-2026-10-17.img "$case_name" > "docs/thos/logs/2026-10-17/wide-$label-runner.log" 2>&1
 if [ -f "target/$tag-serial.log" ]; then cp "target/$tag-serial.log" "docs/thos/logs/2026-10-17/wide-$label-serial.log"; fi
 printf '%s %s PASS\n' "$case_name" "$label"
done
