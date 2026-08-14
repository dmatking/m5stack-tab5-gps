#include "ui_theme.h"

ui_fonts_t ui_font;

void ui_theme_init(void)
{
#if UI_USE_CUSTOM_FONTS
    ui_font.xs     = &ui_font_20;
    ui_font.s      = &ui_font_24;
    ui_font.m      = &ui_font_28;
    ui_font.semi_s = &ui_font_semi_24;
    ui_font.semi_m = &ui_font_semi_32;
    ui_font.semi_l = &ui_font_semi_40;
    ui_font.num_m  = &ui_font_bold_48;
    ui_font.num_l  = &ui_font_bold_62;
    ui_font.num_xl = &ui_font_bold_86;
#else
    /* Stock fallbacks. Enable these sizes in lv_conf.h. */
    ui_font.xs     = &lv_font_montserrat_20;
    ui_font.s      = &lv_font_montserrat_24;
    ui_font.m      = &lv_font_montserrat_28;
    ui_font.semi_s = &lv_font_montserrat_24;
    ui_font.semi_m = &lv_font_montserrat_32;
    ui_font.semi_l = &lv_font_montserrat_40;
    ui_font.num_m  = &lv_font_montserrat_48;
    ui_font.num_l  = &lv_font_montserrat_48;
    ui_font.num_xl = &lv_font_montserrat_48;
#endif
}

lv_obj_t *ui_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

