#!/usr/bin/env python3
# Copyright 2025-2026 David M. King
# SPDX-License-Identifier: Apache-2.0
#
# Converts an ArcGIS Pro "Create Map Tile Package" export (Compact Cache V2
# format -- produced whenever a Predefined tiling scheme like
# ArcGISOnline_Bing_Maps_Google_Maps is used, which forces bundled/Compact
# storage; "Exploded" isn't selectable with a Predefined scheme) into this
# project's SD tile-shard format (see main/tile_sd.c), so real GIS-sourced
# imagery can be deployed the same way tools/fetch_tiles.py's --target sd
# output is.
#
# Compact Cache V2's bundle header/index/tile-record byte layout is
# implemented directly from Esri's own reference spec:
#   https://github.com/Esri/raster-tiles-compactcache/blob/master/CompactCacheV2.md
# not reverse-engineered/guessed.
#
# Usage:
#   python tools/arcgis_bundle_to_sd.py <package_dir> --level 16 --out tiles_sd_out
#
# <package_dir> is the folder containing conf.xml and _alllayers/ (the
# unpacked contents of the exported tile package/.tpkx).
#
# IMPORTANT: this app is portrait-native (see main/map_config.h) and expects
# UN-rotated tile content -- unlike tools/fetch_tiles.py's --rotate, this
# script never rotates anything; ArcGIS tile bytes are used as-is.
#
# The row/col -> tile_x/tile_y mapping below (ArcGIS (row,col) == standard
# XYZ (tile_y,tile_x), verbatim, no offset) is only correct when the export
# used the ArcGISOnline_Bing_Maps_Google_Maps predefined tiling scheme with
# WGS 1984 Web Mercator (Auxiliary Sphere) -- that's the scheme's whole
# purpose (exact interop with Bing/Google/OSM tiling). validate_conf() below
# checks conf.xml for the telltale signs of a wrong export (wrong spatial
# reference, non-standard tile origin, wrong storage format) and refuses to
# proceed by default if any of those look off, since a bad export wastes
# the whole conversion run silently otherwise -- see --force to override.

import argparse
import math
import os
import struct
import sys
import xml.etree.ElementTree as ET
from io import BytesIO

from PIL import Image

BUNDLE_DIM = 128     # tiles per bundle side -- fixed by the Compact V2 format
INDEX_SIZE = 131072  # 128*128*8 bytes
HEADER_SIZE = 64

# Must match main/tile_jpeg.c's MAX_JPEG_TILE_BYTES and
# tools/fetch_tiles.py's copy of the same constant/reasoning.
MAX_JPEG_TILE_BYTES = 48 * 1024
MIN_JPEG_QUALITY = 40

# Standard Web Mercator world corner (EPSG:3857), meters. A tile origin far
# from this means the export isn't using the standard tiling scheme/origin
# this script assumes.
STANDARD_ORIGIN = (-20037508.342787, 20037508.342787)


def parse_conf_xml(pkg_dir):
    conf_path = os.path.join(pkg_dir, "conf.xml")
    root = ET.parse(conf_path).getroot()

    def text(el):
        return el.text.strip() if el is not None and el.text else None

    wkid = None
    sr = root.find(".//SpatialReference")
    if sr is not None:
        wkid_el = sr.find("WKID")
        if wkid_el is None:
            wkid_el = sr.find("LatestWKID")
        if wkid_el is not None:
            wkid = int(wkid_el.text)

    origin_x = origin_y = None
    origin_el = root.find(".//TileOrigin")
    if origin_el is not None:
        x_el, y_el = origin_el.find("X"), origin_el.find("Y")
        if x_el is not None and y_el is not None:
            origin_x, origin_y = float(x_el.text), float(y_el.text)

    cols_el = root.find(".//TileCols")
    rows_el = root.find(".//TileRows")
    tile_size = (
        int(cols_el.text) if cols_el is not None else None,
        int(rows_el.text) if rows_el is not None else None,
    )

    levels = {}
    for lod in root.findall(".//LODInfo"):
        level_id = int(lod.find("LevelID").text)
        scale_el = lod.find("Scale")
        levels[level_id] = float(scale_el.text) if scale_el is not None else None

    storage_format = text(root.find(".//StorageFormat"))
    packet_el = root.find(".//PacketSize")
    packet_size = int(packet_el.text) if packet_el is not None else None

    return {
        "wkid": wkid,
        "origin": (origin_x, origin_y),
        "tile_size": tile_size,
        "levels": levels,
        "storage_format": storage_format,
        "packet_size": packet_size,
    }


