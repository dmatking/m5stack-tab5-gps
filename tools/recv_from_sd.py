#!/usr/bin/env python3
# Copyright 2025-2026 David M. King
# SPDX-License-Identifier: Apache-2.0
#
# PC-side receiver for the SD_XFER_MODE firmware (main/sd_xfer.c) -- pulls a
# file off the Tab5's SD card back to the PC, the reverse of send_to_sd.py.
# Same line protocol, same COM17/USB-Serial-JTAG connection and RTS/DTR
# open/reset sequencing (see send_to_sd.py's open_and_reset() for why).
#
# Usage:
#   python tools/recv_from_sd.py gps_log.txt --port COM17
#   python tools/recv_from_sd.py gps_log.txt --port COM17 --out my_log.txt

import argparse
import base64
import os
import sys
import time
import zlib

from send_to_sd import open_and_reset, hard_reset, wait_for, _poll_line


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", help="filename on the SD card")
    ap.add_argument("--port", default="COM17")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default=None, help="local output path (default: same basename)")
    ap.add_argument("--no-reset", action="store_true", help="assume the board is already at XFER READY")
    args = ap.parse_args()

    out_path = args.out or os.path.basename(args.file)

    ser = open_and_reset(args.port, args.baud)
    if not args.no_reset:
        print("Resetting board...", flush=True)
        hard_reset(ser)

    print("Waiting for XFER READY...", flush=True)
    if wait_for(ser, "XFER READY", timeout=15) is None:
        print("ERROR: never saw XFER READY -- is the board running the SD_XFER_MODE firmware?", file=sys.stderr, flush=True)
        sys.exit(1)

    ser.write(f"XFER GET {args.file}\n".encode())

    print("Waiting for XFER SIZE...", flush=True)
    result = wait_for(ser, ("XFER SIZE", "XFER FAIL"), timeout=15)
    if result is None:
        print("ERROR: no response to XFER GET", file=sys.stderr, flush=True)
        sys.exit(1)
    if result.startswith("XFER FAIL"):
        print(f"FAILED: {result}", file=sys.stderr, flush=True)
        sys.exit(1)

    total = int(result.split()[2])
    print(f"{args.file}: {total} bytes -> {out_path}", flush=True)

    data = bytearray()
    crc = 0
    start = time.time()
    while True:
        line = _poll_line(ser)
        if line is None:
            continue
        if line.startswith("XFER DATA "):
            chunk = base64.b64decode(line[10:])
            data += chunk
            crc = zlib.crc32(chunk, crc)
            pct = 100.0 * len(data) / total if total else 100.0
            elapsed = time.time() - start
            rate = len(data) / elapsed / 1024 if elapsed > 0 else 0
            print(f"  {len(data)}/{total} bytes ({pct:.1f}%)  avg={rate:.1f} KB/s", flush=True)
        elif line.startswith("XFER END "):
            expected_crc = int(line[9:], 16)
            ser.close()
            if len(data) != total:
                print(f"ERROR: size mismatch, got {len(data)} expected {total}", file=sys.stderr, flush=True)
                sys.exit(1)
            if (crc & 0xFFFFFFFF) != expected_crc:
                print(f"ERROR: CRC mismatch, got {crc & 0xFFFFFFFF:08x} expected {expected_crc:08x}", file=sys.stderr, flush=True)
                sys.exit(1)
            with open(out_path, "wb") as f:
                f.write(data)
            elapsed = time.time() - start
            print(f"OK -- {total} bytes in {elapsed:.1f}s, crc32={expected_crc:08x}", flush=True)
            return
        else:
            print(f"  device: {line}", flush=True)


if __name__ == "__main__":
    main()
