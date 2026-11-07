#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in windyn-probe:windyn:cache-old winlocale-probe:winlocale:old windyn-test:windyn:cache winlocale-test:winlocale:locale asus-installer-probe:asus-installer:asus winlocale-test:winlocale:westmere windyn-test:windyn:cache-westmere; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 seed=target/thos-locale-qa-2026-10-24.img
 if [ "$label" = old ] || [ "$label" = cache-old ]; then seed=target/thos-loader-qa-2026-10-24.img; fi
 export THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [[ "$label" == *westmere* ]]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh "$seed" "$case_name" > "docs/thos/logs/2026-10-24/locale-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-24/locale-$label-serial.log"
 if [ -f "target/$tag-finish.ppm" ]; then convert "target/$tag-finish.ppm" "docs/thos/screenshots/2026-10-24/locale-$label.png"; fi
 printf '%s completed\n' "$label"
done
