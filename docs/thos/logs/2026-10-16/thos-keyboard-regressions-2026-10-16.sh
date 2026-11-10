#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
export THOS_MEM=2048M THOS_QEMU_EXTRA='-accel kvm -cpu host'
for item in wayland-keyboard-test:wayland-keyboard wayland-input-test:wayland-input wayland-subsurface-test:subsurface desk-native-resize-test:native-resize desk-winresize-test:winresize gtk-input-test:gtk-input firefox-downloads-test:firefox-downloads live-desktop-test:live-desktop; do
 name=${item%:*};tag=${item#*:};image=target/thos-keyboard-normal-qa-2026-10-16.img
 export THOS_MEM=2048M THOS_QEMU_EXTRA='-accel kvm -cpu host'
 if [ "$name" = wayland-keyboard-test ]; then export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 if [ "$name" = gtk-input-test ] || [ "$name" = firefox-downloads-test ]; then image=target/thos-keyboard-browser-qa-2026-10-16.img; fi
 if [ "$name" = firefox-downloads-test ]; then export THOS_MEM=4096M; fi
 if [ "$name" = live-desktop-test ]; then image=target/thos-live-keyboard-2026-10-16.img; export THOS_QEMU_EXTRA='-cpu Westmere,-pdpe1gb'; fi
 date --iso-8601=seconds
 result=0
 bash /tmp/thos-one-case-2026-10-16.sh "$image" "$name" > "/tmp/thos-keyboard-$name-2026-10-16.log" 2>&1 || result=$?
 if [ -f "target/$tag-serial.log" ]; then cp "target/$tag-serial.log" "docs/thos/logs/2026-10-16/keyboard-$name-serial.log"; fi
 cp "/tmp/thos-keyboard-$name-2026-10-16.log" "docs/thos/logs/2026-10-16/keyboard-$name-case.log"
 echo "$name exit=$result"
 tail -3 "/tmp/thos-keyboard-$name-2026-10-16.log"
 date --iso-8601=seconds
 test "$result" = 0
done
