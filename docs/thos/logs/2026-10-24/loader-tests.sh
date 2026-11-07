#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in winpolicy-probe:winpolicy:old winpolicy-test:winpolicy:policy winbare-test:winbare:bare asus-installer-probe:asus-installer:asus windyn-test:windyn:westmere winpolicy-test:winpolicy:policy-westmere winconsole-test:winconsole:console-westmere pefail-test:pefail:pefail-westmere winldr-long-test:winldr:cap winc-test:winc:crt winlookup-test:winlookup:lookup; do
 case_name=${item%%:*};rest=${item#*:};tag=${rest%:*};label=${rest##*:}
 seed=target/thos-loader-qa-2026-10-24.img
 if [ "$label" = old ]; then seed=target/thos-console-qa-2026-10-24.img; fi
 export THOS_QEMU_EXTRA='-accel kvm -cpu host' THOS_LDR_COUNT=62
 if [[ "$label" == *westmere* ]] || [ "$label" = cap ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash tools/qemu-case.sh "$seed" "$case_name" > "docs/thos/logs/2026-10-24/loader-$label-runner.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-24/loader-$label-serial.log"
 if [ -f "target/$tag-finish.ppm" ]; then convert "target/$tag-finish.ppm" "docs/thos/screenshots/2026-10-24/loader-$label.png"; fi
 printf '%s completed\n' "$label"
done
