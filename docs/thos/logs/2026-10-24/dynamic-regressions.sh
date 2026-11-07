#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in windyn-test:windyn:westmere winldr-long-test:winldr:cap winmodule-test:winmodule:module winlookup-test:winlookup:lookup winexports-test:winexports:exports winunicode-test:winunicode:unicode winenv-test:winenv:env winc-test:winc:crt; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 export THOS_QEMU_EXTRA='-accel kvm -cpu host' THOS_LDR_COUNT=62
 if [ "$label" = westmere ] || [ "$label" = cap ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh target/thos-dynamic-qa-2026-10-24.img "$case_name" > "docs/thos/logs/2026-10-24/dynamic-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-24/dynamic-$label-serial.log"
 if [ -f "target/$tag-finish.ppm" ]; then convert "target/$tag-finish.ppm" "docs/thos/screenshots/2026-10-24/dynamic-$label.png"; fi
 printf '%s PASS\n' "$case_name"
done
