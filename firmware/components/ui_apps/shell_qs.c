// Quick settings (docs/03 F4): swipe down on the face. Status line (battery, clock,
// phone link), a grid of round toggle/action buttons, brightness slider. The state
// comes from the backend (shell_qs_set_backend) each time the panel is shown; a
// swipe up, BACK or PWR closes it.
#include <stdio.h>

#include "shell_priv.h"
#include "ui_overlay.h"
#include "ui_theme.h"

#define TOP_Y       UI_SAFE_INSET
#define GRID_Y      72
#define BTN_D       76 // 4 x 76 + 3 x 18 = 358 px: fits the 362 px between the insets
#define GRID_GAP_X  18
#define GRID_GAP_Y  16
#define BRIGHT_Y    356
#define SLIDER_H    12
#define GRABBER_W   48
#define GRABBER_H   5

// Actions open an app instead of toggling.
#define ACTIONS ((1u << SHELL_QS_FLASHLIGHT) | (1u << SHELL_QS_FIND_PHONE) | (1u << SHELL_QS_SETTINGS))

static const struct {
    const char *name;
    const char *icon;
    const char *app; // actions only
} ITEMS[SHELL_QS_COUNT] = {
    [SHELL_QS_DND] = {"Do not disturb", LV_SYMBOL_MINUS, NULL},
    [SHELL_QS_THEATER] = {"Theater mode", LV_SYMBOL_VIDEO, NULL},
    [SHELL_QS_SLEEP] = {"Sleep mode", LV_SYMBOL_EYE_CLOSE, NULL},
    [SHELL_QS_AOD] = {"Always-on display", LV_SYMBOL_EYE_OPEN, NULL},
    [SHELL_QS_WIFI] = {"Wi-Fi", LV_SYMBOL_WIFI, NULL},
    [SHELL_QS_BLUETOOTH] = {"Bluetooth", LV_SYMBOL_BLUETOOTH, NULL},
    [SHELL_QS_SILENT] = {"Silent", LV_SYMBOL_MUTE, NULL},
    [SHELL_QS_FLASHLIGHT] = {"Flashlight", LV_SYMBOL_CHARGE, "flashlight"},
    [SHELL_QS_SAVER] = {"Battery saver", LV_SYMBOL_BATTERY_1, NULL},
    [SHELL_QS_FIND_PHONE] = {"Find phone", LV_SYMBOL_CALL, "find_phone"},
    [SHELL_QS_SETTINGS] = {"Settings", LV_SYMBOL_SETTINGS, "settings"},
};

static shell_qs_backend_t s_backend;
static bool s_has_backend;
static shell_qs_state_t s_mem = {.available = (1u << SHELL_QS_COUNT) - 1, .brightness = 60, .battery_pct = -1};

static bool s_styles_ready;
static lv_style_t s_btn;
static lv_style_t s_btn_on;
static lv_style_t s_btn_pressed;
static lv_style_t s_btn_off; // not available: dim icon, no fill change

typedef struct {
    shell_qs_state_t st;
    lv_obj_t *btn[SHELL_QS_COUNT];
    lv_obj_t *battery;
    lv_obj_t *phone;
    lv_obj_t *slider;
    lv_obj_t *bright_value;
} qs_t;

void shell_qs_set_backend(const shell_qs_backend_t *backend)
{
    s_has_backend = backend != NULL;
    if (backend) {
        s_backend = *backend;
    }
}

const char *shell_qs_name(shell_qs_item_t item)
{
    return (unsigned)item < SHELL_QS_COUNT ? ITEMS[item].name : "?";
}

static void styles_init(void)
{
    if (!s_styles_ready) {
        s_styles_ready = true;
        lv_style_init(&s_btn);
        lv_style_set_radius(&s_btn, LV_RADIUS_CIRCLE);
        lv_style_set_bg_opa(&s_btn, LV_OPA_COVER);
        lv_style_set_bg_color(&s_btn, ui_color(UI_COLOR_SURFACE_HI));
        lv_style_set_text_color(&s_btn, ui_color(UI_COLOR_TEXT));
        lv_style_set_text_font(&s_btn, UI_FONT_TITLE);
        lv_style_init(&s_btn_on);
        lv_style_init(&s_btn_pressed);
        lv_style_set_bg_opa(&s_btn_pressed, LV_OPA_70);
        lv_style_init(&s_btn_off);
        lv_style_set_bg_color(&s_btn_off, ui_color(UI_COLOR_SURFACE));
        lv_style_set_text_color(&s_btn_off, ui_color(UI_COLOR_TEXT_DIM));
    }
    // The accent may have changed since the last time (ui_theme_set_accent).
    lv_style_set_bg_color(&s_btn_on, ui_theme_accent_color());
}

