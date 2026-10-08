// Shell setup, the app registry with recent apps, and helpers shared by the shell
// screens (shell.h).
#include <stdio.h>
#include <string.h>

#include "battery_apps.h"
#include "clock_priv.h"
#include "connect_apps.h"
#include "s3w_edition.h"
#include "settings_apps.h"
#include "shell_priv.h"
#if S3W_EDITION_PRO
#include "find_apps.h"
#include "flashlight_apps.h"
#include "media_apps.h"
#include "weather_apps.h"
#include "calendar_apps.h"
#include "ha_apps.h"
#include "memo_apps.h"
#include "call_apps.h"
#include "notif_apps.h"
#endif
#include "ui_overlay.h"
#include "ui_theme.h"
#include "ui_widgets.h"

// System apps, in launcher order. Ids are the screen ids the apps will register
// (the complications open the same ones, wf_comp.c); until an app exists, opening
// it shows a toast. The Community edition leaves out the Pro apps (docs/10 §3).
static const shell_app_t SYSTEM_APPS[] = {
    {"alarms", "Alarms", LV_SYMBOL_BELL, 0xFF9F0A, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"timer", "Timer", LV_SYMBOL_LOOP, 0xFF9F0A, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"stopwatch", "Stopwatch", LV_SYMBOL_PLAY, 0xFF9F0A, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"world_clock", "World clock", LV_SYMBOL_GPS, 0x40C8E0, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"activity", "Activity", LV_SYMBOL_REFRESH, UI_COLOR_MOVE, SHELL_APP_SYSTEM, NULL, NULL, NULL},
#if S3W_EDITION_PRO
    {"heart_rate", "Heart rate", LV_SYMBOL_PLUS, UI_COLOR_DANGER, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"weather", "Weather", "\xEF\x86\x85" /* sun */, 0x40C8E0, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"media", "Media", LV_SYMBOL_AUDIO, 0xBF5AF2, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"memos", "Voice memos", LV_SYMBOL_AUDIO, UI_COLOR_DANGER, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"calendar", "Calendar", LV_SYMBOL_LIST, UI_COLOR_DANGER, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"home", "Home", LV_SYMBOL_HOME, 0xFF9F0A, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"notifications", "Notifications", LV_SYMBOL_ENVELOPE, 0x3D8BFF, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"calls", "Calls", LV_SYMBOL_CALL, UI_COLOR_SUCCESS, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"find_phone", "Find phone", LV_SYMBOL_CALL, UI_COLOR_SUCCESS, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"flashlight", "Flashlight", LV_SYMBOL_CHARGE, UI_COLOR_WARNING, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"qr_wallet", "QR wallet", LV_SYMBOL_IMAGE, 0x3D8BFF, SHELL_APP_SYSTEM, NULL, NULL, NULL},
#endif
    {"battery", "Battery", LV_SYMBOL_BATTERY_3, UI_COLOR_SUCCESS, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"face.picker", "Watch faces", LV_SYMBOL_HOME, 0x3D8BFF, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"settings", "Settings", LV_SYMBOL_SETTINGS, 0x8E8E93, SHELL_APP_SYSTEM, NULL, NULL, NULL},
    {"gallery", "Widgets", LV_SYMBOL_EDIT, 0x8E8E93, SHELL_APP_SYSTEM, NULL, NULL, NULL},
};

static const shell_app_t *s_apps[SHELL_APPS_MAX];
static size_t s_app_n;
static const shell_app_t *s_recent[SHELL_RECENT_MAX]; // newest first

// --- Pro licence (docs/10 §4) --------------------------------------------------------------

#if S3W_EDITION_PRO
// The Pro screens and apps (docs/10 §3); a sub-screen "<id>.x" is Pro too.
static const char *const PRO_SCREENS[] = {
    "heart_rate", "weather", "media", "memos", "calendar", "home", "notifications", "calls", "call",
    "find_phone", "find_watch", "flashlight", "qr_wallet", "wifi",
};
static bool s_pro_locked;

static void pro_locked_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)s;
    (void)args;
    lv_obj_t *e = s3w_empty_state_create(root, "PRO", "Pro feature", "Unlock it in the S3Wear Companion app");
    lv_obj_center(e);
}

static const screen_def_t s_pro_locked_screen = {
    .id = "pro.locked",
    .on_create = pro_locked_create,
};

static const screen_def_t *pro_gate(const screen_def_t *def)
{
    return s_pro_locked && shell_is_pro_screen(def->id) ? &s_pro_locked_screen : def;
}
#endif

bool shell_is_pro_screen(const char *id)
{
#if S3W_EDITION_PRO
    for (size_t i = 0; id && i < sizeof PRO_SCREENS / sizeof PRO_SCREENS[0]; i++) {
        const size_t n = strlen(PRO_SCREENS[i]);
        if (strncmp(id, PRO_SCREENS[i], n) == 0 && (id[n] == '\0' || id[n] == '.')) {
            return true;
        }
    }
#else
    (void)id;
#endif
    return false;
}

void shell_set_pro_locked(bool locked)
{
#if S3W_EDITION_PRO
    s_pro_locked = locked;
    ui_nav_set_gate(locked ? pro_gate : NULL);
#else
    (void)locked;
#endif
}

bool shell_pro_locked(void)
{
#if S3W_EDITION_PRO
    return s_pro_locked;
#else
    return false;
#endif
}

