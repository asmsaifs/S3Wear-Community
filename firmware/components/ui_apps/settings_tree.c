// Settings app tree (settings_tree.h). Pure C.
#include "settings_tree.h"

#include <string.h>

#define COUNT(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

// Symbols in the generated fonts only (tools/fonts/gen_fonts.sh). Written as the
// string literals LV_SYMBOL_* expand to, so this file stays free of LVGL.
#define ICON_EYE      "\xEF\x81\xAE"
#define ICON_VOLUME   "\xEF\x80\xA8"
#define ICON_BELL     "\xEF\x83\xB3"
#define ICON_HOME     "\xEF\x80\x95"
#define ICON_LIST     "\xEF\x80\x8B"
#define ICON_PLUS     "\xEF\x81\xA7"
#define ICON_BLUETOOTH "\xEF\x8A\x93"
#define ICON_BATTERY  "\xEF\x89\x80"
#define ICON_EYE_OFF  "\xEF\x81\xB0"
#define ICON_IMAGE    "\xEF\x80\xBE"
#define ICON_GPS      "\xEF\x84\xA4"
#define ICON_SETTINGS "\xEF\x80\x93"

// --- Option lists ---------------------------------------------------------------------------

static const settings_opt_t TIMEOUT_OPTS[] = {
    {"10 s", 10}, {"15 s", 15}, {"30 s", 30},
    {"1 min", 60}, {"2 min", 120}, {"5 min", 300},
};
static const settings_opt_t SENSITIVITY_OPTS[] = {{"Low", 0}, {"Medium", 1}, {"High", 2}};
static const settings_opt_t HAPTICS_OPTS[] = {{"Off", 0}, {"Light", 1}, {"Medium", 2}, {"Strong", 3}};
// Bit 0 = Sunday (alarm_sched.h, modes.h). Anything else reads "Custom".
static const settings_opt_t DAYS_OPTS[] = {{"Off", 0}, {"Every day", 0x7F}, {"Weekdays", 0x3E}, {"Weekends", 0x41}};

// --- Row builders (designated initialisers keep the tables readable) --------------------------

#define TOGGLE(t, id)              {.kind = SETTINGS_TOGGLE, .title = (t), .setting = S3W_SETTING_##id}
#define SLIDER(t, id, lo, hi, u)   {.kind = SETTINGS_SLIDER, .title = (t), .setting = S3W_SETTING_##id, .min = (lo), .max = (hi), .unit = (u)}
#define CHOICE(t, id, o)           {.kind = SETTINGS_CHOICE, .title = (t), .setting = S3W_SETTING_##id, .opts = (o), .n_opts = COUNT(o)}
#define TIME(t, id)                {.kind = SETTINGS_TIME, .title = (t), .setting = S3W_SETTING_##id}
#define PAGE(t, i, c)              {.kind = SETTINGS_PAGE, .title = (t), .icon = (i), .children = (c), .n_children = COUNT(c)}
#define APP(t, i, screen)          {.kind = SETTINGS_APP, .title = (t), .icon = (i), .arg = (screen)}
#define SOON(t, i)                 {.kind = SETTINGS_SOON, .title = (t), .icon = (i)}
#define ACTION(t, act, body, dng)  {.kind = SETTINGS_ACTION, .title = (t), .action = (act), .arg = (body), .danger = (dng)}
#define INFO_TEXT(t, text)         {.kind = SETTINGS_INFO, .title = (t), .arg = (text), .info = SETTINGS_INFO_TEXT}
#define INFO(t, what)              {.kind = SETTINGS_INFO, .title = (t), .info = (what)}

// --- Pages ------------------------------------------------------------------------------------

static const settings_node_t DISPLAY_ROWS[] = {
    SLIDER("Brightness", DISPLAY_BRIGHTNESS, 5, 100, "%"),
    CHOICE("Screen timeout", SCREEN_TIMEOUT_S, TIMEOUT_OPTS),
    TOGGLE("Always-on display", AOD),
    TOGGLE("Raise to wake", RAISE_TO_WAKE),
    CHOICE("Raise sensitivity", RAISE_SENSITIVITY, SENSITIVITY_OPTS),
    TOGGLE("Wake on tap", WAKE_ON_TAP),
};

static const settings_node_t SOUND_ROWS[] = {
    TOGGLE("Silent", SILENT),
    SLIDER("Media volume", VOLUME_MEDIA, 0, 100, "%"),
    SLIDER("System volume", VOLUME_SYSTEM, 0, 100, "%"),
    SLIDER("Alarm volume", VOLUME_ALARM, 10, 100, "%"),
    CHOICE("Haptics", HAPTICS_LEVEL, HAPTICS_OPTS),
};

