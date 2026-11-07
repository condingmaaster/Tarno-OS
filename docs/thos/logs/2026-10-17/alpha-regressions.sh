#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in gdi-alpha-test:gdi-alpha:westmere gdi-blit-test:gdi-blit:blit winlookup-test:winlookup:lookup live-desktop-test:live-desktop:dc-live; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:};img=target/thos-alpha-qa-2026-10-17.img
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 if [ "$label" = dc-live ]; then img=target/thos-live-dcbrush-2026-10-17.img;export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash /tmp/thos-one-case-2026-10-16.sh "$img" "$case_name" > "docs/thos/logs/2026-10-17/alpha-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-17/alpha-$label-serial.log"
 if [ -f "target/$tag-complete.ppm" ]; then convert "target/$tag-complete.ppm" "docs/thos/screenshots/2026-10-17/alpha-$label.png"; fi
 printf '%s %s PASS\n' "$case_name" "$label"
done
