#!/usr/bin/env python3
# Copyright 2025-2026 David M. King
# SPDX-License-Identifier: Apache-2.0
#
# Fetches a grid of OSM raster tiles centered on a lat/lon, for one or more
# zoom levels, and writes:
#   - a flash blob (tiles.bin) laid out as [tile index table][JPEG data],
#     since JPEG tiles are variable-size unlike the old fixed-size raw
#     RGB565 format. The index is one (offset,length) uint32 pair per tile,
#     row-major within each level, levels concatenated in the order given.
#   - main/map_tiles_data.h (generated -- do not hand-edit), describing each
#     embedded level's grid bounds and where its tiles start in the index.
#
# --rotate applies a 90deg-multiple rotation to each tile's pixel content
# before encoding, to match a landscape-oriented render on a physically
# portrait panel (see main/tile_cache.c's placement transform, which must
# use the matching rotation). Positive = counter-clockwise (PIL convention).
#
# Downloaded PNGs are cached locally (.tile_png_cache/) so re-running with a
# different --rotate/--jpeg-quality/grid doesn't re-hit the tile server for
# tiles already fetched.
#
# Respect OSM's tile usage policy (https://operations.osmfoundation.org/policies/tiles/):
# this is meant for occasional grids during development, not bulk/automated
# scraping. Identify yourself with a real User-Agent, keep the per-request
# delay, and size --zoom/--cols/--rows conservatively -- fetching many zoom
# levels at once is a meaningfully bigger ask on their server than one.
#
# Usage:
#   python tools/fetch_tiles.py 32.8896614814945 -97.34129452988256 --zoom 16 --rotate 90
#   python tools/fetch_tiles.py <lat> <lon> --zoom 10,12,14,16,17,18,19 --cols 6 --rows 8 --rotate 90
#
# After running, flash tiles.bin to the "tiledata" partition (see
# ../partitions.csv), e.g.:
#   python -m esptool --chip esp32p4 -p COM17 write-flash 0x110000 tiles.bin
# main/map_tiles_data.h is written directly into the source tree; just rebuild.

import argparse
import io
import math
import os
import struct
import time
import urllib.request

from PIL import Image

TILE = 256
USER_AGENT = "m5stack-tab5-gps-dev/0.1 (personal hobby project, low-volume dev/test fetch)"
CACHE_DIR = os.path.join(os.path.dirname(__file__), ".tile_png_cache")
HEADER_OUT = os.path.join(os.path.dirname(__file__), "..", "main", "map_tiles_data.h")


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


def to_jpeg_bytes(img: Image.Image, quality: int) -> bytes:
    buf = io.BytesIO()
    img.convert("RGB").save(buf, format="JPEG", quality=quality)
    return buf.getvalue()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("lat", type=float)
    ap.add_argument("lon", type=float)
    ap.add_argument("--zoom", type=str, default="16", help="comma-separated zoom levels, e.g. 14,16,18")
    ap.add_argument("--cols", type=int, default=8)
    ap.add_argument("--rows", type=int, default=10)
    ap.add_argument("--rotate", type=int, default=0, help="degrees, positive=CCW (PIL convention)")
    ap.add_argument("--jpeg-quality", type=int, default=85)
    ap.add_argument("--out", default="tiles.bin")
    ap.add_argument("--delay", type=float, default=0.25, help="seconds between fresh (non-cached) requests")
    args = ap.parse_args()

    zoom_levels = [int(z.strip()) for z in args.zoom.split(",")]

    levels = []       # per-level metadata dicts
    all_jpeg = []      # flat list of jpeg bytes, in final index order
    sizes = []         # collected for the size report

    tile_index_start = 0
    for zoom in zoom_levels:
        cx, cy = deg2tile(args.lat, args.lon, zoom)
        base_tx = cx - args.cols // 2
        base_ty = cy - args.rows // 2
        total = args.cols * args.rows
        print(f"\nzoom={zoom} center_tile=({cx},{cy}) base=({base_tx},{base_ty}) grid={args.cols}x{args.rows}")

        n = 0
        for row in range(args.rows):
            for col in range(args.cols):
                tx = base_tx + col
                ty = base_ty + row
                data, from_cache = fetch_png(zoom, tx, ty, args.delay)
                img = Image.open(io.BytesIO(data))
                if img.size != (TILE, TILE):
                    raise RuntimeError(f"unexpected tile size {img.size} for {zoom}/{tx}/{ty}")
                if args.rotate:
                    img = img.rotate(args.rotate)
                jpeg_bytes = to_jpeg_bytes(img, args.jpeg_quality)
                all_jpeg.append(jpeg_bytes)
                sizes.append(len(jpeg_bytes))
                n += 1
                print(f"  [{n}/{total}] tile {tx},{ty} -> {len(jpeg_bytes)} bytes{' (cached png)' if from_cache else ''}")

        levels.append({
            "zoom": zoom, "base_tx": base_tx, "base_ty": base_ty,
            "cols": args.cols, "rows": args.rows,
            "tile_index_start": tile_index_start,
        })
        tile_index_start += total

    # Layout: index table first (fixed size, one 8-byte entry per tile), then
    # all JPEG data back-to-back. Offsets are absolute from the start of the
    # blob, which is also the start of the "tiledata" partition on-device.
    index_table_size = len(all_jpeg) * 8
    blob = bytearray()
    data_section = bytearray()
    offset = index_table_size
    for jpeg_bytes in all_jpeg:
        blob += struct.pack("<II", offset, len(jpeg_bytes))
        data_section += jpeg_bytes
        offset += len(jpeg_bytes)
    blob += data_section

    with open(args.out, "wb") as f:
        f.write(blob)

    avg_size = sum(sizes) / len(sizes)
    print(f"\nwrote {args.out}: {len(blob)} bytes total "
          f"({len(all_jpeg)} tiles, index {index_table_size} bytes, data {len(data_section)} bytes)")
    print(f"tile size: min={min(sizes)} avg={avg_size:.0f} max={max(sizes)} bytes")
    print("Blob must fit within the \"tiledata\" partition size in partitions.csv.")

    header_path = os.path.abspath(HEADER_OUT)
    with open(header_path, "w") as f:
        f.write("// AUTO-GENERATED by tools/fetch_tiles.py -- do not hand-edit.\n")
        f.write("// Copyright 2025-2026 David M. King\n")
        f.write("// SPDX-License-Identifier: Apache-2.0\n\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("typedef struct {\n")
        f.write("    int32_t zoom;\n")
        f.write("    int32_t base_tx, base_ty;\n")
        f.write("    int32_t cols, rows;\n")
        f.write("    uint32_t tile_index_start; // index (not byte offset) of this level's first tile\n")
        f.write("} embedded_zoom_t;\n\n")
        f.write("static const embedded_zoom_t MAP_EMBEDDED_ZOOMS[] = {\n")
        for lvl in levels:
            f.write(f"    {{ {lvl['zoom']}, {lvl['base_tx']}, {lvl['base_ty']}, "
                     f"{lvl['cols']}, {lvl['rows']}, {lvl['tile_index_start']} }},\n")
        f.write("};\n")
        f.write("#define MAP_EMBEDDED_ZOOM_COUNT ((int)(sizeof(MAP_EMBEDDED_ZOOMS) / sizeof(MAP_EMBEDDED_ZOOMS[0])))\n")
    print(f"wrote {header_path}")


if __name__ == "__main__":
    main()
