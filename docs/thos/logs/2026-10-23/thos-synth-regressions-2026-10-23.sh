#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in winexports-test:winexports:westmere winlookup-test:winlookup:lookup wargv-test:wargv:wargv winframe-test:winframe:frame gdi-font-test:gdi-font:font desk-winresize-test:winresize:resize; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh target/thos-synth-qa-2026-10-23.img "$case_name" > "docs/thos/logs/2026-10-23/synth-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-23/synth-$label-serial.log"
 printf '%s PASS\n' "$case_name"
done
