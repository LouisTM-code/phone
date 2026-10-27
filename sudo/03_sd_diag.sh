#!/bin/sh
# Read-only: shows what is really in the first sectors of the BQ-3590 SD card.
# Run: sudo sh sudo/03_sd_diag.sh
DISK=
for d in /dev/sd?; do
	if udevadm info -q property -n $d | grep -q '^ID_VENDOR_ID=1782$' &&
	   [ $(blockdev --getsize64 $d) -gt 1000000000 ]; then DISK=$d; fi
done
[ -n "$DISK" ] || { echo "BQ-3590 SD card not found (USB storage mode?)"; exit 1; }
echo "== $DISK sector 0 (direct read)"
dd if=$DISK bs=512 count=1 iflag=direct status=none | xxd | tail -6
echo "== sector 2048 (where the FAT32 partition starts)"
dd if=$DISK bs=512 skip=2048 count=1 iflag=direct status=none | xxd | head -6
echo "== sfdisk"
sfdisk -d $DISK 2>&1