static const settings_node_t DND_ROWS[] = {
    CHOICE("Days", DND_DAYS, DAYS_OPTS),
    TIME("From", DND_START),
    TIME("To", DND_END),
};

static const settings_node_t SLEEP_ROWS[] = {
    CHOICE("Days", SLEEP_DAYS, DAYS_OPTS),
    TIME("From", SLEEP_START),
    TIME("To", SLEEP_END),
};

static const settings_node_t NOTIFY_ROWS[] = {
    TOGGLE("Wake on alert", WAKE_ON_NOTIFY),
    PAGE("Do not disturb", NULL, DND_ROWS),
    PAGE("Sleep mode", NULL, SLEEP_ROWS),
};

static const settings_node_t CONNECT_ROWS[] = {
    SOON("Bluetooth", NULL),
    SOON("Wi-Fi", NULL),
    ACTION("Forget phone", SETTINGS_ACT_FORGET_PHONE, "The watch will need to be paired again.", true),
};

static const settings_node_t BATTERY_ROWS[] = {
    TOGGLE("Battery saver", BATTERY_SAVER),
    APP("Battery details", NULL, "battery"),
};

static const settings_node_t ACCESS_ROWS[] = {
    TOGGLE("Large text", LARGE_TEXT),
    TOGGLE("High contrast", HIGH_CONTRAST),
};

static const settings_node_t REGION_ROWS[] = {
    TOGGLE("24-hour clock", TIME_24H),
    {.kind = SETTINGS_ZONE, .title = "Time zone", .setting = S3W_SETTING_TIMEZONE},
    INFO_TEXT("Language", "English"),
};

static const settings_node_t ABOUT_ROWS[] = {
    INFO_TEXT("Model", "S3Wear"),
    INFO("Firmware", SETTINGS_INFO_VERSION),
    INFO("ESP-IDF", SETTINGS_INFO_IDF),
    INFO("Storage", SETTINGS_INFO_STORAGE),
};

static const settings_node_t DEV_ROWS[] = {
    TOGGLE("Console", DEV_CONSOLE),
    TOGGLE("Unsigned apps", DEV_UNSIGNED_APPS),
    TOGGLE("FPS overlay", DEV_FPS_OVERLAY),
};

static const settings_node_t SYSTEM_ROWS[] = {
    PAGE("About", NULL, ABOUT_ROWS),
    PAGE("Developer options", NULL, DEV_ROWS),
    ACTION("Restart", SETTINGS_ACT_RESTART, NULL, false),
    ACTION("Power off", SETTINGS_ACT_POWER_OFF, NULL, false),
    ACTION("Factory reset", SETTINGS_ACT_FACTORY_RESET, "Settings go back to their defaults and the watch restarts.", true),
};

// F18 order.
static const settings_node_t ROOT_ROWS[] = {
    PAGE("Display", ICON_EYE, DISPLAY_ROWS),
    PAGE("Sound", ICON_VOLUME, SOUND_ROWS),
    PAGE("Notifications", ICON_BELL, NOTIFY_ROWS),
    APP("Watch faces", ICON_HOME, "face.picker"),
    SOON("Apps", ICON_LIST),
    SOON("Health", ICON_PLUS),
    PAGE("Connections", ICON_BLUETOOTH, CONNECT_ROWS),
    PAGE("Battery", ICON_BATTERY, BATTERY_ROWS),
    SOON("Security", ICON_EYE_OFF),
    PAGE("Accessibility", ICON_IMAGE, ACCESS_ROWS),
    PAGE("Language & region", ICON_GPS, REGION_ROWS),
    PAGE("System", ICON_SETTINGS, SYSTEM_ROWS),
};

static const settings_node_t ROOT = PAGE("Settings", NULL, ROOT_ROWS);

const settings_node_t *settings_tree_root(void)
{
    return &ROOT;
}

int settings_opt_index(const settings_node_t *node, int32_t value)
{
    for (int i = 0; node && node->opts && i < node->n_opts; i++) {
        if (node->opts[i].value == value) {
            return i;
        }
    }
    return -1;
}

const char *settings_opt_label(const settings_node_t *node, int32_t value)
{
    const int i = settings_opt_index(node, value);
    return i < 0 ? NULL : node->opts[i].label;
}
