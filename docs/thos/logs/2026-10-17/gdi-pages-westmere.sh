#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'
bash /tmp/thos-one-case-2026-10-16.sh target/thos-blit-qa-2026-10-17.img gdi-blit-test > docs/thos/logs/2026-10-17/pages-westmere-gdi-runner.log 2>&1
cp target/gdi-blit-serial.log docs/thos/logs/2026-10-17/pages-westmere-gdi-serial.log
convert target/gdi-blit-complete.ppm docs/thos/screenshots/2026-10-17/pages-westmere-gdi.png
printf 'GDI Westmere PASS\n'
bash /tmp/thos-one-case-2026-10-16.sh target/thos-live-gdi-pages-2026-10-17.img live-desktop-test > docs/thos/logs/2026-10-17/pages-live-westmere-runner.log 2>&1
cp target/live-desktop-serial.log docs/thos/logs/2026-10-17/pages-live-westmere-serial.log
printf 'Live Westmere PASS\n'
