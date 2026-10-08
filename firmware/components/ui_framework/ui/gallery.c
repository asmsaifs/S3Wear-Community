// The widget gallery (P2-05) and ui_start(). Every gallery page is a
// registered screen so simulator scripts and the console can open it by id.
#include <stdio.h>

#include "ui_overlay.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "ui_widgets.h"

static void push_cb(lv_event_t *e)
{
    ui_nav_push_id(lv_event_get_user_data(e), NULL);
}

// --- Gallery index -------------------------------------------------------------------

typedef struct {
    const char *id;
    const char *icon;
    const char *title;
    const char *subtitle;
} page_t;

static const page_t PAGES[] = {
    {"gallery.buttons", LV_SYMBOL_OK, "Buttons", "Primary, secondary, danger"},
    {"gallery.lists", LV_SYMBOL_LIST, "Lists", "Rows, sections, toggles, sliders"},
    {"gallery.rings", LV_SYMBOL_REFRESH, "Rings & cards", "Activity rings, page dots"},
    {"gallery.picker", LV_SYMBOL_BELL, "Picker", "Time picker"},
    {"gallery.overlays", LV_SYMBOL_WARNING, "Overlays", "Toast, banner, alert, dialog"},
    {"gallery.qr", LV_SYMBOL_IMAGE, "QR code", NULL},
    {"gallery.empty", LV_SYMBOL_FILE, "Empty state", NULL},
    {"gallery.text", LV_SYMBOL_EDIT, "Typography", "Fonts and colours"},
};

static void gallery_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)s;
    (void)args;
    lv_obj_t *list = s3w_list_create(root);
    s3w_header_create(list, "Widgets");
    for (size_t i = 0; i < sizeof PAGES / sizeof PAGES[0]; i++) {
        lv_obj_t *row = s3w_list_add_row(list, PAGES[i].icon, PAGES[i].title, PAGES[i].subtitle, NULL);
        lv_obj_add_event_cb(row, push_cb, LV_EVENT_CLICKED, (void *)PAGES[i].id);
    }
}

/** Page skeleton: list with a header, returns the list. */
static lv_obj_t *page(lv_obj_t *root, const char *title)
{
    lv_obj_t *list = s3w_list_create(root);
    s3w_header_create(list, title);
    return list;
}

// --- Pages ---------------------------------------------------------------------------

static void buttons_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)s;
    (void)args;
    lv_obj_t *list = page(root, "Buttons");
    lv_obj_set_style_pad_row(list, UI_SPACE_M, 0);
    lv_obj_set_width(s3w_button_create(list, S3W_BUTTON_PRIMARY, "Primary"), LV_PCT(90));
    lv_obj_set_width(s3w_button_create(list, S3W_BUTTON_SECONDARY, "Secondary"), LV_PCT(90));
    lv_obj_set_width(s3w_button_create(list, S3W_BUTTON_DANGER, "Delete"), LV_PCT(90));
    lv_obj_t *dis = s3w_button_create(list, S3W_BUTTON_PRIMARY, "Disabled");
    lv_obj_set_width(dis, LV_PCT(90));
    lv_obj_add_state(dis, LV_STATE_DISABLED);
}

static void lists_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)s;
    (void)args;
    lv_obj_t *list = page(root, "Lists");
    s3w_list_add_row(list, LV_SYMBOL_WIFI, "Wi-Fi", "Off", NULL);
    s3w_list_add_row(list, LV_SYMBOL_BLUETOOTH, "Bluetooth", "Connected", NULL);
    s3w_list_add_row(list, LV_SYMBOL_BATTERY_3, "Battery", NULL, "80 %");
    s3w_list_add_section(list, "Display");
    s3w_toggle_row(list, LV_SYMBOL_EYE_OPEN, "Always on", false);
    s3w_toggle_row(list, LV_SYMBOL_REFRESH, "Raise to wake", true);
    s3w_slider_row(list, "Brightness", 5, 100, 60, " %");
}

static void rings_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)s;
    (void)args;
    lv_obj_t *list = page(root, "Rings & cards");
    lv_obj_t *card = s3w_card_create(list);
    lv_obj_set_size(card, LV_PCT(100), 240);
    static const struct {
        uint32_t color;
        int32_t pct;
    } RINGS[] = {{UI_COLOR_MOVE, 72}, {UI_COLOR_EXERCISE, 45}, {UI_COLOR_STAND, 90}};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *ring = s3w_ring_create(card, 192 - i * 56, 24, RINGS[i].color);
        s3w_ring_set_value(ring, RINGS[i].pct);
        lv_obj_align(ring, LV_ALIGN_LEFT_MID, i * 28, 0);
    }
    lv_obj_t *steps = lv_label_create(card);
    lv_label_set_text(steps, "7 214\nsteps");
    lv_obj_set_style_text_font(steps, UI_FONT_BODY, 0);
    lv_obj_align(steps, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *card2 = s3w_card_create(list);
    lv_obj_set_width(card2, LV_PCT(100));
    lv_obj_set_flex_flow(card2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card2, UI_SPACE_M, 0);
    lv_obj_t *t = lv_label_create(card2);
    lv_label_set_text(t, "Page dots");
    lv_obj_set_style_text_font(t, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(t, ui_color(UI_COLOR_TEXT_DIM), 0);
    s3w_page_dots_set_active(s3w_page_dots_create(card2, 5), 1);
}

static void picker_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)s;
    (void)args;
    lv_obj_t *list = page(root, "Picker");
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    s3w_picker_create(row, 0, 23, 1, 7, true);
    lv_obj_t *colon = lv_label_create(row);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, UI_FONT_TITLE, 0);
    s3w_picker_create(row, 0, 55, 5, 30, true);
    lv_obj_t *btn = s3w_button_create(list, S3W_BUTTON_PRIMARY, "Set");
    lv_obj_set_width(btn, LV_PCT(90));
}