lv_obj_t *ui_card(lv_obj_t *parent)
{
    lv_obj_t *o = ui_box(parent);
    lv_obj_set_style_bg_color(o, UI_C_CARD, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, UI_C_BORDER, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, UI_RADIUS, 0);
    lv_obj_set_style_pad_all(o, 16, 0);
    return o;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text,
                   const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t *ui_caption(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = ui_label(parent, text, ui_font.xs, UI_C_MUTED);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    return l;
}

void ui_flex_col(lv_obj_t *obj, lv_coord_t gap)
{
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(obj, gap, 0);
}

void ui_flex_row(lv_obj_t *obj, lv_coord_t gap)
{
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(obj, gap, 0);
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text,
                    lv_color_t bg, lv_color_t text_color,
                    bool outline, lv_color_t border, lv_coord_t h)
{
    lv_obj_t *b = ui_box(parent);
    lv_obj_set_size(b, LV_PCT(100), h);
    lv_obj_set_style_radius(b, 16, 0);
    if (outline) {
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(b, border, 0);
        lv_obj_set_style_border_width(b, 2, 0);
    } else {
        lv_obj_set_style_bg_color(b, bg, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    }
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_label(b, text, ui_font.semi_m, text_color);
    return b;
}

lv_obj_t *ui_divider(lv_obj_t *parent)
{
    lv_obj_t *d = ui_box(parent);
    lv_obj_set_size(d, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(d, UI_C_DIVIDER, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    return d;
}

lv_obj_t *ui_compass(lv_obj_t *parent, lv_coord_t size,
                     lv_obj_t **out_value, lv_obj_t **out_sub)
{
    lv_obj_t *wrap = ui_box(parent);
    lv_obj_set_size(wrap, size, size);

    /* Tick ring: lv_scale in round mode gives the dashed-ring look of the
     * mockup without any vector drawing. */
    lv_obj_t *scale = lv_scale_create(wrap);
    lv_obj_set_size(scale, size, size);
    lv_obj_center(scale);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_label_show(scale, false);
    lv_scale_set_total_tick_count(scale, 73);
    lv_scale_set_major_tick_every(scale, 6);
    lv_scale_set_range(scale, 0, 360);
    lv_scale_set_angle_range(scale, 360);
    lv_scale_set_rotation(scale, 270);
    lv_obj_set_style_arc_opa(scale, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_line_color(scale, UI_C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_line_width(scale, 2, LV_PART_ITEMS);
    lv_obj_set_style_length(scale, 8, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale, UI_C_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 3, LV_PART_INDICATOR);
    lv_obj_set_style_length(scale, 13, LV_PART_INDICATOR);

    /* Inner hairline circle. */
    lv_obj_t *inner = ui_box(wrap);
    lv_coord_t d = size - 34;
    lv_obj_set_size(inner, d, d);
    lv_obj_center(inner);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x2A3542), 0);
    lv_obj_set_style_border_width(inner, 1, 0);

    /* Green north index. */
    lv_obj_t *north = ui_box(wrap);
    lv_obj_set_size(north, 10, 10);
    lv_obj_align(north, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_radius(north, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(north, UI_C_GREEN, 0);
    lv_obj_set_style_bg_opa(north, LV_OPA_COVER, 0);

    lv_obj_t *val = ui_label(wrap, "043" LV_SYMBOL_DUMMY, ui_font.semi_l, UI_C_TEXT);
    lv_label_set_text(val, "043");
    lv_obj_align(val, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *sub = ui_label(wrap, "NE", ui_font.xs, UI_C_MUTED);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 26);

    if (out_value) *out_value = val;
    if (out_sub)   *out_sub   = sub;
    return wrap;
}

void ui_compass_set_heading(lv_obj_t *value_label, lv_obj_t *sub_label,
                            int deg, const char *cardinal)
{
    if (value_label) lv_label_set_text_fmt(value_label, "%03d", deg);
    if (sub_label && cardinal) lv_label_set_text(sub_label, cardinal);
}

lv_obj_t *ui_clock_icon(lv_obj_t *parent, lv_coord_t size, lv_color_t color)
{
    lv_obj_t *wrap = ui_box(parent);
    lv_obj_set_size(wrap, size, size);

    lv_obj_t *face = ui_box(wrap);
    lv_obj_set_size(face, size, size);
    lv_obj_center(face);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(face, color, 0);
    lv_obj_set_style_border_width(face, LV_MAX(size / 14, 2), 0);

    lv_coord_t hand_t = LV_MAX(size / 10, 2);

    /* Minute hand, pointing to 12 -- center-to-top bar, bottom edge pinned
     * to wrap's center so it reads as emanating from there. */
    lv_coord_t min_len = size * 2 / 5;
    lv_obj_t *minute = ui_box(wrap);
    lv_obj_set_size(minute, hand_t, min_len);
    lv_obj_set_style_radius(minute, hand_t / 2, 0);
    lv_obj_set_style_bg_color(minute, color, 0);
    lv_obj_set_style_bg_opa(minute, LV_OPA_COVER, 0);
    lv_obj_align(minute, LV_ALIGN_CENTER, 0, -min_len / 2);

    /* Hour hand, pointing to 3 -- shorter, same idea rotated 90 degrees
     * (an axis-aligned bar rather than an actually-rotated one, same as
     * the minute hand -- no lv_line/transform needed for two right angles). */
    lv_coord_t hr_len = size * 3 / 10;
    lv_obj_t *hour = ui_box(wrap);
    lv_obj_set_size(hour, hr_len, hand_t);
    lv_obj_set_style_radius(hour, hand_t / 2, 0);
    lv_obj_set_style_bg_color(hour, color, 0);
    lv_obj_set_style_bg_opa(hour, LV_OPA_COVER, 0);
    lv_obj_align(hour, LV_ALIGN_CENTER, hr_len / 2, 0);

    return wrap;
}

lv_obj_t *ui_satellite_icon(lv_obj_t *parent, lv_coord_t size, lv_color_t color)
{
    lv_obj_t *wrap = ui_box(parent);
    lv_obj_set_size(wrap, size, size);

    /* Filled square body, centered. */
    lv_coord_t body = LV_MAX(size * 3 / 10, 4);
    lv_obj_t *sat_body = ui_box(wrap);
    lv_obj_set_size(sat_body, body, body);
    lv_obj_center(sat_body);
    lv_obj_set_style_radius(sat_body, LV_MAX(body / 5, 1), 0);
    lv_obj_set_style_bg_color(sat_body, color, 0);
    lv_obj_set_style_bg_opa(sat_body, LV_OPA_COVER, 0);

    /* Outlined "solar panel" wings, one on each side of the body. */
    lv_coord_t wing_w = LV_MAX(size * 3 / 10, 4);
    lv_coord_t wing_h = LV_MAX(size * 9 / 20, 4);
    lv_coord_t gap = LV_MAX(size / 12, 1);
    lv_coord_t border = LV_MAX(size / 16, 1);
    lv_coord_t offset = body / 2 + gap + wing_w / 2;

    lv_obj_t *wing_l = ui_box(wrap);
    lv_obj_set_size(wing_l, wing_w, wing_h);
    lv_obj_set_style_radius(wing_l, 1, 0);
    lv_obj_set_style_border_color(wing_l, color, 0);
    lv_obj_set_style_border_width(wing_l, border, 0);
    lv_obj_align(wing_l, LV_ALIGN_CENTER, -offset, 0);

    lv_obj_t *wing_r = ui_box(wrap);
    lv_obj_set_size(wing_r, wing_w, wing_h);
    lv_obj_set_style_radius(wing_r, 1, 0);
    lv_obj_set_style_border_color(wing_r, color, 0);
    lv_obj_set_style_border_width(wing_r, border, 0);
    lv_obj_align(wing_r, LV_ALIGN_CENTER, offset, 0);

    return wrap;
}

void ui_mark_placeholder(lv_obj_t *card)
{
#if UI_DEBUG_MARK_PLACEHOLDERS
    if (!card) return;
    lv_obj_set_style_bg_color(card, UI_C_CARD_PLACEHOLDER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
#else
    (void)card;
#endif
}
