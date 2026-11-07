#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
export PATH=/usr/sbin:$PATH THOS_MEM=2048M THOS_SMP=4 THOS_QEMU_EXTRA='-accel kvm -cpu host'
for variant in before after; do
 if ps -eo comm= | awk '$0 ~ /^qemu/ {found=1} END {exit !found}'; then exit 201; fi
 image=target/thos-multipointer-normal-qa-2026-10-16.img
 if [ "$variant" = after ]; then image=target/thos-flock-qa-2026-10-16.img; fi
 result=0
 THOS_PREBUILT_IMG=$image cargo xtask flock-test > "/tmp/thos-flock-$variant-2026-10-16.log" 2>&1 || result=$?
 cp target/flock-serial.log "docs/thos/logs/2026-10-16/flock-$variant-serial.log"
 cp "/tmp/thos-flock-$variant-2026-10-16.log" "docs/thos/logs/2026-10-16/flock-$variant-case.log"
 echo "$variant exit=$result"
 tail -2 "/tmp/thos-flock-$variant-2026-10-16.log"
 if [ "$variant" = before ]; then grep -q 'flock-missing errno=38' target/flock-serial.log; else test "$result" = 0; fi
done
