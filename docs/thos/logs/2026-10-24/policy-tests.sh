#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for label in old fixed; do
 seed=target/thos-policy-qa-2026-10-24.img
 case_name=winpolicy-test
 if [ "$label" = old ]; then seed=target/thos-dynamic-qa-2026-10-24.img; case_name=winpolicy-probe; fi
 bash tools/qemu-case.sh "$seed" "$case_name" > "docs/thos/logs/2026-10-24/policy-$label-runner.log" 2>&1
 cp target/winpolicy-serial.log "docs/thos/logs/2026-10-24/policy-$label-serial.log"
 convert target/winpolicy-finish.ppm "docs/thos/screenshots/2026-10-24/policy-$label.png"
 printf '%s completed\n' "$label"
done
