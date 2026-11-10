#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in gdi-dcbrush-test:gdi-dcbrush:westmere gdi-blit-test:gdi-blit:blit gdi-lifetime-test:gdi-lifetime:lifetime desk-winresize-test:winresize:resize; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash /tmp/thos-one-case-2026-10-16.sh target/thos-dcbrush-qa-2026-10-17.img "$case_name" > "docs/thos/logs/2026-10-17/dcbrush-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-17/dcbrush-$label-serial.log"
 if [ -f "target/$tag-complete.ppm" ]; then convert "target/$tag-complete.ppm" "docs/thos/screenshots/2026-10-17/dcbrush-$label.png"; fi
 printf '%s %s PASS\n' "$case_name" "$label"
done
