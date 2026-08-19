// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
#ifndef WIFI_UI_BRIDGE_H
#define WIFI_UI_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// Registers persistent WIFI_EVENT/IP_EVENT listeners that keep Settings'
// Wi-Fi row (main/ui_settings.c's UI_SET_WIFI) and the Wi-Fi screen's own
// status line (main/ui_wifi.c) in sync, then -- if credentials are already
// stored in wifi_prov's own NVS namespace (components/wifi_prov/wifi_prov.h)
// -- kicks a background reconnect the same way a fresh boot's stored
// credentials would. Call once from app_main(), after ui_init() so the UI
// exists to receive the first status push, and after app_settings_init()
// so the default NVS partition is already open.
void wifi_ui_bridge_init(void);

// Writes ssid/pass into wifi_prov's own NVS store (namespace "wifi_prov",
// keys "ssid"/"pass") so a future boot's wifi_ui_bridge_init() picks them
// up automatically, then attempts a direct STA connect in a background
// task -- deliberately NOT via wifi_prov_start(): that function never
// reports failure back to its caller (a bad password falls into its own
// captive-portal AP fallback and blocks there forever instead of
// returning), which would silently start a stray SoftAP + web server the
// whole point of this on-device screen is to avoid needing. See
// wifi_ui_bridge.c's connect_task() for the direct connect path (mirrors
// wifi_prov.c's own sta_connect(), minus the portal fallback) that reports
// UI_WIFI_FAILED back through the UI instead. `pass` may be NULL/empty for
// an open network.
void wifi_ui_bridge_connect(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif
#endif /* WIFI_UI_BRIDGE_H */