static void read_state(qs_t *q)
{
    if (s_has_backend && s_backend.read) {
        q->st = (shell_qs_state_t){.brightness = 60, .battery_pct = -1};
        s_backend.read(&q->st, s_backend.ctx);
    } else {
        q->st = s_mem;
    }
}

static void show_state(qs_t *q)
{
    for (int i = 0; i < SHELL_QS_COUNT; i++) {
        const uint32_t bit = 1u << i;
        lv_obj_t *b = q->btn[i];
        if (q->st.on & bit & ~ACTIONS) {
            lv_obj_add_state(b, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(b, LV_STATE_CHECKED);
        }
        if ((q->st.available & bit) || (ACTIONS & bit)) {
            lv_obj_remove_state(b, LV_STATE_USER_1);
        } else {
            lv_obj_add_state(b, LV_STATE_USER_1);
        }
    }
    char buf[24];
    const char *sym = q->st.charging            ? LV_SYMBOL_CHARGE
                      : q->st.battery_pct > 75  ? LV_SYMBOL_BATTERY_FULL
                      : q->st.battery_pct > 50  ? LV_SYMBOL_BATTERY_3
                      : q->st.battery_pct > 25  ? LV_SYMBOL_BATTERY_2
                      : q->st.battery_pct >= 0  ? LV_SYMBOL_BATTERY_1
                                                : LV_SYMBOL_BATTERY_EMPTY;
    if (q->st.battery_pct >= 0) {
        snprintf(buf, sizeof buf, "%s %d%%", sym, q->st.battery_pct);
    } else {
        snprintf(buf, sizeof buf, "%s --", sym);
    }
    lv_label_set_text(q->battery, buf);
    lv_obj_set_style_text_color(q->phone, ui_color(q->st.phone_connected ? UI_COLOR_SUCCESS : UI_COLOR_TEXT_DIM), 0);
    lv_slider_set_value(q->slider, q->st.brightness, LV_ANIM_OFF);
    lv_label_set_text_fmt(q->bright_value, "%d%%", q->st.brightness);
}

static void btn_clicked(lv_event_t *e)
{
    qs_t *q = lv_event_get_user_data(e);
    lv_obj_t *b = lv_event_get_target_obj(e);
    int item = 0;
    while (item < SHELL_QS_COUNT && q->btn[item] != b) {
        item++;
    }
    if (item == SHELL_QS_COUNT) {
        return;
    }
    const uint32_t bit = 1u << item;
    char msg[48];
    if (ACTIONS & bit) {
        shell_app_open(shell_app_find(ITEMS[item].app));
        return;
    }
    if (!(q->st.available & bit)) {
        snprintf(msg, sizeof msg, "%s: not available yet", ITEMS[item].name);
        ui_toast_show(msg, 0);
        return;
    }
    q->st.on ^= bit;
    const bool on = q->st.on & bit;
    show_state(q);
    if (s_has_backend) {
        if (s_backend.toggle) {
            s_backend.toggle((shell_qs_item_t)item, on, s_backend.ctx);
        }
    } else {
        s_mem.on = q->st.on;
    }
    // The buttons have no labels: say what changed.
    snprintf(msg, sizeof msg, "%s %s", ITEMS[item].name, on ? "on" : "off");
    ui_toast_show(msg, 0);
}

static void slider_event(lv_event_t *e)
{
    qs_t *q = lv_event_get_user_data(e);
    const int32_t v = lv_slider_get_value(q->slider);
    lv_label_set_text_fmt(q->bright_value, "%d%%", (int)v);
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) {
        return;
    }
    // Saved once on release, not on every step of the drag (NVS write per change).
    q->st.brightness = (uint8_t)v;
    if (s_has_backend) {
        if (s_backend.brightness) {
            s_backend.brightness((uint8_t)v, s_backend.ctx);
        }
    } else {
        s_mem.brightness = (uint8_t)v;
    }
}

