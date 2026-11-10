#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
for item in wayland-keyboard-test:wayland-keyboard wayland-input-test:wayland-input gtk-input-test:gtk-input desk-native-resize-test:native-resize desk-winresize-test:winresize firefox-downloads-test:firefox-downloads live-desktop-test:live-desktop; do
 testcase=${item%:*};tag=${item#*:};image=target/thos-held-normal-qa-2026-10-16.img
 export THOS_MEM=2048M THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$tag" = wayland-keyboard ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 if [ "$tag" = firefox-downloads ]; then image=target/thos-held-browser-qa-2026-10-16.img;export THOS_MEM=4096M; fi
 if [ "$tag" = live-desktop ]; then image=target/thos-live-held-2026-10-16.img;export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 bash /tmp/thos-one-case-2026-10-16.sh "$image" "$testcase" > "/tmp/thos-held-$tag-2026-10-16.log" 2>&1
 cp "target/$tag-serial.log" "docs/thos/logs/2026-10-16/held-$tag-serial.log"
 cp "/tmp/thos-held-$tag-2026-10-16.log" "docs/thos/logs/2026-10-16/held-$tag-case.log"
 echo "$testcase PASS"
done
