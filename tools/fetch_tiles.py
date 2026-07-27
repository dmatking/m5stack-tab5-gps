#!/usr/bin/env python3
# Copyright 2025-2026 David M. King
# SPDX-License-Identifier: Apache-2.0
#
# Fetches a grid of OSM raster tiles centered on a lat/lon, converts them to
# raw little-endian RGB565 (matching the panel's native format), and writes
# one flat row-major blob to flash into the "tiledata" partition (see
# ../partitions.csv) at offset 0x110000.
#
# --rotate applies a 90deg-multiple rotation to each tile's pixel content
# before packing, to match a landscape-oriented render on a physically
# portrait panel (see main/tile_cache.c's placement transform, which must
# use the matching rotation). Positive = counter-clockwise (PIL convention).
#
# Downloaded PNGs are cached locally (.tile_png_cache/) so re-running with a
# different --rotate or grid doesn't re-hit the tile server for tiles already
# fetched.
#
# Respect OSM's tile usage policy (https://operations.osmfoundation.org/policies/tiles/):
# this is meant for occasional small grids during development, not bulk/automated
# scraping. Identify yourself with a real User-Agent and keep grids modest.
#
# Usage:
#   python tools/fetch_tiles.py 32.8896614814945 -97.34129452988256 --rotate 90
#   python tools/fetch_tiles.py <lat> <lon> --zoom 16 --cols 8 --rows 10
#
# After running, flash the blob and update main/map_config.h with the printed
# MAP_TILE_BASE_TX/TY/GRID_COLS/GRID_ROWS/MAP_ZOOM values, e.g.:
#   python -m esptool --chip esp32p4 -p COM17 write-flash 0x110000 tiles.bin

import argparse
import io
import math
import os
import time
import urllib.request

import numpy as np
from PIL import Image

TILE = 256
USER_AGENT = "m5stack-tab5-gps-dev/0.1 (personal hobby project, low-volume dev/test fetch)"
CACHE_DIR = os.path.join(os.path.dirname(__file__), ".tile_png_cache")


def deg2tile(lat, lon, zoom):
    lat_rad = math.radians(lat)
    n = 2 ** zoom
    xtile = int((lon + 180.0) / 360.0 * n)
    ytile = int((1.0 - math.log(math.tan(lat_rad) + 1 / math.cos(lat_rad)) / math.pi) / 2.0 * n)
    return xtile, ytile


def fetch_png(zoom, tx, ty, delay):
    cache_path = os.path.join(CACHE_DIR, f"{zoom}_{tx}_{ty}.png")
    if os.path.exists(cache_path):
        with open(cache_path, "rb") as f:
            return f.read(), True

    url = f"https://tile.openstreetmap.org/{zoom}/{tx}/{ty}.png"
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=15) as resp:
        data = resp.read()
    os.makedirs(CACHE_DIR, exist_ok=True)
    with open(cache_path, "wb") as f:
        f.write(data)
    time.sleep(delay)
    return data, False


def rgb888_to_rgb565_bytes(img: Image.Image) -> bytes:
    arr = np.asarray(img.convert("RGB"), dtype=np.uint16)  # H,W,3
    r = (arr[:, :, 0] >> 3) & 0x1F
    g = (arr[:, :, 1] >> 2) & 0x3F
    b = (arr[:, :, 2] >> 3) & 0x1F
    packed = (r << 11) | (g << 5) | b  # H,W uint16
    return packed.astype("<u2").tobytes()  # force little-endian on the wire


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("lat", type=float)
    ap.add_argument("lon", type=float)
    ap.add_argument("--zoom", type=int, default=16)
    ap.add_argument("--cols", type=int, default=8)
    ap.add_argument("--rows", type=int, default=10)
    ap.add_argument("--rotate", type=int, default=0, help="degrees, positive=CCW (PIL convention)")
    ap.add_argument("--out", default="tiles.bin")
    ap.add_argument("--delay", type=float, default=0.25, help="seconds between fresh (non-cached) requests")
    args = ap.parse_args()

    cx, cy = deg2tile(args.lat, args.lon, args.zoom)
    base_tx = cx - args.cols // 2
    base_ty = cy - args.rows // 2
    print(f"zoom={args.zoom} center_tile=({cx},{cy}) base=({base_tx},{base_ty}) "
          f"grid={args.cols}x{args.rows} rotate={args.rotate}")

    blob = bytearray()
    total = args.cols * args.rows
    n = 0
    for row in range(args.rows):
        for col in range(args.cols):
            tx = base_tx + col
            ty = base_ty + row
            data, from_cache = fetch_png(args.zoom, tx, ty, args.delay)
            img = Image.open(io.BytesIO(data))
            if img.size != (TILE, TILE):
                raise RuntimeError(f"unexpected tile size {img.size} for {tx},{ty}")
            if args.rotate:
                img = img.rotate(args.rotate)
            blob += rgb888_to_rgb565_bytes(img)
            n += 1
            print(f"  [{n}/{total}] tile {tx},{ty} ok{' (cached)' if from_cache else ''}")

    with open(args.out, "wb") as f:
        f.write(blob)

    print(f"\nwrote {args.out} ({len(blob)} bytes)")
    print("\nPaste into main/map_config.h:")
    print(f"#define MAP_ZOOM             {args.zoom}")
    print(f"#define MAP_TILE_BASE_TX     {base_tx}")
    print(f"#define MAP_TILE_BASE_TY     {base_ty}")
    print(f"#define MAP_TILE_GRID_COLS   {args.cols}")
    print(f"#define MAP_TILE_GRID_ROWS   {args.rows}")
    print(f"\nBlob must fit within the \"tiledata\" partition size in partitions.csv ({len(blob)} bytes needed).")


if __name__ == "__main__":
    main()