def validate_conf(conf):
    problems = []
    if conf["wkid"] not in (3857, 102100):  # Esri uses both to mean Web Mercator Aux Sphere
        problems.append(f"WKID is {conf['wkid']}, expected 3857 (WGS 1984 Web Mercator (Auxiliary Sphere)) "
                         f"-- set the map's coordinate system before re-exporting")
    if conf["tile_size"] != (256, 256):
        problems.append(f"tile size is {conf['tile_size']}, expected (256, 256)")
    if conf["storage_format"] and "CompactV2" not in conf["storage_format"]:
        problems.append(f"storage format is {conf['storage_format']}, expected "
                         f"esriMapCacheStorageModeCompactV2 (this script only implements Compact V2)")
    if conf["packet_size"] not in (None, BUNDLE_DIM):
        problems.append(f"packet size is {conf['packet_size']}, expected {BUNDLE_DIM}")
    ox, oy = conf["origin"]
    if ox is not None and (abs(ox - STANDARD_ORIGIN[0]) > 1.0 or abs(oy - STANDARD_ORIGIN[1]) > 1.0):
        problems.append(f"tile origin is ({ox},{oy}), expected the standard Web Mercator world corner "
                         f"{STANDARD_ORIGIN} -- a non-standard origin means this script's row/col -> "
                         f"tile_x/tile_y mapping is probably wrong for this export (likely means the "
                         f"tiling scheme wasn't the ArcGISOnline_Bing_Maps_Google_Maps predefined one)")
    return problems


def bundle_files_for_level(pkg_dir, level):
    level_dir = os.path.join(pkg_dir, "_alllayers", f"L{level:02d}")
    if not os.path.isdir(level_dir):
        raise SystemExit(f"no such level directory: {level_dir}")
    for name in sorted(os.listdir(level_dir)):
        if name.upper().endswith(".BUNDLE"):
            yield os.path.join(level_dir, name)


def parse_bundle_name(path, level):
    """R<hex>C<hex>.bundle -> (row, col) -- hex, absolute tile coords of the
    bundle's top-left (lowest row/col) tile.

    Genuinely tricky to parse right, and this project has now been wrong
    twice on real data before landing here:

    1st attempt: scan for the first literal 'C' as the separator. Broken
    because hex digits can themselves be 'c' (0xC=12) -- a real z13 bundle
    was R0c80C0700.bundle, whose row value "0c80" contains a 'c' that a
    naive scan mistakes for the separator, silently producing a wrong
    split with no error.

    2nd attempt: assume row and col are always padded to equal width
    (true by construction, reasoned, since the tile grid is square at any
    given level so row/col share the same value range) -- ALSO broken,
    confirmed on a real z18 bundle: R19a80Ce800.bundle has a 5-hex-digit
    row (0x19a80) but only a 4-hex-digit col (0xe800). ArcGIS pads each
    field to its own minimum width independently (at least 4 digits per
    the spec, but no more than a given value actually needs) -- not a
    shared width between the two fields. A greedy-regex fix was also
    considered along the way and rejected on paper: it's silently wrong
    whenever the column value happens to start with 'C'.

    Real fix: the filename alone is genuinely ambiguous when hex digits
    collide with the separator -- resolve it with the one piece of outside
    information available: the valid tile range for this zoom level ([0,
    2**level)). Try every 'c'/'C' character as a candidate separator, keep
    only candidates where both sides are at least 4 hex digits (the
    spec's stated minimum) AND both parse to a value inside the valid
    range for `level`. Exactly one candidate should survive; if zero or
    more than one do, stop and say so rather than silently guess.
    """
    name = os.path.splitext(os.path.basename(path))[0]
    if name[:1].upper() != "R":
        raise ValueError(f"bundle filename doesn't start with R: {path}")
    rest = name[1:]
    max_valid = (1 << level) - 1

    candidates = []
    for i, ch in enumerate(rest):
        if ch.upper() != "C":
            continue
        row_str, col_str = rest[:i], rest[i + 1:]
        if len(row_str) < 4 or len(col_str) < 4:
            continue
        try:
            row_val, col_val = int(row_str, 16), int(col_str, 16)
        except ValueError:
            continue
        if 0 <= row_val <= max_valid and 0 <= col_val <= max_valid:
            candidates.append((row_val, col_val))

    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        raise ValueError(f"couldn't find a valid row/col split for level {level} "
                          f"(valid range 0-{max_valid}): {path}")
    raise ValueError(f"ambiguous row/col split -- {len(candidates)} candidates all "
                      f"valid for level {level}: {candidates} in {path}")


