#!/usr/bin/env python3
# Copyright 2025-2026 David M. King
# SPDX-License-Identifier: Apache-2.0
#
# PC-side sender for the SD_XFER_MODE firmware (main/sd_xfer.c) -- a
# fallback to the USB mass-storage approach (main/usb_msc.c) for copying
# files onto the Tab5's microSD card when that path proved unreliable over
# the available cable. Sends a file as base64-encoded text lines over the
# same COM17/USB-Serial-JTAG connection already used for flashing, with a
# CRC32 check at the end. Text framing means any stray log line the device
# prints gets ignored rather than corrupting the transfer -- only lines
# starting with "XFER " are meaningful.
#
# Requires firmware built with SD_XFER_MODE=1 and the sdkconfig.sdxfer.defaults
# overlay (see main.cmake.extra / sdkconfig.sdxfer.defaults for why).
#
# Usage:
#   python tools/send_to_sd.py tiles.bin --port COM17
#   python tools/send_to_sd.py tiles.bin --port COM17 --remote-name tiles.bin

import argparse
import base64
import sys
import time
import zlib

import serial

RAW_CHUNK_SIZE = 6000  # must match main/sd_xfer.c's RAW_CHUNK_SIZE


def _set_rts(ser, value):
    ser.rts = value
    ser.dtr = ser.dtr  # Windows usbser.sys workaround (see esptool/reset.py)


def open_and_reset(port_name, baud):
    # Mirrors esp_idf_monitor's serial_reader.py open_serial() + reset.py's
    # Reset.hard() exactly -- earlier attempts here (opening normally, then
    # toggling RTS) reliably failed to reach a working post-reset state on
    # this device, even though the same simple toggle worked fine on other
    # firmware builds this session. The difference: opening the port with
    # pyserial's default control-line states can itself glitch the chip's
    # reset/boot-select pins during the open transition. Holding the chip
    # in reset (RTS asserted) *before* the port physically opens, then
    # releasing RTS-before-DTR in a specific order, avoids that.
    ser = serial.Serial()
    ser.port = port_name
    ser.baudrate = baud
    ser.timeout = 1
    ser.rts = True   # EN -> LOW: hold in reset before opening
    ser.dtr = True
    ser.open()
    _set_rts(ser, False)  # EN -> HIGH: out of reset, RTS first to avoid bootloader entry
    ser.dtr = False
    return ser


def hard_reset(ser):
    _set_rts(ser, True)   # EN -> LOW (reset)
    time.sleep(0.1)
    _set_rts(ser, False)  # EN -> HIGH (out of reset)


# Manual buffered line reader on top of raw ser.read() polling, with the
# port's timeout set ONCE (never changed mid-stream). Repeatedly reassigning
# ser.timeout (as an earlier version of this script did, via readline())
# reliably broke reads on this device even though writes kept working --
# never fully diagnosed why, but this fixed-timeout polling pattern is the
# one proven to work in isolated testing, so it's what both read paths use.
_read_buf = b""


def _poll_line(ser):
    global _read_buf
    while b"\n" not in _read_buf:
        chunk = ser.read(4096)
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
        print(f"  device: {line}", flush=True)
        if line.startswith(prefixes):
            return line
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", help="local file to send")
    ap.add_argument("--port", default="COM17")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--remote-name", default=None, help="filename on the SD card (default: same basename)")
    ap.add_argument("--no-reset", action="store_true", help="assume the board is already at XFER READY")
    args = ap.parse_args()

    import os
    remote_name = args.remote_name or os.path.basename(args.file)
    with open(args.file, "rb") as f:
        data = f.read()
    total = len(data)
    n_chunks = (total + RAW_CHUNK_SIZE - 1) // RAW_CHUNK_SIZE
    print(f"{args.file}: {total} bytes, {n_chunks} chunks -> /sdcard/{remote_name}", flush=True)

    ser = open_and_reset(args.port, args.baud)
    if not args.no_reset:
        print("Resetting board...", flush=True)
        hard_reset(ser)

    print("Waiting for XFER READY...", flush=True)
    if wait_for(ser, "XFER READY", timeout=15) is None:
        print("ERROR: never saw XFER READY -- is the board running the SD_XFER_MODE firmware?", file=sys.stderr, flush=True)
        sys.exit(1)

    ser.write(f"XFER BEGIN {remote_name} {total}\n".encode())

    crc = 0
    sent = 0
    start = time.time()
    last_chunk_start = start
    for i, offset in enumerate(range(0, total, RAW_CHUNK_SIZE)):
        chunk_start = time.time()
        chunk = data[offset:offset + RAW_CHUNK_SIZE]
        crc = zlib.crc32(chunk, crc)
        b64 = base64.b64encode(chunk).decode()
        ser.write(f"XFER DATA {b64}\n".encode())
        write_done = time.time()
        sent += len(chunk)
        pct = 100.0 * sent / total
        elapsed = time.time() - start
        rate = sent / elapsed / 1024 if elapsed > 0 else 0
        print(f"  chunk {i+1}/{n_chunks}  {sent}/{total} bytes ({pct:.1f}%)  "
              f"write={write_done - chunk_start:.3f}s  avg={rate:.1f} KB/s", flush=True)
        last_chunk_start = chunk_start

    print(flush=True)
    ser.write(f"XFER END {crc & 0xFFFFFFFF:08x}\n".encode())

    print("Waiting for result...", flush=True)
    result = wait_for(ser, ("XFER OK", "XFER FAIL"), timeout=60)
    ser.close()

    if result is None:
        print("ERROR: no result from device", file=sys.stderr, flush=True)
        sys.exit(1)
    if result.startswith("XFER OK"):
        elapsed = time.time() - start
        print(f"OK -- {total} bytes in {elapsed:.1f}s ({total / elapsed / 1024:.0f} KB/s)", flush=True)
    else:
        print(f"FAILED: {result}", file=sys.stderr, flush=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
