#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in windyn-test:windyn:kvm winpolicy-test:winpolicy:policy windyn-test:windyn:westmere winpolicy-test:winpolicy:policy-westmere; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [[ "$label" == *westmere* ]]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh target/thos-loader-reuse-qa-2026-10-24.img "$case_name" > "docs/thos/logs/2026-10-24/reuse-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-24/reuse-$label-serial.log"
 convert "target/$tag-finish.ppm" "docs/thos/screenshots/2026-10-24/reuse-$label.png"
 printf '%s PASS\n' "$label"
done