// Overlays page

static void toast_cb(lv_event_t *e)
{
    (void)e;
    ui_toast_show("Saved", 0);
}

static void banner_cb(lv_event_t *e)
{
    (void)e;
    const ui_banner_t b = {
        .icon = LV_SYMBOL_ENVELOPE,
        .title = "Alex",
        .body = "Running 5 minutes late, order me a coffee please",
    };
    ui_banner_show(&b);
}

static void alert_result(int button, void *ctx)
{
    (void)ctx;
    ui_toast_show(button == 0 ? "Stopped" : button == 1 ? "Snoozed" : "Dismissed", 0);
}

static void alert_cb(lv_event_t *e)
{
    (void)e;
    const ui_alert_t a = {
        .icon = LV_SYMBOL_BELL,
        .title = "Alarm",
        .body = "07:30  Wake up",
        .accent = UI_COLOR_WARNING,
        .primary = "Stop",
        .secondary = "Snooze",
        .on_result = alert_result,
        .prio = UI_ALERT_PRIO_HIGH,
        .back_dismisses = false,
    };
    ui_alert_show(&a);
}

static void dialog_result(bool confirmed, void *ctx)
{
    (void)ctx;
    ui_toast_show(confirmed ? "Deleted" : "Kept", 0);
}

static void dialog_cb(lv_event_t *e)
{
    ui_screen_t *s = lv_event_get_user_data(e);
    s3w_dialog_show(ui_screen_root(s), "Delete memo?", "This cannot be undone.", "Delete", true, dialog_result,
                    NULL);
}

static void overlays_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    lv_obj_t *list = page(root, "Overlays");
    lv_obj_add_event_cb(s3w_list_add_row(list, LV_SYMBOL_OK, "Toast", NULL, NULL), toast_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s3w_list_add_row(list, LV_SYMBOL_ENVELOPE, "Banner", NULL, NULL), banner_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s3w_list_add_row(list, LV_SYMBOL_BELL, "Full-screen alert", NULL, NULL), alert_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s3w_list_add_row(list, LV_SYMBOL_TRASH, "Confirm dialog", NULL, NULL), dialog_cb,
                        LV_EVENT_CLICKED, s);
}

static void qr_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)s;
    (void)args;
    lv_obj_t *list = page(root, "QR code");
    s3w_qr_create(list, "https://github.com/s3wear", 240);
}

static void empty_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)s;
    (void)args;
    lv_obj_t *list = page(root, "Notifications");
    s3w_empty_state_create(list, LV_SYMBOL_BELL, "No notifications", "New notifications from your phone appear here.");
}

static void text_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)s;
    (void)args;
    lv_obj_t *list = page(root, "Typography");
    static const struct {
        const lv_font_t *font;
        uint32_t color;
        const char *text;
    } LINES[] = {
        {UI_FONT_DISPLAY, UI_COLOR_TEXT, "10:09"},
        {UI_FONT_TITLE, UI_COLOR_TEXT, "Title 32"},
        {UI_FONT_BODY, UI_COLOR_TEXT, "Body 26 Ünïcødé"},
        {UI_FONT_BODY, UI_COLOR_TEXT, "Ελληνικά Кириллица"},
        {UI_FONT_CAPTION, UI_COLOR_TEXT_DIM, "Caption 22 \xE2\x80\x94 dim"},
        {UI_FONT_CAPTION, UI_COLOR_SUCCESS, "Success"},
        {UI_FONT_CAPTION, UI_COLOR_WARNING, "Warning"},
        {UI_FONT_CAPTION, UI_COLOR_DANGER, "Danger"},
    };
    for (size_t i = 0; i < sizeof LINES / sizeof LINES[0]; i++) {
        lv_obj_t *l = lv_label_create(list);
        lv_label_set_text(l, LINES[i].text);
        lv_obj_set_style_text_font(l, LINES[i].font, 0);
        lv_obj_set_style_text_color(l, ui_color(LINES[i].color), 0);
    }
}

static const screen_def_t GALLERY[] = {
    {.id = "gallery", .on_create = gallery_create},
    {.id = "gallery.buttons", .on_create = buttons_create},
    {.id = "gallery.lists", .on_create = lists_create},
    {.id = "gallery.rings", .on_create = rings_create},
    {.id = "gallery.picker", .on_create = picker_create, .flags = UI_SCREEN_NO_SWIPE_BACK},
    {.id = "gallery.overlays", .on_create = overlays_create},
    {.id = "gallery.qr", .on_create = qr_create, .flags = UI_SCREEN_KEEP_ON},
    {.id = "gallery.empty", .on_create = empty_create},
    {.id = "gallery.text", .on_create = text_create},
};

void ui_gallery_register(void)
{
    for (size_t i = 0; i < sizeof GALLERY / sizeof GALLERY[0]; i++) {
        ui_nav_register(&GALLERY[i]);
    }
}

static const screen_def_t *s_home;

void ui_set_home(const screen_def_t *home)
{
    s_home = home;
}

esp_err_t ui_start(void)
{
    if (s_home == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    // Registering twice (ui_start after a clobber) is refused by the registry: harmless.
    ui_gallery_register();
    ui_nav_register(&ui_power_menu_screen);
    return ui_nav_init(s_home, NULL);
}
