#!/usr/bin/env python3
# Copyright 2025-2026 David M. King
# SPDX-License-Identifier: Apache-2.0
#
# Fetches a grid of OSM raster tiles for one or more zoom levels, for either
# of two on-device targets:
#
#   --target flash (default): writes a single blob (tiles.bin, [index
#     table][JPEG data]) meant for the small (14MB) compile-time-embedded
#     "tiledata" flash partition, plus regenerates main/map_tiles_data.h.
#     Unchanged from before -- this is still the right choice for a small
#     always-available "home area" grid.
#
#   --target sd: writes multiple sharded blob files meant to be copied onto
#     the Tab5's SD card (see main/tile_sd.c), for real geographic coverage
#     far larger than flash can hold. Sharded rather than one big blob
#     because a single multi-GB file is broken on this project's actual
#     toolchain/filesystem config in three independent ways: fseek()/off_t
#     is 32-bit (~2GB ceiling), FAT32 caps a single file at 4GB-1, and this
#     project's sdkconfig has fast-seek disabled, so backward seeks (which
#     panning/zooming inherently produce) walk the FAT chain from the file's
#     first cluster -- see main/tile_sd.c's header comment for the full
#     analysis. Each shard covers a bounded row-range of one zoom level's
#     grid (--sd-shard-max-tiles), named
#         tiles_sd_z<zoom>_x<base_tx>_y<base_ty>_c<cols>_r<rows>.bin
#     so main/tile_sd.c can discover coverage from filenames alone (no
#     separate manifest to go stale). No main/map_tiles_data.h write for
#     this target -- SD coverage is discovered at runtime, not compiled in.
#
# Area can be given either as a center point + --cols/--rows (a grid
# centered on one point -- the original mode, good for a small local area),
# or as --bbox lat1,lon1,lat2,lon2 (good for an irregular region like a
# multi-county area that doesn't fit a "center + symmetric grid" model).
# Both work with either target.
#
# --rotate applies a 90deg-multiple rotation to each tile's pixel content
# before encoding, to match a landscape-oriented render on a physically
# portrait panel (see main/tile_cache.c's placement transform, which must
# use the matching rotation). Positive = counter-clockwise (PIL convention).
#
# Downloaded PNGs are cached locally (.tile_png_cache/) so re-running with a
# different --rotate/--jpeg-quality/grid/target doesn't re-hit the tile
# server for tiles already fetched.
#
# Respect OSM's tile usage policy (https://operations.osmfoundation.org/policies/tiles/):
# this is meant for occasional grids during development, not bulk/automated
# scraping. Identify yourself with a real User-Agent, keep the per-request
# delay, and size --zoom/--cols/--rows/--bbox conservatively -- fetching many
# zoom levels or a large region at once is a meaningfully bigger ask on
# their server than one small grid.
#
# Usage:
#   python tools/fetch_tiles.py 32.8896614814945 -97.34129452988256 --zoom 16 --rotate 90
#   python tools/fetch_tiles.py <lat> <lon> --zoom 10,12,14,16,17,18,19 --cols 6 --rows 8 --rotate 90
#   python tools/fetch_tiles.py --target sd --bbox 33.45,-98.15,32.15,-96.55 --zoom 10,12,14,16 --rotate 90
#
# After a flash-target run, flash tiles.bin to the "tiledata" partition (see
# ../partitions.csv), e.g.:
#   python -m esptool --chip esp32p4 -p COM17 write-flash 0x110000 tiles.bin
# main/map_tiles_data.h is written directly into the source tree; just rebuild.
#
# After an sd-target run, copy every file from the output directory onto the
# SD card (tools/sdmount.sh on the Pi, then eject.sh when done) -- no
# firmware rebuild/reflash needed to update SD-backed coverage.

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


