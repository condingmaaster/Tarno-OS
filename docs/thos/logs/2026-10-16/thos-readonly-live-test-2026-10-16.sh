#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
if ps -eo comm= | awk '$0 ~ /^qemu/ {found=1} END {exit !found}'; then exit 201; fi
THOS_PREBUILT_IMG=target/thos-live-readonly-2026-10-16.img THOS_MEM=2048M THOS_SMP=4 THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb' cargo xtask live-desktop-test > /tmp/thos-readonly-live-test-2026-10-16.log 2>&1
cp target/live-desktop-serial.log docs/thos/logs/2026-10-16/readonly-live-serial.log
cp /tmp/thos-readonly-live-test-2026-10-16.log docs/thos/logs/2026-10-16/readonly-live-case.log
convert target/live-desktop-finish.ppm docs/thos/screenshots/2026-10-16/readonly-live-desktop.png
tail -2 /tmp/thos-readonly-live-test-2026-10-16.log
