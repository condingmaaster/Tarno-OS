#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in winframe-test:winframe:westmere gdi-pen-test:gdi-pen:pen gdi-font-test:gdi-font:font gdi-blit-test:gdi-blit:blit gdi-alpha-test:gdi-alpha:alpha gdi-dcbrush-test:gdi-dcbrush:brush desk-winresize-test:winresize:resize desk-winfocus-panel-test:winfocus:focus winlookup-test:winlookup:lookup windows-snake-probe:windows-snake:snake asus-installer-probe:asus-installer:asus; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh target/thos-frame-qa-2026-10-23.img "$case_name" > "docs/thos/logs/2026-10-23/frame-$label-runner.log" 2>&1
 if [ -f "target/$tag-serial.log" ]; then cp "target/$tag-serial.log" "docs/thos/logs/2026-10-23/frame-$label-serial.log"; fi
 printf '%s PASS\n' "$case_name"
done
