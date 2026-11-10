#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for label in long cap; do
 export THOS_QEMU_EXTRA='-accel kvm -cpu host' THOS_LDR_COUNT=20
 if [ "$label" = cap ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb' THOS_LDR_COUNT=62; fi
 bash tools/qemu-case.sh target/thos-ldr-qa-2026-10-24.img winldr-long-test > "docs/thos/logs/2026-10-24/ldr-$label-runner.log" 2>&1
 cp target/winldr-serial.log "docs/thos/logs/2026-10-24/ldr-$label-serial.log"
 printf '%s PASS\n' "$label"
done