static void qs_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    qs_t *q = ui_screen_state(s);
    styles_init();
    shell_close_on_swipe(root, LV_DIR_TOP);

    // Status line: battery, clock, phone link.
    q->battery = shell_label(root, "", UI_FONT_CAPTION, UI_COLOR_TEXT);
    lv_obj_align(q->battery, LV_ALIGN_TOP_LEFT, UI_SAFE_INSET + UI_SPACE_M, TOP_Y);
    lv_obj_t *clock = shell_label(root, "", UI_FONT_CAPTION, UI_COLOR_TEXT);
    ui_clock_bind_label(clock);
    lv_obj_align(clock, LV_ALIGN_TOP_MID, 0, TOP_Y);
    q->phone = shell_label(root, LV_SYMBOL_CALL, UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
    lv_obj_align(q->phone, LV_ALIGN_TOP_RIGHT, -(UI_SAFE_INSET + UI_SPACE_M), TOP_Y);

    lv_obj_t *grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(grid, 4 * BTN_D + 3 * GRID_GAP_X, 3 * BTN_D + 2 * GRID_GAP_Y);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, GRID_Y);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(grid, GRID_GAP_X, 0);
    lv_obj_set_style_pad_row(grid, GRID_GAP_Y, 0);
    for (int i = 0; i < SHELL_QS_COUNT; i++) {
        lv_obj_t *b = lv_obj_create(grid);
        lv_obj_remove_style_all(b);
        lv_obj_add_style(b, &s_btn, 0);
        lv_obj_add_style(b, &s_btn_on, LV_STATE_CHECKED);
        lv_obj_add_style(b, &s_btn_off, LV_STATE_USER_1);
        lv_obj_add_style(b, &s_btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(b, BTN_D, BTN_D);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *icon = lv_label_create(b);
        lv_label_set_text_static(icon, ITEMS[i].icon);
        lv_obj_center(icon);
        lv_obj_add_event_cb(b, btn_clicked, LV_EVENT_CLICKED, q);
        q->btn[i] = b;
    }

    lv_obj_t *bright = shell_label(root, "Brightness", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
    lv_obj_align(bright, LV_ALIGN_TOP_LEFT, UI_SAFE_INSET + UI_SPACE_M, BRIGHT_Y);
    q->bright_value = shell_label(root, "", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
    lv_obj_align(q->bright_value, LV_ALIGN_TOP_RIGHT, -(UI_SAFE_INSET + UI_SPACE_M), BRIGHT_Y);
    q->slider = lv_slider_create(root);
    lv_obj_set_size(q->slider, 4 * BTN_D + 3 * GRID_GAP_X - 2 * UI_SPACE_M, SLIDER_H);
    lv_obj_align(q->slider, LV_ALIGN_TOP_MID, 0, BRIGHT_Y + 44);
    // The bar is thin; the touch target stays >= 64 px.
    lv_obj_set_ext_click_area(q->slider, (UI_TOUCH_MIN - SLIDER_H) / 2);
    lv_obj_set_style_bg_color(q->slider, ui_color(UI_COLOR_SURFACE_HI), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(q->slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_slider_set_range(q->slider, 5, 100);
    lv_obj_add_event_cb(q->slider, slider_event, LV_EVENT_VALUE_CHANGED, q);
    lv_obj_add_event_cb(q->slider, slider_event, LV_EVENT_RELEASED, q);

    // Grabber: swipe up to close.
    lv_obj_t *grab = lv_obj_create(root);
    lv_obj_remove_style_all(grab);
    lv_obj_remove_flag(grab, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(grab, GRABBER_W, GRABBER_H);
    lv_obj_set_style_radius(grab, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(grab, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(grab, ui_color(UI_COLOR_SURFACE_HI), 0);
    lv_obj_align(grab, LV_ALIGN_BOTTOM_MID, 0, -UI_SAFE_INSET);

    read_state(q);
    show_state(q);
}

static void qs_resume(ui_screen_t *s)
{
    // Shown again (display on, an app opened from here closed): the state may have
    // changed elsewhere (settings, power menu).
    qs_t *q = ui_screen_state(s);
    read_state(q);
    show_state(q);
}

const screen_def_t shell_qs_screen = {
    .id = "qs",
    .on_create = qs_create,
    .on_resume = qs_resume,
    .flags = UI_SCREEN_NO_SWIPE_BACK, // closed by a swipe up
    .state_size = sizeof(qs_t),
};
