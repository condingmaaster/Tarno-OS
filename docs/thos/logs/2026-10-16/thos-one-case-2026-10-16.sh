#!/bin/bash
set -eu
cd /home/router/Projekte/Tarno-OS
exec 9>target/thos-qemu-tests.lock
flock -n 9 || exit 202
if ps -eo comm= | awk '$0 ~ /^qemu/ {found=1} END {exit !found}'; then echo 'Refusing overlapping QEMU'; exit 201; fi
exec env PATH=/usr/sbin:$PATH THOS_PREBUILT_IMG="$1" THOS_MEM="${THOS_MEM:-2048M}" THOS_SMP=4 THOS_QEMU_EXTRA="${THOS_QEMU_EXTRA:--accel kvm -cpu host}" cargo xtask "$2"
