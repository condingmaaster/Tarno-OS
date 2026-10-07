#!/usr/bin/env bash
# Write the THOS image to a USB stick and grow the root file system to fill the stick.
#   tools/make-stick.sh /dev/sdX [image]        (default image: target/thos-bios.img)
# Needs root (sudo) and: dd lsblk sfdisk e2fsck resize2fs partprobe.
# THOS only ever uses the ext2 volume labelled THOSROOT, so the rest of the stick is simply its disk space.
set -euo pipefail
DEV="${1:-}"
IMG="${2:-$(dirname "$0")/../target/thos-bios.img}"
[ -n "$DEV" ] || { echo "usage: $0 /dev/sdX [image]   (the STICK: check with lsblk first)"; exit 1; }
[ -b "$DEV" ] || { echo "$DEV is not a block device"; exit 1; }
[ -f "$IMG" ] || { echo "image $IMG not found (build it: cargo xtask bios-image --interactive)"; exit 1; }
[ "$(id -u)" = 0 ] || { echo "run as root: sudo $0 $*"; exit 1; }

# safety: only removable disks, never one that holds a mounted file system
case "$DEV" in *[0-9]) echo "give the whole disk (e.g. /dev/sdb), not a partition"; exit 1;; esac
RM="$(lsblk -dno RM "$DEV" 2>/dev/null | tr -d ' ')"
SIZE="$(lsblk -bdno SIZE "$DEV")"
MODEL="$(lsblk -dno VENDOR,MODEL "$DEV" | sed 's/  */ /g')"
if [ "$RM" != "1" ]; then echo "$DEV ($MODEL) is not marked removable - refusing. (Check lsblk; use FORCE=1 only if you are sure.)"; [ "${FORCE:-}" = 1 ] || exit 1; fi
if lsblk -no MOUNTPOINTS "$DEV" | grep -q .; then echo "something on $DEV is mounted - unmount it first"; exit 1; fi
IMGSIZE="$(stat -c %s "$IMG")"
[ "$SIZE" -gt "$IMGSIZE" ] || { echo "the stick ($SIZE bytes) is smaller than the image ($IMGSIZE bytes)"; exit 1; }

echo "Erasing $DEV: $MODEL, $((SIZE / 1000000000)) GB (removable, nothing mounted: checked)"
lsblk "$DEV"

# Growing the file system ON a USB stick is hopelessly slow (thousands of small writes at about 60 KB/s), so the root partition is grown
# inside a sparse copy of the image on this computer's disk and the finished image is then written to the stick in one sequential pass.
ROOT_MIB="${STICK_ROOT_MIB:-16384}"                                  # root size; more of the stick can be used later
MAX_MIB=$((SIZE / 1048576 - 8))
[ "$ROOT_MIB" -le "$MAX_MIB" ] || ROOT_MIB="$MAX_MIB"
WORK="$(mktemp "$(dirname "$IMG")/stick-work.XXXXXX")"
trap 'rm -f "$WORK"' EXIT
echo "== growing a copy of the image to $ROOT_MIB MiB (on this computer, fast)"
cp --sparse=always "$IMG" "$WORK"
"$(dirname "$0")/grow-image.sh" "$WORK" "$ROOT_MIB"
echo "== writing it to the stick ($ROOT_MIB MiB)"
dd if="$WORK" of="$DEV" bs=4M conv=fsync status=progress
sync
partprobe "$DEV" || true
echo "== done. Put the stick in the Acer, press F12 at power-on and choose the stick (Legacy/BIOS boot)."
lsblk -o NAME,SIZE,FSTYPE,LABEL "$DEV"