def bbox_tile_range(lat1, lon1, lat2, lon2, zoom):
    """(base_tx, base_ty, cols, rows) covering the bbox at zoom. Corner order
    doesn't matter -- tile Y increases southward while lat decreases
    southward, so this always takes min/max of the computed tile coords
    rather than assuming which input corner maps to which tile-space corner."""
    x1, y1 = deg2tile(lat1, lon1, zoom)
    x2, y2 = deg2tile(lat2, lon2, zoom)
    min_tx, max_tx = min(x1, x2), max(x1, x2)
    min_ty, max_ty = min(y1, y2), max(y1, y2)
    return min_tx, min_ty, (max_tx - min_tx + 1), (max_ty - min_ty + 1)


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


def fetch_tile_jpeg(zoom, tx, ty, args):
    data, from_cache = fetch_png(zoom, tx, ty, args.delay)
    img = Image.open(io.BytesIO(data))
    if img.size != (TILE, TILE):
        raise RuntimeError(f"unexpected tile size {img.size} for {zoom}/{tx}/{ty}")
    if args.rotate:
        img = img.rotate(args.rotate)
    jpeg_bytes = to_jpeg_bytes(img, args.jpeg_quality)
    return jpeg_bytes, from_cache


def level_grid(args, bbox, zoom):
    if bbox:
        return bbox_tile_range(*bbox, zoom)
    cx, cy = deg2tile(args.lat, args.lon, zoom)
    cols, rows = args.cols, args.rows
    return cx - cols // 2, cy - rows // 2, cols, rows


