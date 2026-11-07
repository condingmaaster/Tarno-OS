#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
export PATH=/usr/sbin:$PATH
export THOS_PREBUILT_IMG=target/thos-multipointer-normal-qa-2026-10-16.img THOS_MEM=2048M THOS_SMP=4 THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb -nic none'
for item in desk-native-resize-test:native-resize desk-winresize-test:winresize; do
 name=${item%:*}; tag=${item#*:}
 if ps -eo comm= | awk '$0 ~ /^qemu/ {found=1} END {exit !found}'; then echo 'Refusing overlapping QEMU'; exit 201; fi
 date --iso-8601=seconds
 cargo xtask "$name" > "/tmp/thos-normal-$name-2026-10-16.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-16/normal-$name-serial.log"
 cp "/tmp/thos-normal-$name-2026-10-16.log" "docs/thos/logs/2026-10-16/normal-$name-case.log"
 tail -2 "/tmp/thos-normal-$name-2026-10-16.log"
 date --iso-8601=seconds
done
