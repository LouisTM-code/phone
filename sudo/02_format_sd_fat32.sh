#!/bin/sh
# Formats the SD card inside the BQ-3590 (phone in USB storage mode) as FAT32
# in the first MBR partition, as fpdoom's microfat requires. ERASES THE CARD.
# Run: sudo sh sudo/02_format_sd_fat32.sh
set -e
DISK=
for d in /dev/sd?; do
	# the phone shows its internal 51 KB disk too; pick the large one
	if udevadm info -q property -n $d | grep -q '^ID_VENDOR_ID=1782$' &&
	   [ $(blockdev --getsize64 $d) -gt 1000000000 ]; then DISK=$d; fi
done
[ -n "$DISK" ] || { echo "BQ-3590 SD card not found (USB storage mode?)"; exit 1; }
PART=${DISK}1
echo "card: $DISK, $(( $(blockdev --getsize64 $DISK) / 1000000000 )) GB"
printf "Erase it and format $PART as FAT32? [y/N] "; read a; [ "$a" = y ] || exit 1

umount $DISK* 2>/dev/null || true
wipefs -a $DISK
echo 'type=c' | sfdisk $DISK
# the phone's storage is slow to report the new partition
blockdev --rereadpt $DISK || true
udevadm settle
i=0; while [ ! -b $PART ] && [ $i -lt 10 ]; do sleep 1; i=$((i + 1)); done
[ -b $PART ] || { echo "$PART did not appear: replug the phone and run again"; exit 1; }
mkfs.vfat -F 32 -n BQ3590 $PART
sync
echo done
