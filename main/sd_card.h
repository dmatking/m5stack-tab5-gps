// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdbool.h>

// Mount point where the SD card's FAT filesystem is exposed once mounted.
#define SD_MOUNT_POINT "/sdcard"

// Power up the SD IO rail (ESP32-P4 on-chip LDO channel 4) and mount the
// microSD card (SDMMC, 4-bit bus, dedicated host slot 0 -- no conflict with
// the C6 WiFi co-processor's separate SDIO link) at SD_MOUNT_POINT. Call
// once, early in app_main() -- independent of board_init()'s I2C/IO-
// expander sequence (this uses the P4's own on-chip LDO, not EXT5V_EN).
// Never auto-formats the card. There's no card-detect pin wired on this
// board, so this must be called unconditionally and must fail softly:
// returns false (and logs a warning, not an error) if no card is present
// or the mount otherwise fails -- callers should treat that as "SD
// unavailable" and keep going, not as fatal.
bool sd_card_mount(void);

// True if sd_card_mount() succeeded.
bool sd_card_is_mounted(void);

// Real used/total space on the mounted card, in GB (1e9 bytes, matching how
// card/drive capacities are normally advertised -- not GiB). false (leaves
// both untouched) if the card isn't mounted or the FATFS free-space query
// fails.
bool sd_card_get_usage(float *used_gb, float *total_gb);
