#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Run one isolated case. Use a host context that can see existing QEMU processes.
set -eu
# Failed host assertions must not leave large core dumps on the SSD.
ulimit -c 0
cd "$(dirname "$0")/.."
if [ "$#" -ne 2 ]; then
    echo "usage: bash tools/qemu-case.sh SEED_IMAGE CASE" >&2
    exit 2
fi
exec 9>target/thos-qemu-tests.lock
flock -n 9 || exit 202
if ps -eo comm= | awk '$0 ~ /^qemu/ {found=1} END {exit !found}'; then
    echo 'Refusing overlapping QEMU' >&2
    exit 201
fi
exec env PATH="/usr/sbin:$PATH" THOS_PREBUILT_IMG="$1" THOS_MEM="${THOS_MEM:-2048M}" THOS_SMP="${THOS_SMP:-4}" THOS_QEMU_EXTRA="${THOS_QEMU_EXTRA:--accel kvm -cpu host}" cargo xtask "$2"
