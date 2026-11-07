#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
export PATH=/usr/sbin:$PATH THOS_PREBUILT_IMG=target/thos-readonly-qa-2026-10-16.img THOS_MEM=2048M THOS_SMP=4 THOS_QEMU_EXTRA='-accel kvm -cpu host'
for item in readonly-file-test:readonly-file flock-test:flock memfd-freeze-test:memfd-freeze winlookup-test:winlookup firefox-downloads-test:firefox-downloads; do
 name=${item%:*};tag=${item#*:}
 if ps -eo comm= | awk '$0 ~ /^qemu/ {found=1} END {exit !found}'; then exit 201; fi
 if [ "$name" = firefox-downloads-test ]; then export THOS_MEM=4096M THOS_PREBUILT_IMG=target/thos-browser-readonly-qa-2026-10-16.img; fi
 date --iso-8601=seconds
 result=0
 cargo xtask "$name" > "/tmp/thos-readonly-$name-2026-10-16.log" 2>&1 || result=$?
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-16/readonly-$name-serial.log"
 cp "/tmp/thos-readonly-$name-2026-10-16.log" "docs/thos/logs/2026-10-16/readonly-$name-case.log"
 tail -3 "/tmp/thos-readonly-$name-2026-10-16.log"
 date --iso-8601=seconds
 test "$result" = 0
done
