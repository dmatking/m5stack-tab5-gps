#!/bin/bash
# Copyright 2025-2026 David M. King
# SPDX-License-Identifier: Apache-2.0
#
# Cleanly unmount the SD card mounted by sdmount.sh. Always run this before
# pulling the card or handing it back to the Tab5: a rw mount can leave
# buffered writes unflushed and FAT's dirty bit set until a clean unmount,
# and skipping it risks the same "sudden power loss, half-written state"
# problem gps.c's fsync() fix was added to avoid on the device side --
# no reason to reintroduce that from the Linux side instead.
set -euo pipefail

MOUNT_POINT="$HOME/hardware/sdcard_mount"

if ! mountpoint -q "$MOUNT_POINT"; then
    echo "$MOUNT_POINT is not mounted -- nothing to do."
    exit 0
fi

sync

if err=$(sudo umount "$MOUNT_POINT" 2>&1); then
    echo "Unmounted $MOUNT_POINT -- safe to remove the card."
    exit 0
fi

# Busiest known cause on this box: smbd holds the path open because it's
# also served out to Windows as H:\sdcard_mount -- a client having browsed
# it is enough to pin the mount, with no active transfer required.
echo "$err" >&2
echo >&2
echo "Unmount failed -- checking what's holding $MOUNT_POINT open:" >&2
fuser_out=$(sudo fuser -vm "$MOUNT_POINT" 2>&1) || true
echo "$fuser_out" >&2

if echo "$fuser_out" | grep -qi smbd; then
    echo >&2
    echo "smbd (Samba, serving this path as H:\ on the Windows side) is holding it -- restarting smbd and retrying." >&2
    sudo systemctl restart smbd
    sleep 1
    if sudo umount "$MOUNT_POINT"; then
        echo "Unmounted $MOUNT_POINT -- safe to remove the card."
        exit 0
    fi
fi

echo >&2
echo "Still busy. Close any Explorer/PowerShell window with H:\sdcard_mount open on the Windows side, then run this again." >&2
exit 1