def run_flash_target(args, zoom_levels, bbox):
    levels = []       # per-level metadata dicts
    all_jpeg = []      # flat list of jpeg bytes, in final index order
    sizes = []         # collected for the size report

    tile_index_start = 0
    for zoom in zoom_levels:
        base_tx, base_ty, cols, rows = level_grid(args, bbox, zoom)
        total = cols * rows
        print(f"\nzoom={zoom} base=({base_tx},{base_ty}) grid={cols}x{rows}")

        n = 0
        for row in range(rows):
            for col in range(cols):
                tx = base_tx + col
                ty = base_ty + row
                jpeg_bytes, from_cache = fetch_tile_jpeg(zoom, tx, ty, args)
                all_jpeg.append(jpeg_bytes)
                sizes.append(len(jpeg_bytes))
                n += 1
                print(f"  [{n}/{total}] tile {tx},{ty} -> {len(jpeg_bytes)} bytes{' (cached png)' if from_cache else ''}")

        levels.append({
            "zoom": zoom, "base_tx": base_tx, "base_ty": base_ty,
            "cols": cols, "rows": rows,
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


def run_sd_target(args, zoom_levels, bbox):
    os.makedirs(args.out, exist_ok=True)

    all_sizes = []
    shard_paths = []

    for zoom in zoom_levels:
        base_tx, base_ty, cols, rows = level_grid(args, bbox, zoom)
        shard_rows = max(1, args.sd_shard_max_tiles // cols)
        num_shards = math.ceil(rows / shard_rows)
        print(f"\nzoom={zoom} base=({base_tx},{base_ty}) grid={cols}x{rows} "
              f"-> {num_shards} shard(s) of up to {shard_rows} row(s) each")

        for shard_idx in range(num_shards):
            shard_base_ty = base_ty + shard_idx * shard_rows
            this_rows = min(shard_rows, rows - shard_idx * shard_rows)
            total = cols * this_rows

            index_bytes = bytearray()
            data_section = bytearray()
            offset = cols * this_rows * 8  # index table size for this shard
            n = 0
            for row in range(this_rows):
                for col in range(cols):
                    tx = base_tx + col
                    ty = shard_base_ty + row
                    jpeg_bytes, from_cache = fetch_tile_jpeg(zoom, tx, ty, args)
                    all_sizes.append(len(jpeg_bytes))
                    index_bytes += struct.pack("<II", offset, len(jpeg_bytes))
                    data_section += jpeg_bytes
                    offset += len(jpeg_bytes)
                    n += 1
                    print(f"  shard {shard_idx + 1}/{num_shards} [{n}/{total}] tile {tx},{ty} "
                          f"-> {len(jpeg_bytes)} bytes{' (cached png)' if from_cache else ''}")

            shard_name = f"tiles_sd_z{zoom}_x{base_tx}_y{shard_base_ty}_c{cols}_r{this_rows}.bin"
            shard_path = os.path.join(args.out, shard_name)
            with open(shard_path, "wb") as f:
                f.write(index_bytes)
                f.write(data_section)
            shard_paths.append(shard_path)
            print(f"  wrote {shard_path}: {len(index_bytes) + len(data_section)} bytes "
                  f"({total} tiles, index {len(index_bytes)} bytes, data {len(data_section)} bytes)")

    avg_size = sum(all_sizes) / len(all_sizes)
    total_bytes = sum(os.path.getsize(p) for p in shard_paths)
    print(f"\nwrote {len(shard_paths)} shard file(s) to {args.out}/, {total_bytes} bytes total")
    print(f"tile size: min={min(all_sizes)} avg={avg_size:.0f} max={max(all_sizes)} bytes")
    if max(all_sizes) > 40 * 1024:
        print("WARNING: max tile size is getting close to main/tile_jpeg.c's MAX_JPEG_TILE_BYTES (48KB) -- "
              "recheck that constant against this dataset before flashing.")
    print(f"Copy every file in {args.out}/ onto the SD card's root (tools/sdmount.sh on the Pi, "
          f"then tools/eject.sh when done) -- no firmware rebuild/reflash needed.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("lat", type=float, nargs="?", help="center latitude (omit if using --bbox)")
    ap.add_argument("lon", type=float, nargs="?", help="center longitude (omit if using --bbox)")
    ap.add_argument("--bbox", type=str, default=None,
                     help="lat1,lon1,lat2,lon2 -- alternative to center lat/lon + --cols/--rows, "
                          "for an irregular region that doesn't fit a centered grid")
    ap.add_argument("--target", choices=["flash", "sd"], default="flash",
                     help="flash: single blob for the compile-time-embedded tiledata partition (default). "
                          "sd: sharded blobs for main/tile_sd.c, meant for the SD card")
    ap.add_argument("--zoom", type=str, default="16", help="comma-separated zoom levels, e.g. 14,16,18")
    ap.add_argument("--cols", type=int, default=8, help="ignored if --bbox is given")
    ap.add_argument("--rows", type=int, default=10, help="ignored if --bbox is given")
    ap.add_argument("--sd-shard-max-tiles", type=int, default=20000,
                     help="--target sd only: max tiles per shard file, sized to stay well under the "
                          "2GB fseek/4GB FAT32 ceilings even at worst-case tile size -- see main/tile_sd.c")
    ap.add_argument("--rotate", type=int, default=0, help="degrees, positive=CCW (PIL convention)")
    ap.add_argument("--jpeg-quality", type=int, default=85)
    ap.add_argument("--out", default=None,
                     help="flash target: output blob file (default tiles.bin). "
                          "sd target: output directory for shard files (default tiles_sd_out)")
    ap.add_argument("--delay", type=float, default=0.25, help="seconds between fresh (non-cached) requests")
    args = ap.parse_args()

    if args.bbox:
        if args.lat is not None or args.lon is not None:
            ap.error("--bbox is mutually exclusive with the center lat/lon positional arguments")
        parts = [p.strip() for p in args.bbox.split(",")]
        if len(parts) != 4:
            ap.error("--bbox must be lat1,lon1,lat2,lon2")
        bbox = tuple(float(p) for p in parts)
    else:
        if args.lat is None or args.lon is None:
            ap.error("lat/lon are required unless --bbox is given")
        bbox = None

    if args.out is None:
        args.out = "tiles.bin" if args.target == "flash" else "tiles_sd_out"

    zoom_levels = [int(z.strip()) for z in args.zoom.split(",")]

    if args.target == "flash":
        run_flash_target(args, zoom_levels, bbox)
    else:
        run_sd_target(args, zoom_levels, bbox)


if __name__ == "__main__":
    main()
