#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for label in old fixed asus; do
 seed=target/thos-console-qa-2026-10-24.img
 case_name=winconsole-test
 tag=winconsole
 if [ "$label" = old ]; then seed=target/thos-pinned-import-qa-2026-10-24.img; case_name=winconsole-probe; fi
 if [ "$label" = asus ]; then case_name=asus-installer-probe; tag=asus-installer; fi
 bash tools/qemu-case.sh "$seed" "$case_name" > "docs/thos/logs/2026-10-24/console-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-24/console-$label-serial.log"
 if [ -f "target/$tag-finish.ppm" ]; then convert "target/$tag-finish.ppm" "docs/thos/screenshots/2026-10-24/console-$label.png"; fi
 printf '%s completed\n' "$label"
done
