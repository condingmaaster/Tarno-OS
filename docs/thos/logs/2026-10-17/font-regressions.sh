#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in gdi-font-test:gdi-font:kvm gdi-font-test:gdi-font:westmere gdi-alpha-test:gdi-alpha:alpha gdi-dcbrush-test:gdi-dcbrush:dcbrush desk-winfocus-panel-test:winfocus:focus windows-snake-probe:windows-snake:snake; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash /tmp/thos-one-case-2026-10-16.sh target/thos-font-qa-2026-10-17.img "$case_name" > "docs/thos/logs/2026-10-17/font-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-17/font-$label-serial.log"
 if [ -f "target/$tag-complete.ppm" ]; then convert "target/$tag-complete.ppm" "docs/thos/screenshots/2026-10-17/font-$label.png"; fi
 printf '%s %s PASS\n' "$case_name" "$label"
done
