#!/bin/sh
# Lets spd_dump/libc_server open the phone in BROM mode (1782:4d00) without root
# and keeps ModemManager from probing it during the boot handshake.
# Run: sudo sh sudo/01_udev_spd.sh
set -e
RULE=/etc/udev/rules.d/80-spd.rules
cat > $RULE <<'EOF'
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1782", ATTRS{idProduct}=="4d00", MODE="0666", TAG+="uaccess", ENV{ID_MM_DEVICE_IGNORE}="1"
EOF
udevadm control --reload-rules
echo "installed $RULE:"
cat $RULE
