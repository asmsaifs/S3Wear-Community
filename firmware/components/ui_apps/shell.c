// Shell setup, the app registry with recent apps, and helpers shared by the shell
// screens (shell.h).
#include <stdio.h>
#include <string.h>

#include "battery_apps.h"
#include "clock_priv.h"
#include "settings_apps.h"
#include "shell_priv.h"
#include "ui_overlay.h"
#include "ui_theme.h"

// System apps, in launcher order. Ids are the screen ids the apps will register
// (the complications open the same ones, wf_comp.c); until an app exists, opening
// it shows a toast.
static const shell_app_t SYSTEM_APPS[] = {
    {"alarms", "Alarms", LV_SYMBOL_BELL, 0xFF9F0A, SHELL_APP_SYSTEM},
    {"timer", "Timer", LV_SYMBOL_LOOP, 0xFF9F0A, SHELL_APP_SYSTEM},
    {"stopwatch", "Stopwatch", LV_SYMBOL_PLAY, 0xFF9F0A, SHELL_APP_SYSTEM},
    {"world_clock", "World clock", LV_SYMBOL_GPS, 0x40C8E0, SHELL_APP_SYSTEM},
    {"activity", "Activity", LV_SYMBOL_REFRESH, UI_COLOR_MOVE, SHELL_APP_SYSTEM},
    {"heart_rate", "Heart rate", LV_SYMBOL_PLUS, UI_COLOR_DANGER, SHELL_APP_SYSTEM},
    {"weather", "Weather", LV_SYMBOL_TINT, 0x40C8E0, SHELL_APP_SYSTEM},
    {"media", "Media", LV_SYMBOL_AUDIO, 0xBF5AF2, SHELL_APP_SYSTEM},
    {"calendar", "Calendar", LV_SYMBOL_LIST, UI_COLOR_DANGER, SHELL_APP_SYSTEM},
    {"notifications", "Notifications", LV_SYMBOL_ENVELOPE, 0x3D8BFF, SHELL_APP_SYSTEM},
    {"find_phone", "Find phone", LV_SYMBOL_CALL, UI_COLOR_SUCCESS, SHELL_APP_SYSTEM},
    {"flashlight", "Flashlight", LV_SYMBOL_CHARGE, UI_COLOR_WARNING, SHELL_APP_SYSTEM},
    {"qr_wallet", "QR wallet", LV_SYMBOL_IMAGE, 0x3D8BFF, SHELL_APP_SYSTEM},
    {"level", "Level", LV_SYMBOL_MINUS, UI_COLOR_SUCCESS, SHELL_APP_SYSTEM},
    {"battery", "Battery", LV_SYMBOL_BATTERY_3, UI_COLOR_SUCCESS, SHELL_APP_SYSTEM},
    {"face.picker", "Watch faces", LV_SYMBOL_HOME, 0x3D8BFF, SHELL_APP_SYSTEM},
    {"settings", "Settings", LV_SYMBOL_SETTINGS, 0x8E8E93, SHELL_APP_SYSTEM},
    {"gallery", "Widgets", LV_SYMBOL_EDIT, 0x8E8E93, SHELL_APP_SYSTEM},
};

static const shell_app_t *s_apps[SHELL_APPS_MAX];
static size_t s_app_n;
static const shell_app_t *s_recent[SHELL_RECENT_MAX]; // newest first

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
    ui_nav_register(&shell_notif_screen);
    ui_nav_register(&shell_tiles_screen);
    ui_nav_register(&shell_launcher_screen);
    clock_apps_init(); // Alarms, Timer, Stopwatch, World clock (P3-07)
    battery_apps_init(); // Battery app, charging screen (P3-09)
    settings_apps_init(); // Settings app (P3-10)
    // docs/04 §3: the finger direction on the face -> the panel.
    ui_nav_set_home_swipe(LV_DIR_BOTTOM, &shell_qs_screen);
    ui_nav_set_home_swipe(LV_DIR_TOP, &shell_notif_screen);
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
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, d, d);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, ui_color(app->color), 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *i = shell_label(c, app->icon, d >= 80 ? UI_FONT_TITLE : UI_FONT_BODY, UI_COLOR_TEXT);
    lv_obj_center(i);
    return c;
}