def read_bundle_tiles(path, level):
    """Yields (row, col, jpeg_bytes) for every present tile in this bundle --
    row/col are absolute tile coordinates (bundle's base + local 0..127),
    per CompactCacheV2.md's header/index/tile-record layout."""
    base_row, base_col = parse_bundle_name(path, level)
    with open(path, "rb") as f:
        header = f.read(HEADER_SIZE)
        if len(header) != HEADER_SIZE:
            raise ValueError(f"{path}: truncated header ({len(header)} bytes)")
        version = struct.unpack_from("<I", header, 0)[0]
        if version != 3:
            raise ValueError(f"{path}: bundle version {version}, expected 3 (Compact Cache V2)")

        index_bytes = f.read(INDEX_SIZE)
        if len(index_bytes) != INDEX_SIZE:
            raise ValueError(f"{path}: truncated index ({len(index_bytes)} bytes, expected {INDEX_SIZE})")

        M = 1 << 40  # offset/size packing per the spec: IDX = size*M + offset
        for local_row in range(BUNDLE_DIM):
            for local_col in range(BUNDLE_DIM):
                rec_offset = 8 * (BUNDLE_DIM * local_row + local_col)
                idx = struct.unpack_from("<Q", index_bytes, rec_offset)[0]
                tile_offset = idx % M
                tile_size = idx // M
                if tile_size == 0:
                    continue  # tile absent from this bundle
                f.seek(tile_offset)
                data = f.read(tile_size)
                if len(data) != tile_size:
                    raise ValueError(f"{path}: short read for tile at local ({local_row},{local_col}) "
                                      f"-- wanted {tile_size} bytes, got {len(data)}")
                yield base_row + local_row, base_col + local_col, data


def to_jpeg_if_needed(tile_bytes):
    """ArcGIS Compact caches can mix PNG in with JPEG tiles even when
    conf.xml declares CacheTileFormat=JPEG -- ArcGIS falls back to PNG for
    tiles with real transparency (typically right at the edge of the data
    extent, where the tile is only partially covered), since JPEG has no
    alpha channel. Confirmed on real hardware: the ESP32-P4's hardware JPEG
    decoder doesn't just cleanly reject non-JPEG bytes, it wedges hard
    enough to trip the interrupt watchdog (a real crash, not just a decode
    failure) -- so re-encode anything that isn't already a JPEG here,
    before it ever reaches the shard, rather than ever handing the device
    something that isn't actually what its extension/format field claims.
    Returns (jpeg_bytes, was_converted)."""
    if tile_bytes[:2] == b"\xff\xd8":
        return tile_bytes, False
    img = Image.open(BytesIO(tile_bytes))
    if img.mode in ("RGBA", "LA") or (img.mode == "P" and "transparency" in img.info):
        # JPEG has no alpha channel -- flatten transparency onto white
        # rather than let PIL's .convert("RGB") silently do something less
        # predictable with it.
        rgba = img.convert("RGBA")
        bg = Image.new("RGB", img.size, (255, 255, 255))
        bg.paste(rgba, mask=rgba.split()[-1])
        img = bg
    else:
        img = img.convert("RGB")
    buf = BytesIO()
    img.save(buf, format="JPEG", quality=85)
    return buf.getvalue(), True


