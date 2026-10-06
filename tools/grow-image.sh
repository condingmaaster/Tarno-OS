#!/usr/bin/env bash
# Grow a THOS disk image file (MBR: partition 2 = THOSROOT) to SIZE_MIB, the way make-stick.sh grows a stick: the root partition and its ext2 file system fill the rest.
#   tools/grow-image.sh image.img 6144
set -euo pipefail
IMG="$1"; MIB="$2"
export PATH="$PATH:/sbin:/usr/sbin"
truncate -s "${MIB}M" "$IMG"
echo ",+" | sfdisk -N 2 --no-reread "$IMG" >/dev/null 2>&1
START=$(sfdisk -d "$IMG" | awk '/img2|\.2 :/ {for(i=1;i<=NF;i++) if($i=="start=") {gsub(",","",$(i+1)); print $(i+1)}}')
SIZE=$(sfdisk -d "$IMG" | awk '/img2|\.2 :/ {for(i=1;i<=NF;i++) if($i=="size=") {gsub(",","",$(i+1)); print $(i+1)}}')
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
dd if="$IMG" of="$TMP" bs=512 skip="$START" count="$SIZE" status=none
e2fsck -fy "$TMP" >/dev/null 2>&1 || true
resize2fs "$TMP" >/dev/null 2>&1
dd if="$TMP" of="$IMG" bs=512 seek="$START" conv=notrunc status=none
echo "grown: $IMG is now ${MIB} MiB"
