#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for label in old fixed; do
 seed=target/thos-pefail-qa-2026-10-24.img
 case_name=pefail-test
 if [ "$label" = old ]; then seed=target/thos-policy-qa-2026-10-24.img; case_name=pefail-probe; fi
 bash tools/qemu-case.sh "$seed" "$case_name" > "docs/thos/logs/2026-10-24/pefail-$label-runner.log" 2>&1
 cp target/pefail-serial.log "docs/thos/logs/2026-10-24/pefail-$label-serial.log"
 convert target/pefail-finish.ppm "docs/thos/screenshots/2026-10-24/pefail-$label.png"
 printf '%s completed\n' "$label"
done