def ensure_under_size_cap(jpeg_bytes, context):
    if len(jpeg_bytes) <= MAX_JPEG_TILE_BYTES:
        return jpeg_bytes
    # Rare -- ArcGIS's own export CompressionQuality setting should normally
    # keep tiles well under this, but re-encode at lower quality rather than
    # fail the whole run over one dense tile. Mirrors fetch_tiles.py's same
    # fallback (MAX_JPEG_TILE_BYTES/MIN_JPEG_QUALITY above must stay in sync
    # with that script's copies and with main/tile_jpeg.c's real cap).
    img = Image.open(BytesIO(jpeg_bytes)).convert("RGB")
    quality = 85
    while quality > MIN_JPEG_QUALITY:
        quality -= 10
        buf = BytesIO()
        img.save(buf, format="JPEG", quality=quality)
        jpeg_bytes = buf.getvalue()
        if len(jpeg_bytes) <= MAX_JPEG_TILE_BYTES:
            print(f"  ({context}: re-encoded at quality={quality} to fit under the "
                  f"{MAX_JPEG_TILE_BYTES}-byte on-device cap)")
            return jpeg_bytes
    raise SystemExit(f"{context}: {len(jpeg_bytes)} bytes even at quality={quality}, over "
                      f"MAX_JPEG_TILE_BYTES ({MAX_JPEG_TILE_BYTES})")


