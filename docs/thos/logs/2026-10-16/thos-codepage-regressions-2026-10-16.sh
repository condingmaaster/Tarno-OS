#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in winunicode-test:winunicode:after winunicode-test:winunicode:westmere winmodule-test:winmodule:module winlookup-test:winlookup:lookup winc-test:winc:crt live-desktop-test:live-desktop:live; do
 testcase=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:};image=target/thos-codepage-normal-qa-2026-10-16.img
 export THOS_MEM=2048M THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 if [ "$label" = live ]; then image=target/thos-live-codepage-2026-10-16.img;export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 result=0
 bash /tmp/thos-one-case-2026-10-16.sh "$image" "$testcase" > "/tmp/thos-codepage-$label-2026-10-16.log" 2>&1 || result=$?
 if [ -f "target/$tag-serial.log" ]; then cp "target/$tag-serial.log" "docs/thos/logs/2026-10-16/codepage-$label-serial.log"; fi
 cp "/tmp/thos-codepage-$label-2026-10-16.log" "docs/thos/logs/2026-10-16/codepage-$label-case.log"
 echo "$testcase $label exit=$result"
 test "$result" = 0
done
