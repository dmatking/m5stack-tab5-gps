#!/usr/bin/env python3
# Copyright 2025-2026 David M. King
# SPDX-License-Identifier: Apache-2.0
#
# Converts raw RGB565 screenshots saved by the corner-tap hotspot
# (main/ui_common.c's screenshot_hotspot_cb() -> main/fb_capture.c's
# fb_capture_save_to_sd()) into PNGs. Those land on the SD card itself as
# "<SD_MOUNT_POINT>/screenshots/shot_NNNN.bin" -- pull the card (or its
# whole screenshots/ folder) onto this machine first, same as any other
# bulk SD-card transfer in this project, then point this script at the
# folder.
#
# Same raw layout tools/pull_snapshot.py already decodes (tightly packed
# RGB565, no header, UI_SCREEN_W x UI_SCREEN_H -- see main/ui_theme.h),
# just read from a local file instead of pulled over USB.
#
# Usage:
#   python tools/sd_screenshots_to_png.py <folder with shot_*.bin files>
#   python tools/sd_screenshots_to_png.py <folder> --out <png output folder>

import argparse
import os
import sys

from PIL import Image

# Must match main/ui_theme.h's UI_SCREEN_W/UI_SCREEN_H (same constant
# pull_snapshot.py already keys off).
SCREEN_W = 720
SCREEN_H = 1280
EXPECTED_BYTES = SCREEN_W * SCREEN_H * 2


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", help="folder containing shot_*.bin files")
    ap.add_argument("--out", default=None, help="output folder for PNGs (default: same folder)")
    args = ap.parse_args()

    out_dir = args.out or args.folder
    os.makedirs(out_dir, exist_ok=True)

    bin_files = sorted(f for f in os.listdir(args.folder) if f.lower().endswith(".bin"))
    if not bin_files:
        print(f"No .bin files found in {args.folder}", file=sys.stderr)
        sys.exit(1)

    converted = 0
    for name in bin_files:
        src = os.path.join(args.folder, name)
        size = os.path.getsize(src)
        if size != EXPECTED_BYTES:
            print(f"skipping {name}: {size} bytes, expected {EXPECTED_BYTES} "
                  f"({SCREEN_W}x{SCREEN_H} RGB565) -- not a full/matching capture")
            continue

        with open(src, "rb") as f:
            data = f.read()

        img = Image.frombuffer("RGB", (SCREEN_W, SCREEN_H), data, "raw", "BGR;16", 0, 1)
        out_name = os.path.splitext(name)[0] + ".png"
        out_path = os.path.join(out_dir, out_name)
        img.save(out_path)
        print(f"{name} -> {out_path}")
        converted += 1

    print(f"done -- {converted}/{len(bin_files)} converted")


if __name__ == "__main__":
    main()
