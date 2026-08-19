// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include "ui_wifi.h"

static ui_wifi_t s_wifi;

// Keyboard-textarea pairing -- standard LVGL idiom. Keyboard is a FLOATING
// child of `scr` (not `body`), bottom-aligned, hidden until a textarea
// gets focus, and re-targeted (not recreated) when focus moves between the
// two fields. Same FLOATING-overlay pattern as ui_home.c's confirm scrim
// and ui_common.c's screenshot flash -- it needs to sit at a fixed screen
// position outside `body`'s own flex column, on top of whatever's normally
// there (here, the footer buttons) while it's up.
static void ta_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    lv_keyboard_set_textarea(s_wifi.keyboard, ta);
    lv_obj_remove_flag(s_wifi.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_wifi.keyboard);
}

// Password field's focus highlight lives on its own wrapping card
// (pass_row) rather than the textarea itself -- see its own creation
// comment for why -- so LVGL's automatic focused-border styling on the
// textarea has nothing to draw. Mirror the focus state onto the parent
// by hand instead.
static void pass_row_focus_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_obj_get_parent((lv_obj_t *)lv_event_get_target(e));
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED) lv_obj_add_state(row, LV_STATE_FOCUSED);
    else                                          lv_obj_remove_state(row, LV_STATE_FOCUSED);
}

// Eye-icon toggle -- shows the icon for the action tapping it will take
// next (EYE_OPEN while masked = "tap to reveal", EYE_CLOSE while shown =
// "tap to hide"), standard convention. Requested directly: real hardware
// use turned up that a typo-vs-actually-wrong-password mixup is
// indistinguishable from the dots alone, and there's no other way to
// double-check what got typed on a device with no separate display.
static void pass_eye_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    bool was_masked = lv_textarea_get_password_mode(s_wifi.pass_ta);
    lv_textarea_set_password_mode(s_wifi.pass_ta, !was_masked);
    lv_label_set_text(s_wifi.pass_eye, was_masked ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

// Enter/checkmark tapped, or the keyboard's own cancel -- either way, done
// typing for now. Doesn't clear the textarea's own focus state; the cursor
// staying visible after the keyboard hides is harmless.
static void kb_dismiss_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_add_flag(s_wifi.keyboard, LV_OBJ_FLAG_HIDDEN);
}

