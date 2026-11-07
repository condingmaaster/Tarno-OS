#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in winlocale-probe:winlocale:old winlocale-test:winlocale:fixed asus-installer-probe:asus-installer:asus winlocale-test:winlocale:westmere winpolicy-test:winpolicy:policy winbare-test:winbare:bare winlookup-test:winlookup:lookup winenv-test:winenv:env wide-main-test:wide-main:wide windows-snake-probe:windows-snake:snake; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 seed=target/thos-nls-qa-2026-10-24.img
 if [ "$label" = old ]; then seed=target/thos-locale-qa-2026-10-24.img; fi
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$label" = westmere ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh "$seed" "$case_name" > "docs/thos/logs/2026-10-24/nls-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-24/nls-$label-serial.log"
 if [ -f "target/$tag-finish.ppm" ]; then convert "target/$tag-finish.ppm" "docs/thos/screenshots/2026-10-24/nls-$label.png"; fi
 printf '%s completed\n' "$label"
done
