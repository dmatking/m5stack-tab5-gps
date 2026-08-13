#!/usr/bin/env python3
# Copyright 2025-2026 David M. King
# SPDX-License-Identifier: Apache-2.0
#
# PC-side client for main/fb_capture.c -- grabs whatever's currently on an
# LVGL-driven screen (Home/Telemetry/Goto/Settings) and saves it as a PNG.
# Replaces photographing the panel for debugging.
#
# Two steps, both over the same USB-Serial-JTAG connection already used for
# flashing/monitoring:
#   1. A short "SNAP GET" command tells the running app to capture the
#      screen and write it to the "snapshot" flash partition (see
#      partitions.csv). Short, single-write exchange -- proven reliable.
#   2. `python -m esptool read_flash` pulls that partition off, the same
#      proven bootloader-mode path already used for flashing. This resets
#      the board into the ROM bootloader and back (esptool's normal
#      before/after reset behavior) -- the app pauses for that ~second, not
#      indefinitely.
#
# An earlier version streamed the captured frame back over the USB-Serial-
# JTAG connection directly, live, without touching flash -- it reliably
# wedged on real hardware partway through the transfer (see fb_capture.c's
# file header for the full story). This two-step approach sidesteps that
# entirely by not doing any bulk transfer over that channel at all.
#
# Map screen NOT supported -- see main/fb_capture.h for why; step 1 gets
# "SNAP FAIL map screen not supported yet" if you try.
#
# Usage:
#   python tools/pull_snapshot.py --port COM17
#   python tools/pull_snapshot.py --port COM17 --out screen.png

import argparse
import subprocess
import sys
import time
import zlib

import serial
from PIL import Image

# Must match partitions.csv's "snapshot" entry and main/ui_theme.h's
# UI_SCREEN_W/UI_SCREEN_H.
SNAPSHOT_PART_OFFSET = 0x420000
SNAPSHOT_PART_SIZE = 0x200000
SCREEN_W = 720
SCREEN_H = 1280


def open_no_reset(port_name, baud):
    # Deliberately doesn't reset the board (unlike tools/send_to_sd.py's
    # open_and_reset(), which resets on purpose to reach a dev-tool mode's
    # known boot state) -- this step just sends a short command to the
    # already-running app. Step 2 (esptool) does its own reset, separately,
    # only around the flash read itself.
    ser = serial.Serial()
    ser.port = port_name
    ser.baudrate = baud
    ser.timeout = 1
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


_read_buf = b""


def _poll_line(ser):
    global _read_buf
    while b"\n" not in _read_buf:
        chunk = ser.read(65536)
        if not chunk:
            return None
        _read_buf += chunk
    line, _read_buf = _read_buf.split(b"\n", 1)
    return line.decode(errors="replace").rstrip("\r")


def wait_for(ser, prefixes, timeout):
    if isinstance(prefixes, str):
        prefixes = (prefixes,)
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = _poll_line(ser)
        if line is None:
            continue
        if line.startswith(prefixes):
            return line
        # anything else is interleaved log output -- ignore, same as the
        # device side ignoring anything that doesn't start with "SNAP "
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="COM17")
    ap.add_argument("--baud", type=int, default=115200, help="baud for the SNAP GET command (default 115200)")
    ap.add_argument("--flash-baud", type=int, default=460800, help="baud for the esptool flash read (default 460800)")
    ap.add_argument("--out", default=None, help="output PNG path (default: capture_<timestamp>.png)")
    ap.add_argument("--timeout", type=float, default=15, help="seconds to wait for the SNAP response (default 15)")
    args = ap.parse_args()

    out_path = args.out or time.strftime("capture_%Y%m%d_%H%M%S.png")

    print("Requesting capture...", flush=True)
    ser = open_no_reset(args.port, args.baud)
    ser.write(b"SNAP GET\n")
    result = wait_for(ser, ("SNAP OK", "SNAP FAIL"), timeout=args.timeout)
    ser.close()

    if result is None:
        print("ERROR: no response -- is the device running fb_capture-enabled firmware "
              "and connected on the right port?", file=sys.stderr, flush=True)
        sys.exit(1)
    if result.startswith("SNAP FAIL"):
        print(f"FAILED: {result}", file=sys.stderr, flush=True)
        sys.exit(1)

    _, _, w_s, h_s, size_s, crc_s = result.split()
    w, h, size, expected_crc = int(w_s), int(h_s), int(size_s), int(crc_s, 16)
    print(f"Captured {w}x{h}, {size} bytes, crc32={crc_s} -- reading it off flash...", flush=True)

    raw_path = out_path + ".raw.tmp"
    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", "esp32p4",
        "--port", args.port,
        "-b", str(args.flash_baud),
        "read_flash", str(SNAPSHOT_PART_OFFSET), str(SNAPSHOT_PART_SIZE), raw_path,
    ]
    subprocess.run(cmd, check=True)

    with open(raw_path, "rb") as f:
        data = f.read(size)
    import os
    os.remove(raw_path)

    actual_crc = zlib.crc32(data) & 0xFFFFFFFF
    if actual_crc != expected_crc:
        print(f"ERROR: CRC mismatch, got {actual_crc:08x} expected {expected_crc:08x}",
              file=sys.stderr, flush=True)
        sys.exit(1)

    img = Image.frombuffer("RGB", (w, h), data, "raw", "BGR;16", 0, 1)
    img.save(out_path)
    print(f"OK -- saved {out_path}", flush=True)


if __name__ == "__main__":
    main()