ui_wifi_t *ui_wifi_create(lv_event_cb_t tab_cb)
{
    ui_wifi_t *w = &s_wifi;
    lv_memzero(w, sizeof(*w));

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_flex_col(scr, 0);
    w->screen = scr;

    /* header: title, same shape as ui_settings.c's (this screen is reached
     * from Settings, so it reads as a sub-page of it, not a sixth tab). No
     * live clock/battery here -- Settings' own header fields aren't wired
     * to gps_ui_bridge.c's push loop either (see that file's push_status()
     * call sites), so adding it just here would be a one-off the rest of
     * this screen's own header doesn't have anywhere else to match. */
    lv_obj_t *head = ui_box(scr);
    lv_obj_set_size(head, LV_PCT(100), 66);
    lv_obj_set_style_pad_hor(head, 22, 0);
    ui_flex_row(head, 12);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_label(head, "Wi-Fi", ui_font.semi_m, UI_C_TEXT);

    lv_obj_t *body = ui_box(scr);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_bottom(body, 16, 0);
    ui_flex_col(body, 12);

    /* status line ------------------------------------------------------ */
    lv_obj_t *status = ui_card(body);
    lv_obj_set_width(status, LV_PCT(100));
    lv_obj_set_height(status, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(status, 20, 0);
    lv_obj_set_style_pad_ver(status, 16, 0);
    ui_flex_row(status, 12);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    w->status_dot = ui_box(status);
    lv_obj_set_size(w->status_dot, 16, 16);
    lv_obj_set_style_radius(w->status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(w->status_dot, UI_C_DIM, 0);
    lv_obj_set_style_bg_opa(w->status_dot, LV_OPA_COVER, 0);
    w->status_label = ui_label(status, "Not connected", ui_font.s, UI_C_MUTED);

    /* SSID / password ---------------------------------------------------- */
    ui_caption(body, "NETWORK NAME (SSID)");
    w->ssid_ta = lv_textarea_create(body);
    lv_obj_set_width(w->ssid_ta, LV_PCT(100));
    lv_obj_set_height(w->ssid_ta, LV_SIZE_CONTENT);
    lv_textarea_set_one_line(w->ssid_ta, true);
    // 32 bytes, matching wifi_config_t's sta.ssid[32] (esp_wifi_types.h) --
    // no point letting the field hold more than the driver will ever use.
    lv_textarea_set_max_length(w->ssid_ta, 32);
    lv_textarea_set_placeholder_text(w->ssid_ta, "Network name");
    lv_obj_set_style_bg_color(w->ssid_ta, UI_C_CARD, 0);
    lv_obj_set_style_bg_opa(w->ssid_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w->ssid_ta, 1, 0);
    lv_obj_set_style_border_color(w->ssid_ta, UI_C_BORDER, 0);
    lv_obj_set_style_radius(w->ssid_ta, 12, 0);
    lv_obj_set_style_pad_all(w->ssid_ta, 14, 0);
    lv_obj_set_style_text_color(w->ssid_ta, UI_C_TEXT, 0);
    lv_obj_set_style_text_font(w->ssid_ta, ui_font.m, 0);
    // Focused-border highlight -- same UI_C_BLUE "this one's active"
    // convention as ui_goto.c's coord_field()/fmt_btn[], and a real gap
    // without it: reported directly on real hardware (tapping a field and
    // typing worked, but nothing on screen showed which field was
    // selected -- the two textareas are otherwise visually identical).
    lv_obj_set_style_border_color(w->ssid_ta, UI_C_BLUE, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(w->ssid_ta, 2, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(w->ssid_ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    ui_caption(body, "PASSWORD");
    // Card row (textarea + eye toggle side by side) -- same flat shape as
    // ui_goto.c's coord_field() (a ui_card() flex row holding a label and
    // a badge), not a second level of SIZE_CONTENT nesting inside body's
    // own column: that specific shape is the one this project has been
    // bitten by more than once (see project_nested_flex_render_bug), this
    // one is proven safe.
    lv_obj_t *pass_row = ui_card(body);
    lv_obj_set_width(pass_row, LV_PCT(100));
    lv_obj_set_height(pass_row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(pass_row, 14, 0);
    lv_obj_set_style_pad_ver(pass_row, 0, 0);
    ui_flex_row(pass_row, 4);
    lv_obj_set_flex_align(pass_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    w->pass_ta = lv_textarea_create(pass_row);
    lv_obj_set_flex_grow(w->pass_ta, 1);
    lv_obj_set_height(w->pass_ta, LV_SIZE_CONTENT);
    lv_textarea_set_one_line(w->pass_ta, true);
    lv_textarea_set_password_mode(w->pass_ta, true);
    // 64 bytes, matching wifi_config_t's sta.password[64].
    lv_textarea_set_max_length(w->pass_ta, 64);
    lv_textarea_set_placeholder_text(w->pass_ta, "Leave blank if open");
    // Transparent/borderless -- pass_row itself is the card now, the
    // textarea just fills it.
    lv_obj_set_style_bg_opa(w->pass_ta, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->pass_ta, 0, 0);
    lv_obj_set_style_pad_ver(w->pass_ta, 14, 0);
    lv_obj_set_style_pad_hor(w->pass_ta, 0, 0);
    lv_obj_set_style_text_color(w->pass_ta, UI_C_TEXT, 0);
    lv_obj_set_style_text_font(w->pass_ta, ui_font.m, 0);
    lv_obj_add_event_cb(w->pass_ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    // Focus highlight moves to the row (the actual visible card border)
    // now that the textarea itself has none of its own.
    lv_obj_set_style_border_color(pass_row, UI_C_BLUE, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(pass_row, 2, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(w->pass_ta, pass_row_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(w->pass_ta, pass_row_focus_cb, LV_EVENT_DEFOCUSED, NULL);

    w->pass_eye = ui_label(pass_row, LV_SYMBOL_EYE_CLOSE, ui_font.s, UI_C_MUTED);
    lv_obj_add_flag(w->pass_eye, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(w->pass_eye, pass_eye_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *spacer = ui_box(body);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);

    /* footer -------------------------------------------------------------- */
    // Same shape as ui_goto.c's Cancel/Start Navigation footer (1:2 grow
    // ratio), minus the middle Save button -- there's nothing here to save
    // separately from connecting, unlike Goto's typed-coordinate/waypoint
    // split.
    lv_obj_t *btns = ui_box(body);
    lv_obj_set_size(btns, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(btns, 12);
    lv_obj_t *bl = ui_box(btns);
    lv_obj_set_size(bl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(bl, 1);
    w->btn_cancel = ui_button(bl, "Cancel", UI_C_CARD, UI_C_MUTED, true,
                              UI_C_BORDER, 92);
    lv_obj_t *br = ui_box(btns);
    lv_obj_set_size(br, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(br, 2);
    w->btn_connect = ui_button(br, "Connect", UI_C_BLUE_BTN, UI_C_TEXT,
                               false, UI_C_BLUE, 92);

    ui_navbar_create(scr, UI_TAB_MORE, tab_cb);

    /* on-screen keyboard, overlays the footer while up -------------------- */
    w->keyboard = lv_keyboard_create(scr);
    // 420, not the default-theme-driven ~380 -- a bit more row height to
    // give the bumped-up key font below room to breathe, same reasoning
    // as ui_goto.c's keypad buttons (74px tall for a ~32px font).
    lv_obj_set_size(w->keyboard, LV_PCT(100), 420);
    lv_obj_align(w->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    // Default theme font on a button matrix this size (each key ~55-65px
    // wide on this 720px-wide screen) reads small -- bump it, same
    // "chunkier, not fiddly on a touchscreen" adjustment already made to
    // Settings' brightness slider and Goto's +/- badges.
    lv_obj_set_style_text_font(w->keyboard, ui_font.s, LV_PART_ITEMS);
    lv_obj_add_flag(w->keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(w->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(w->keyboard, kb_dismiss_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(w->keyboard, kb_dismiss_cb, LV_EVENT_CANCEL, NULL);

    return w;
}

void ui_wifi_set_status(ui_wifi_t *w, ui_wifi_status_t status, const char *ssid)
{
    if (!w) return;
    lv_color_t c;
    switch (status) {
    case UI_WIFI_CONNECTING:
        c = UI_C_BLUE;
        lv_label_set_text_fmt(w->status_label, "Connecting to \"%s\"...",
                              ssid ? ssid : "");
        break;
    case UI_WIFI_CONNECTED:
        c = UI_C_GREEN;
        lv_label_set_text_fmt(w->status_label, "Connected to \"%s\"",
                              ssid ? ssid : "");
        break;
    case UI_WIFI_FAILED:
        c = UI_C_RED;
        lv_label_set_text(w->status_label, "Connection failed");
        break;
    case UI_WIFI_DISCONNECTED:
    default:
        c = UI_C_MUTED;
        lv_label_set_text(w->status_label, "Not connected");
        break;
    }
    lv_obj_set_style_bg_color(w->status_dot, status == UI_WIFI_DISCONNECTED ? UI_C_DIM : c, 0);
    lv_obj_set_style_text_color(w->status_label, c, 0);
}

const char *ui_wifi_get_ssid(ui_wifi_t *w)
{
    return w ? lv_textarea_get_text(w->ssid_ta) : "";
}

const char *ui_wifi_get_password(ui_wifi_t *w)
{
    return w ? lv_textarea_get_text(w->pass_ta) : "";
}

void ui_wifi_set_ssid(ui_wifi_t *w, const char *ssid)
{
    if (w) lv_textarea_set_text(w->ssid_ta, ssid ? ssid : "");
}

void ui_wifi_set_connect_cb(ui_wifi_t *w, lv_event_cb_t cb, void *user_data)
{
    if (w && cb) lv_obj_add_event_cb(w->btn_connect, cb, LV_EVENT_CLICKED, user_data);
}

void ui_wifi_set_cancel_cb(ui_wifi_t *w, lv_event_cb_t cb, void *user_data)
{
    if (w && cb) lv_obj_add_event_cb(w->btn_cancel, cb, LV_EVENT_CLICKED, user_data);
}
