#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in wide-main-test:wide-main winunicode-test:winunicode winmodule-test:winmodule winlookup-test:winlookup winc-test:winc; do
 case_name=${item%:*};tag=${item#*:}
 bash tools/qemu-case.sh target/thos-commandline-qa-2026-10-17.img "$case_name" > "docs/thos/logs/2026-10-23/runtime-$tag-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-23/runtime-$tag-serial.log"
 printf '%s PASS\n' "$case_name"
done
