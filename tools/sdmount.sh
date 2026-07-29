#!/bin/bash
# Copyright 2025-2026 David M. King
# SPDX-License-Identifier: Apache-2.0
#
# Mount the Tab5's microSD card (plugged into a USB card reader on the Pi)
# read-write at ~/hardware/sdcard_mount, so it shows up over the existing
# Samba share as H:\sdcard_mount on the Windows side too -- replaces the
# fragile SD_XFER serial protocol (main/sd_xfer.c, tools/send_to_sd.py /
# recv_from_sd.py) for moving files onto/off the card, now that a reader
# that Linux actually recognizes is available (Windows didn't see it --
# same class of USB flakiness as the mass-storage mode never enumerating).
#
# Always pair with eject.sh before pulling the card or handing it back to
# the Tab5 -- see that script for why.
set -euo pipefail

MOUNT_POINT="$HOME/hardware/sdcard_mount"

# First USB-attached partition -- the card reader, assuming nothing else
# USB-storage is plugged in. Deliberately not `lsblk ... TRAN=usb`: lsblk
# only populates TRAN on the parent disk row (e.g. sda), not on partition
# rows (sda1) -- filtering partitions by TRAN directly never matches
# anything, even when the reader is right there (confirmed on real
# hardware). udevadm reports ID_BUS on partition device nodes correctly,
# so query each partition directly instead.
DEV=""
for part in /dev/sd*[0-9]; do
    [ -b "$part" ] || continue
    if udevadm info --query=property --name="$part" 2>/dev/null | grep -q '^ID_BUS=usb$'; then
        DEV="${part#/dev/}"
        break
    fi
done

if [ -z "$DEV" ]; then
    echo "No USB partition found -- is the card reader plugged in?" >&2
    echo "Current block devices:" >&2
    lsblk -o NAME,TRAN,TYPE,SIZE,FSTYPE,MOUNTPOINT >&2
    exit 1
fi

DEV_PATH="/dev/$DEV"
mkdir -p "$MOUNT_POINT"

if mountpoint -q "$MOUNT_POINT"; then
    echo "$MOUNT_POINT is already mounted:"
    findmnt "$MOUNT_POINT"
    exit 0
fi

sudo mount -o uid="$(id -u)",gid="$(id -g)" "$DEV_PATH" "$MOUNT_POINT"
echo "Mounted $DEV_PATH at $MOUNT_POINT"
ls -la "$MOUNT_POINT"
