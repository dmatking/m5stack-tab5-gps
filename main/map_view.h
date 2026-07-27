// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Start the map view task: polls touch, updates pan from drag deltas, and
// renders each frame via the tile cache's PPA compositor. Call after
// board_init(), touch_init(), and tile_cache_init().
void map_view_start(void);