def write_sd_shards(out_dir, zoom, base_tx, base_ty, cols, rows, tile_lookup, shard_max_tiles):
    """tile_lookup: dict (tx,ty) -> jpeg_bytes. Tiles missing from the dict
    (holes in the ArcGIS export) get a zero-length index entry -- tile_sd.c
    already treats that as "not present" and falls back to synth_tile()."""
    os.makedirs(out_dir, exist_ok=True)
    shard_rows = max(1, shard_max_tiles // cols)
    num_shards = math.ceil(rows / shard_rows)
    written = []
    sizes = []

    for shard_idx in range(num_shards):
        shard_base_ty = base_ty + shard_idx * shard_rows
        this_rows = min(shard_rows, rows - shard_idx * shard_rows)

        index_bytes = bytearray()
        data_section = bytearray()
        offset = cols * this_rows * 8  # index table size for this shard
        present = 0
        for row in range(this_rows):
            for col in range(cols):
                tx = base_tx + col
                ty = shard_base_ty + row
                jpeg_bytes = tile_lookup.get((tx, ty))
                if jpeg_bytes is None:
                    index_bytes += struct.pack("<II", 0, 0)
                    continue
                present += 1
                sizes.append(len(jpeg_bytes))
                index_bytes += struct.pack("<II", offset, len(jpeg_bytes))
                data_section += jpeg_bytes
                offset += len(jpeg_bytes)

        shard_name = f"tiles_sd_z{zoom}_x{base_tx}_y{shard_base_ty}_c{cols}_r{this_rows}.bin"
        shard_path = os.path.join(out_dir, shard_name)
        with open(shard_path, "wb") as f:
            f.write(index_bytes)
            f.write(data_section)
        written.append(shard_path)
        print(f"  wrote {shard_path}: {len(index_bytes) + len(data_section)} bytes "
              f"({present}/{cols * this_rows} tiles present)")

    return written, sizes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package_dir", help="folder containing conf.xml and _alllayers/")
    ap.add_argument("--level", type=int, required=True,
                     help="ArcGIS LOD LevelID to extract (== the app's zoom level 1:1 when the "
                          "export used the ArcGISOnline_Bing_Maps_Google_Maps predefined scheme)")
    ap.add_argument("--out", default="tiles_sd_out", help="output directory for shard files")
    ap.add_argument("--sd-shard-max-tiles", type=int, default=20000,
                     help="same meaning as tools/fetch_tiles.py's option of the same name")
    ap.add_argument("--force", action="store_true",
                     help="proceed even if conf.xml validation finds a problem -- default is to "
                          "stop and print what's wrong, since a bad export wastes the whole run")
    args = ap.parse_args()

    conf = parse_conf_xml(args.package_dir)
    print(f"conf.xml: WKID={conf['wkid']} tile_size={conf['tile_size']} "
          f"storage_format={conf['storage_format']} packet_size={conf['packet_size']} "
          f"origin={conf['origin']}")
    print(f"levels present: {sorted(conf['levels'])}")

    if args.level not in conf["levels"]:
        raise SystemExit(f"level {args.level} not found in conf.xml's LODInfos "
                          f"(available: {sorted(conf['levels'])})")

    problems = validate_conf(conf)
    if problems:
        print("conf.xml validation problems:")
        for p in problems:
            print(f"  - {p}")
        if not args.force:
            raise SystemExit("stopping -- re-export with WGS 1984 Web Mercator (Auxiliary Sphere) + "
                              "the Predefined ArcGISOnline_Bing_Maps_Google_Maps tiling scheme, or "
                              "pass --force to proceed anyway (tiles will likely be misplaced/wrong)")
        print("--force given, proceeding despite the above")

    bundle_paths = list(bundle_files_for_level(args.package_dir, args.level))
    if not bundle_paths:
        raise SystemExit(f"no .bundle files found for level {args.level}")
    print(f"reading {len(bundle_paths)} bundle file(s) for level {args.level}...")

    tile_lookup = {}
    png_count = 0
    for path in bundle_paths:
        n = 0
        for row, col, tile_bytes in read_bundle_tiles(path, args.level):
            # ArcGIS (row, col) == standard XYZ (tile_y, tile_x), verbatim --
            # see the module docstring for why, and validate_conf() above for
            # the checks that catch it not actually being true for this export.
            tx, ty = col, row
            tile_bytes, was_png = to_jpeg_if_needed(tile_bytes)
            if was_png:
                png_count += 1
            tile_bytes = ensure_under_size_cap(tile_bytes, f"tile {tx},{ty}")
            tile_lookup[(tx, ty)] = tile_bytes
            n += 1
        print(f"  {os.path.basename(path)}: {n} tile(s)")

    if png_count:
        print(f"note: {png_count} tile(s) were PNG (real transparency, typically at the edge of "
              f"the data extent) instead of the JPEG conf.xml declares -- re-encoded to JPEG "
              f"(transparency flattened onto white) since the on-device hardware decoder can't "
              f"handle non-JPEG bytes")

    if not tile_lookup:
        raise SystemExit("no tiles found -- is this really the right level/package?")

    all_tx = [tx for tx, _ty in tile_lookup]
    all_ty = [ty for _tx, ty in tile_lookup]
    base_tx, base_ty = min(all_tx), min(all_ty)
    cols = max(all_tx) - base_tx + 1
    rows = max(all_ty) - base_ty + 1
    total_possible = cols * rows
    print(f"tile bounding rect: base=({base_tx},{base_ty}) grid={cols}x{rows} "
          f"({len(tile_lookup)}/{total_possible} tiles present -- "
          f"{total_possible - len(tile_lookup)} hole(s), if any, will fall back to the on-device "
          f"procedural placeholder)")

    written, sizes = write_sd_shards(args.out, args.level, base_tx, base_ty, cols, rows,
                                      tile_lookup, args.sd_shard_max_tiles)

    avg_size = sum(sizes) / len(sizes)
    total_bytes = sum(os.path.getsize(p) for p in written)
    print(f"\nwrote {len(written)} shard file(s) to {args.out}/, {total_bytes} bytes total")
    print(f"tile size: min={min(sizes)} avg={avg_size:.0f} max={max(sizes)} bytes")
    print(f"Copy every file in {args.out}/ onto the SD card's root (tools/sdmount.sh on the Pi, "
          f"then tools/eject.sh when done) -- no firmware rebuild/reflash needed.")


if __name__ == "__main__":
    main()
