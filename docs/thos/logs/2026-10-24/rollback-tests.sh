#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for label in kvm westmere; do
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh target/thos-nls-qa-2026-10-24.img winrollback-test > "docs/thos/logs/2026-10-24/rollback-$label-runner.log" 2>&1
 cp target/winrollback-serial.log "docs/thos/logs/2026-10-24/rollback-$label-serial.log"
 convert target/winrollback-finish.ppm "docs/thos/screenshots/2026-10-24/rollback-$label.png"
 printf '%s completed\n' "$label"
done