void shell_init(void)
{
    static bool s_done;
    if (s_done) {
        return;
    }
    s_done = true;
    for (size_t i = 0; i < sizeof SYSTEM_APPS / sizeof SYSTEM_APPS[0]; i++) {
        shell_app_register(&SYSTEM_APPS[i]);
    }
    ui_nav_register(&shell_qs_screen);
#if S3W_EDITION_PRO
    ui_nav_register(&shell_notif_screen);
#endif
    ui_nav_register(&shell_tiles_screen);
    ui_nav_register(&shell_launcher_screen);
    clock_apps_init(); // Alarms, Timer, Stopwatch, World clock (P3-07)
    battery_apps_init(); // Battery app, charging screen (P3-09)
    settings_apps_init(); // Settings app (P3-10)
#if S3W_EDITION_PRO
    find_apps_init();     // Find phone (P4-08)
    flashlight_apps_init(); // Flashlight (P8-15)
    media_apps_init();    // Media (P6-01)
    weather_apps_init();  // Weather, and the weather on faces and tiles (P6-02)
    calendar_apps_init(); // Calendar agenda, and the next event on faces and tiles (P6-03)
    call_apps_init();     // Calls: incoming and call screens, missed calls (P6-04)
    ha_apps_init();       // Home: Home Assistant entities (P9-05)
    memo_apps_init();     // Voice memos (P7-01)
#endif
    connect_apps_init();  // Settings > Connections (P4-09)
#if S3W_EDITION_PRO
    notif_apps_init();    // notification detail (P4-07)
#endif
    // docs/04 §3: the finger direction on the face -> the panel.
    ui_nav_set_home_swipe(LV_DIR_BOTTOM, &shell_qs_screen);
#if S3W_EDITION_PRO
    ui_nav_set_home_swipe(LV_DIR_TOP, &shell_notif_screen);
#endif
    ui_nav_set_home_swipe(LV_DIR_LEFT, &shell_tiles_screen);
    ui_nav_set_home_swipe(LV_DIR_RIGHT, &shell_launcher_screen);
}

// --- App registry ------------------------------------------------------------------------

esp_err_t shell_app_register(const shell_app_t *app)
{
    if (app == NULL || app->id == NULL || app->name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (shell_app_find(app->id)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_app_n >= SHELL_APPS_MAX) {
        return ESP_ERR_NO_MEM;
    }
    s_apps[s_app_n++] = app;
    return ESP_OK;
}

void shell_app_unregister(const char *id)
{
    const shell_app_t *app = shell_app_find(id);
    if (!app) {
        return;
    }
    size_t w = 0;
    for (size_t i = 0; i < s_app_n; i++) {
        if (s_apps[i] != app) {
            s_apps[w++] = s_apps[i];
        }
    }
    s_app_n = w;
    w = 0;
    for (size_t i = 0; i < SHELL_RECENT_MAX; i++) {
        if (s_recent[i] != app) {
            s_recent[w++] = s_recent[i];
        }
    }
    for (; w < SHELL_RECENT_MAX; w++) {
        s_recent[w] = NULL;
    }
}

size_t shell_app_count(void)
{
    return s_app_n;
}

const shell_app_t *shell_app_at(size_t index)
{
    return index < s_app_n ? s_apps[index] : NULL;
}

const shell_app_t *shell_app_find(const char *id)
{
    for (size_t i = 0; id && i < s_app_n; i++) {
        if (strcmp(s_apps[i]->id, id) == 0) {
            return s_apps[i];
        }
    }
    return NULL;
}

const shell_app_t *shell_recent_at(size_t index)
{
    return index < SHELL_RECENT_MAX ? s_recent[index] : NULL;
}

static void recent_add(const shell_app_t *app)
{
    size_t i = 0;
    while (i < SHELL_RECENT_MAX - 1 && s_recent[i] != app) {
        i++;
    }
    // Shift the newer entries down over the old position of app (or the oldest).
    for (; i > 0; i--) {
        s_recent[i] = s_recent[i - 1];
    }
    s_recent[0] = app;
}

void shell_app_open(const shell_app_t *app)
{
    if (app == NULL) {
        return;
    }
#if S3W_EDITION_PRO
    if (s_pro_locked && shell_is_pro_screen(app->id)) {
        ui_nav_push(&s_pro_locked_screen, NULL); // also for Pro apps without a screen yet
        return;
    }
#endif
    if (app->open) {
        recent_add(app);
        app->open(app);
        return;
    }
    if (ui_nav_push_id(app->id, NULL) == ESP_OK) {
        recent_add(app);
        return;
    }
    // System apps arrive with their tasks (P3-07 alarms, P3-10 settings, ...).
    char msg[48];
    snprintf(msg, sizeof msg, "%s: no app yet", app->name);
    ui_toast_show(msg, 0);
}

// --- Helpers -------------------------------------------------------------------------------

static void close_gesture(lv_event_t *e)
{
    const lv_dir_t want = (lv_dir_t)(intptr_t)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_gesture_dir(indev) == want) {
        lv_indev_wait_release(indev);
        ui_nav_back();
    }
}

void shell_close_on_swipe(lv_obj_t *root, lv_dir_t dir)
{
    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, close_gesture, LV_EVENT_GESTURE, (void *)(intptr_t)dir);
}

lv_obj_t *shell_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color_hex)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, ui_color(color_hex), 0);
    return l;
}

lv_obj_t *shell_app_icon_create(lv_obj_t *parent, const shell_app_t *app, int32_t d)
{
    if (app->image) {
        lv_obj_t *img = lv_image_create(parent);
        lv_image_set_src(img, app->image);
        lv_obj_set_size(img, d, d);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_set_style_image_recolor(img, lv_color_black(), LV_STATE_PRESSED);
        lv_obj_set_style_image_recolor_opa(img, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
        return img;
    }
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, d, d);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, ui_color(app->color), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *i = shell_label(c, app->icon, d >= 80 ? UI_FONT_TITLE : UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_center(i);
    return c;
}
