#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for label in old fixed westmere; do
 seed=target/thos-pinned-import-qa-2026-10-24.img
 case_name=windyn-test
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = old ]; then seed=target/thos-pefail-qa-2026-10-24.img; case_name=windyn-probe; fi
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh "$seed" "$case_name" > "docs/thos/logs/2026-10-24/pinned-$label-runner.log" 2>&1
 cp target/windyn-serial.log "docs/thos/logs/2026-10-24/pinned-$label-serial.log"
 convert target/windyn-finish.ppm "docs/thos/screenshots/2026-10-24/pinned-$label.png"
 printf '%s completed\n' "$label"
done
