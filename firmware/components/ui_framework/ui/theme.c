// S3Wear theme: LVGL's default dark theme (accent = primary colour, body font) with
// the token overrides from docs/04-ui-ux.md §2 applied on top as a child theme.
#include "ui_theme.h"

#include "src/themes/lv_theme_private.h"

static const uint32_t ACCENT_HEX[UI_ACCENT_COUNT] = {
    [UI_ACCENT_BLUE] = 0x3D8BFF,
    [UI_ACCENT_GREEN] = 0x30D158,
    [UI_ACCENT_ORANGE] = 0xFF9F0A,
    [UI_ACCENT_PINK] = 0xFF375F,
    [UI_ACCENT_PURPLE] = 0xBF5AF2,
    [UI_ACCENT_TEAL] = 0x40C8E0,
};

static lv_theme_t s_theme;
static lv_display_t *s_disp;
static ui_accent_t s_accent = UI_ACCENT_BLUE;
static bool s_styles_ready;
static lv_style_t s_screen;
static lv_style_t s_card;
static lv_style_t s_button;
static lv_style_t s_button_disabled;

static void styles_init(void)
{
    lv_style_init(&s_screen);
    lv_style_set_bg_color(&s_screen, ui_color(UI_COLOR_BG));
    lv_style_set_bg_opa(&s_screen, LV_OPA_COVER);
    lv_style_set_text_color(&s_screen, ui_color(UI_COLOR_TEXT));
    lv_style_set_text_font(&s_screen, UI_FONT_BODY);

    lv_style_init(&s_card);
    lv_style_set_bg_color(&s_card, ui_color(UI_COLOR_SURFACE));
    lv_style_set_radius(&s_card, UI_RADIUS_CARD);
    lv_style_set_border_width(&s_card, 0);

    lv_style_init(&s_button);
    lv_style_set_radius(&s_button, UI_RADIUS_BUTTON);
    lv_style_set_min_height(&s_button, UI_TOUCH_MIN);
    lv_style_set_pad_hor(&s_button, UI_SPACE_L);
    lv_style_set_shadow_width(&s_button, 0);

    lv_style_init(&s_button_disabled);
    lv_style_set_bg_color(&s_button_disabled, ui_color(UI_COLOR_SURFACE));
    lv_style_set_text_color(&s_button_disabled, ui_color(UI_COLOR_TEXT_DIM));
    s_styles_ready = true;
}

static void apply_cb(lv_theme_t *th, lv_obj_t *obj)
{
    (void)th;
    if (lv_obj_get_parent(obj) == NULL) {
        lv_obj_add_style(obj, &s_screen, 0);
    } else if (lv_obj_check_type(obj, &lv_button_class)) {
        lv_obj_add_style(obj, &s_button, 0);
        lv_obj_add_style(obj, &s_button_disabled, LV_STATE_DISABLED);
    } else if (lv_obj_check_type(obj, &lv_obj_class)) {
        lv_obj_add_style(obj, &s_card, 0);
    }
}

static lv_theme_t *base_init(void)
{
    // Secondary colour is used by LVGL for some widget accents; keep it neutral.
    return lv_theme_default_init(s_disp, lv_color_hex(ACCENT_HEX[s_accent]), ui_color(UI_COLOR_SURFACE_HI), true,
                                 UI_FONT_BODY);
}

void ui_theme_init(lv_display_t *disp)
{
    s_disp = disp;
    if (!s_styles_ready) {
        styles_init();
    }
    lv_theme_t *base = base_init();
    s_theme = *base;
    lv_theme_set_parent(&s_theme, base);
    lv_theme_set_apply_cb(&s_theme, apply_cb);
    lv_display_set_theme(disp, &s_theme);
}

void ui_theme_set_accent(ui_accent_t accent)
{
    if (accent >= UI_ACCENT_COUNT || accent == s_accent || s_disp == NULL) {
        return;
    }
    s_accent = accent;
    // Re-initialising the default theme updates its static styles in place.
    lv_theme_t *base = base_init();
    s_theme.color_primary = base->color_primary;
    lv_obj_report_style_change(NULL);
}

ui_accent_t ui_theme_get_accent(void)
{
    return s_accent;
}

lv_color_t ui_theme_accent_color(void)
{
    return lv_color_hex(ACCENT_HEX[s_accent]);
}

// --- Tabular digits ----------------------------------------------------------------
// Inter's digits are proportional. These wrappers share a display font's glyph data
// and give every digit the widest digit's advance (kept in user_data), centred,
// without kerning.

static const lv_font_t *const TABULAR_BASE[] = {UI_FONT_DISPLAY, UI_FONT_DISPLAY_BOLD, UI_FONT_DISPLAY_LIGHT};
#define TABULAR_N (sizeof TABULAR_BASE / sizeof TABULAR_BASE[0])
static lv_font_t s_tabular[TABULAR_N];

static bool tabular_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter, uint32_t next)
{
    const bool digit = letter >= '0' && letter <= '9';
    if (!lv_font_get_glyph_dsc_fmt_txt(font, dsc, letter, digit ? 0 : next)) {
        return false;
    }
    if (digit) {
        const uint16_t adv = (uint16_t)(uintptr_t)font->user_data;
        dsc->ofs_x += (int16_t)((adv - dsc->adv_w) / 2);
        dsc->adv_w = adv;
    }
    return true;
}

const lv_font_t *ui_font_tabular(const lv_font_t *display_font)
{
    for (size_t i = 0; i < TABULAR_N; i++) {
        if (TABULAR_BASE[i] != display_font) {
            continue;
        }
        lv_font_t *t = &s_tabular[i];
        if (t->get_glyph_dsc == NULL) {
            uint16_t widest = 0;
            for (uint32_t c = '0'; c <= '9'; c++) {
                lv_font_glyph_dsc_t dsc;
                if (lv_font_get_glyph_dsc_fmt_txt(display_font, &dsc, c, 0) && dsc.adv_w > widest) {
                    widest = dsc.adv_w;
                }
            }
            *t = *display_font;
            t->user_data = (void *)(uintptr_t)widest;
            t->get_glyph_dsc = tabular_glyph_dsc;
        }
        return t;
    }
    return NULL;
}

const lv_font_t *ui_font_display_tabular(void)
{
    return ui_font_tabular(UI_FONT_DISPLAY);
}
