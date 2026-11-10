#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
if ps -eo comm= | awk '$0 ~ /^qemu/ {found=1} END {exit !found}'; then exit 201; fi
PATH=/usr/sbin:$PATH THOS_PREBUILT_IMG=target/thos-readonly-qa-2026-10-16.img THOS_MEM=2048M THOS_SMP=4 THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb' cargo xtask readonly-file-test > /tmp/thos-readonly-westmere-2026-10-16.log 2>&1
cp target/readonly-file-serial.log docs/thos/logs/2026-10-16/readonly-westmere-serial.log
cp /tmp/thos-readonly-westmere-2026-10-16.log docs/thos/logs/2026-10-16/readonly-westmere-case.log
tail -2 /tmp/thos-readonly-westmere-2026-10-16.log
