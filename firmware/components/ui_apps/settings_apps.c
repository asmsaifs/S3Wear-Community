// On-watch Settings app (settings_apps.h): draws the settings_tree.h tree with the
// standard list widgets. One page screen for every PAGE node, a choice screen for
// CHOICE and ZONE rows and a time screen for TIME rows.
#include <stdio.h>
#include <string.h>

#include "settings_apps.h"
#include "shell_priv.h"
#include "ui_overlay.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "world_clock.h"

static const screen_def_t settings_screen;
static const screen_def_t settings_page_screen;
static const screen_def_t settings_choice_screen;
static const screen_def_t settings_time_screen;

// --- Backend --------------------------------------------------------------------------------

static int32_t stub_get_int(s3w_setting_t id, void *ctx)
{
    (void)id;
    (void)ctx;
    return 0;
}

static esp_err_t stub_set_int(s3w_setting_t id, int32_t v, void *ctx)
{
    (void)id;
    (void)v;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

static void stub_get_str(s3w_setting_t id, char *buf, size_t len, void *ctx)
{
    (void)id;
    (void)ctx;
    snprintf(buf, len, "%s", "");
}

static esp_err_t stub_set_str(s3w_setting_t id, const char *v, void *ctx)
{
    (void)id;
    (void)v;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t stub_action(settings_action_t a, void *ctx)
{
    (void)a;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

static void stub_about(settings_about_t *out, void *ctx)
{
    (void)ctx;
    memset(out, 0, sizeof *out);
}

static settings_app_backend_t s_be = {
    .get_int = stub_get_int,
    .set_int = stub_set_int,
    .get_str = stub_get_str,
    .set_str = stub_set_str,
    .action = stub_action,
    .about = stub_about,
};

void settings_apps_set_backend(const settings_app_backend_t *b)
{
    s_be.get_int = b && b->get_int ? b->get_int : stub_get_int;
    s_be.set_int = b && b->set_int ? b->set_int : stub_set_int;
    s_be.get_str = b && b->get_str ? b->get_str : stub_get_str;
    s_be.set_str = b && b->set_str ? b->set_str : stub_set_str;
    s_be.action = b && b->action ? b->action : stub_action;
    s_be.about = b && b->about ? b->about : stub_about;
    s_be.ctx = b ? b->ctx : NULL;
}

void settings_apps_init(void)
{
    static bool s_done;
    if (s_done) {
        return;
    }
    s_done = true;
    ui_nav_register(&settings_screen);
    ui_nav_register(&settings_page_screen);
    ui_nav_register(&settings_choice_screen);
    ui_nav_register(&settings_time_screen);
}

// --- Helpers ----------------------------------------------------------------------------------

static void save_failed(esp_err_t err)
{
    ui_toast_show(err == ESP_ERR_NOT_SUPPORTED ? "Not available" : "Could not save", 0);
}

static void bottom_pad(lv_obj_t *list)
{
    lv_obj_t *pad = lv_obj_create(list);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, 1, UI_SPACE_XL);
    lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static const world_city_t *zone_city(const char *tz)
{
    for (size_t i = 0; i < world_city_count(); i++) {
        if (strcmp(world_city_at(i)->tz, tz) == 0) {
            return world_city_at(i);
        }
    }
    return NULL;
}

// Zone options: UTC, then every world clock city (several share a rule; the first is shown).
static size_t zone_count(void)
{
    return 1 + world_city_count();
}

static const char *zone_name(size_t i)
{
    return i == 0 ? "UTC" : world_city_at(i - 1)->name;
}

static const char *zone_tz(size_t i)
{
    return i == 0 ? "UTC0" : world_city_at(i - 1)->tz;
}

static void zone_value_text(char *buf, size_t len)
{
    char tz[64];
    s_be.get_str(S3W_SETTING_TIMEZONE, tz, sizeof tz, s_be.ctx);
    const world_city_t *c = zone_city(tz);
    // A long custom TZ string is cut to the buffer (a plain copy: snprintf("%s") trips
    // -Wformat-truncation in -O2 release builds).
    const char *v = strcmp(tz, "UTC0") == 0 ? "UTC" : c ? c->name : tz;
    if (len) {
        const size_t n = strnlen(v, len - 1);
        memcpy(buf, v, n);
        buf[n] = '\0';
    }
}

static void time_text(int32_t minute_of_day, char *buf, size_t len)
{
    const int h = (int)(minute_of_day / 60);
    const int m = (int)(minute_of_day % 60);
    if (ui_clock_is_24h()) {
        snprintf(buf, len, "%02d:%02d", h, m);
    } else {
        snprintf(buf, len, "%d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
    }
}

static void info_text(const settings_node_t *n, char *buf, size_t len)
{
    settings_about_t a;
    s_be.about(&a, s_be.ctx);
    const char *t = n->arg;
    switch (n->info) {
    case SETTINGS_INFO_VERSION:
        t = a.version;
        break;
    case SETTINGS_INFO_IDF:
        t = a.idf;
        break;
    case SETTINGS_INFO_STORAGE:
        t = a.storage;
        break;
    default:
        break;
    }
    snprintf(buf, len, "%s", t && t[0] ? t : "--");
}

// --- Page ---------------------------------------------------------------------------------------

typedef struct {
    ui_screen_t *screen;
    const settings_node_t *node;
    lv_obj_t *list;
    bool shown; // on_resume has run once: later ones refresh
} page_t;

static const settings_node_t *node_of(lv_event_t *e)
{
    return lv_obj_get_user_data(lv_event_get_current_target_obj(e));
}

static void page_clicked(lv_event_t *e)
{
    ui_nav_push(&settings_page_screen, node_of(e));
}

static void app_clicked(lv_event_t *e)
{
    const settings_node_t *n = node_of(e);
    if (ui_nav_push_id(n->arg, NULL) != ESP_OK) {
        char msg[48];
        snprintf(msg, sizeof msg, "%s: no app yet", n->title);
        ui_toast_show(msg, 0);
    }
}

static void soon_clicked(lv_event_t *e)
{
    char msg[48];
    snprintf(msg, sizeof msg, "%s: not available yet", node_of(e)->title);
    ui_toast_show(msg, 0);
}

static void choice_clicked(lv_event_t *e)
{
    ui_nav_push(&settings_choice_screen, node_of(e));
}

static void time_clicked(lv_event_t *e)
{
    ui_nav_push(&settings_time_screen, node_of(e));
}

static void toggle_changed(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    const settings_node_t *n = lv_obj_get_user_data(sw);
    const esp_err_t err = s_be.set_int(n->setting, lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0, s_be.ctx);
    if (err != ESP_OK) {
        save_failed(err);
    }
}

static void slider_released(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target_obj(e);
    const settings_node_t *n = lv_obj_get_user_data(sl);
    const esp_err_t err = s_be.set_int(n->setting, lv_slider_get_value(sl), s_be.ctx);
    if (err != ESP_OK) {
        save_failed(err);
    }
}

static void action_confirmed(bool ok, void *ctx)
{
    const settings_node_t *n = ctx;
    if (!ok) {
        return;
    }
    const esp_err_t err = s_be.action(n->action, s_be.ctx);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        ui_toast_show("Not available yet", 0);
    } else if (err != ESP_OK) {
        ui_toast_show("Failed", 0);
    }
}

static void action_clicked(lv_event_t *e)
{
    page_t *p = lv_event_get_user_data(e);
    const settings_node_t *n = node_of(e);
    char title[40];
    snprintf(title, sizeof title, "%s?", n->title);
    s3w_dialog_show(ui_screen_root(p->screen), title, n->arg, n->title, n->danger, action_confirmed, (void *)n);
}

static lv_obj_t *add_row(page_t *p, const settings_node_t *n)
{
    lv_obj_t *list = p->list;
    char buf[48];
    lv_obj_t *row = NULL;
    switch (n->kind) {
    case SETTINGS_PAGE:
        row = s3w_list_add_row(list, n->icon, n->title, NULL, LV_SYMBOL_RIGHT);
        lv_obj_add_event_cb(row, page_clicked, LV_EVENT_CLICKED, NULL);
        break;
    case SETTINGS_APP:
        row = s3w_list_add_row(list, n->icon, n->title, NULL, LV_SYMBOL_RIGHT);
        lv_obj_add_event_cb(row, app_clicked, LV_EVENT_CLICKED, NULL);
        break;
    case SETTINGS_SOON:
        row = s3w_list_add_row(list, n->icon, n->title, NULL, "Soon");
        lv_obj_add_event_cb(row, soon_clicked, LV_EVENT_CLICKED, NULL);
        break;
    case SETTINGS_TOGGLE: {
        lv_obj_t *sw = s3w_toggle_row(list, NULL, n->title, s_be.get_int(n->setting, s_be.ctx) != 0);
        lv_obj_set_user_data(sw, (void *)n);
        lv_obj_add_event_cb(sw, toggle_changed, LV_EVENT_VALUE_CHANGED, NULL);
        return sw;
    }
    case SETTINGS_SLIDER: {
        const int32_t v = LV_CLAMP(n->min, s_be.get_int(n->setting, s_be.ctx), n->max);
        lv_obj_t *sl = s3w_slider_row(list, n->title, n->min, n->max, v, n->unit);
        lv_obj_set_user_data(sl, (void *)n);
        lv_obj_add_event_cb(sl, slider_released, LV_EVENT_RELEASED, NULL);
        return sl;
    }
    case SETTINGS_CHOICE: {
        const char *l = settings_opt_label(n, s_be.get_int(n->setting, s_be.ctx));
        row = s3w_list_add_row(list, NULL, n->title, NULL, l ? l : "Custom");
        lv_obj_add_event_cb(row, choice_clicked, LV_EVENT_CLICKED, NULL);
        break;
    }
    case SETTINGS_ZONE:
        zone_value_text(buf, sizeof buf);
        row = s3w_list_add_row(list, NULL, n->title, NULL, buf);
        lv_obj_add_event_cb(row, choice_clicked, LV_EVENT_CLICKED, NULL);
        break;
    case SETTINGS_TIME:
        time_text(s_be.get_int(n->setting, s_be.ctx), buf, sizeof buf);
        row = s3w_list_add_row(list, NULL, n->title, NULL, buf);
        lv_obj_add_event_cb(row, time_clicked, LV_EVENT_CLICKED, NULL);
        break;
    case SETTINGS_ACTION:
        row = s3w_list_add_row(list, NULL, n->title, NULL, NULL);
        lv_obj_add_event_cb(row, action_clicked, LV_EVENT_CLICKED, p);
        if (n->danger) {
            lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_child(row, 0), 0), ui_color(UI_COLOR_DANGER), 0);
        }
        break;
    case SETTINGS_INFO:
        info_text(n, buf, sizeof buf);
        row = s3w_list_add_row(list, NULL, n->title, NULL, buf);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
        break;
    }
    if (row) {
        lv_obj_set_user_data(row, (void *)n);
    }
    return row;
}

static void page_build(page_t *p)
{
    lv_obj_update_layout(p->list);
    const int32_t y = lv_obj_get_scroll_y(p->list);
    lv_obj_clean(p->list);
    s3w_header_create(p->list, p->node->title);
    for (uint8_t i = 0; i < p->node->n_children; i++) {
        add_row(p, &p->node->children[i]);
    }
    bottom_pad(p->list);
    lv_obj_update_layout(p->list);
    lv_obj_scroll_to_y(p->list, y, LV_ANIM_OFF);
}

static void page_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    page_t *p = ui_screen_state(s);
    p->screen = s;
    p->node = args ? args : settings_tree_root();
    p->list = s3w_list_create(root);
    page_build(p);
}

static void page_resume(ui_screen_t *s)
{
    page_t *p = ui_screen_state(s);
    if (p->shown) {
        page_build(p); // back from a choice or time screen: the value changed
    }
    p->shown = true;
}

static const screen_def_t settings_screen = {
    .id = "settings",
    .on_create = page_create,
    .on_resume = page_resume,
    .state_size = sizeof(page_t),
};

static const screen_def_t settings_page_screen = {
    .id = "settings.page",
    .on_create = page_create,
    .on_resume = page_resume,
    .state_size = sizeof(page_t),
};

// --- Choice ---------------------------------------------------------------------------------------

typedef struct {
    const settings_node_t *node;
} choice_t;

static void choice_picked(lv_event_t *e)
{
    const choice_t *c = lv_event_get_user_data(e);
    const size_t i = (size_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    const settings_node_t *n = c->node;
    const esp_err_t err = n->kind == SETTINGS_ZONE ? s_be.set_str(n->setting, zone_tz(i), s_be.ctx)
                                                   : s_be.set_int(n->setting, n->opts[i].value, s_be.ctx);
    if (err != ESP_OK) {
        save_failed(err);
        return;
    }
    ui_nav_back();
}

static void choice_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    choice_t *c = ui_screen_state(s);
    c->node = args;
    const settings_node_t *n = c->node;
    lv_obj_t *list = s3w_list_create(root);
    s3w_header_create(list, n->title);

    const bool zone = n->kind == SETTINGS_ZONE;
    const size_t count = zone ? zone_count() : n->n_opts;
    char tz[64] = "";
    size_t cur = (size_t)-1;
    if (zone) {
        s_be.get_str(n->setting, tz, sizeof tz, s_be.ctx);
        for (size_t i = 0; i < count && cur == (size_t)-1; i++) {
            if (strcmp(zone_tz(i), tz) == 0) {
                cur = i;
            }
        }
    } else {
        const int idx = settings_opt_index(n, s_be.get_int(n->setting, s_be.ctx));
        cur = idx < 0 ? (size_t)-1 : (size_t)idx;
    }
    lv_obj_t *cur_row = NULL;
    for (size_t i = 0; i < count; i++) {
        lv_obj_t *row = s3w_list_add_row(list, NULL, zone ? zone_name(i) : n->opts[i].label, NULL,
                                         i == cur ? LV_SYMBOL_OK : NULL);
        lv_obj_set_user_data(row, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, choice_picked, LV_EVENT_CLICKED, c);
        if (i == cur) {
            cur_row = row;
        }
    }
    bottom_pad(list);
    if (cur_row) {
        lv_obj_update_layout(list);
        lv_obj_scroll_to_view(cur_row, LV_ANIM_OFF);
    }
}

static const screen_def_t settings_choice_screen = {
    .id = "settings.choice",
    .on_create = choice_create,
    .state_size = sizeof(choice_t),
};

// --- Time -----------------------------------------------------------------------------------------

typedef struct {
    const settings_node_t *node;
    lv_obj_t *hour;
    lv_obj_t *minute;
} time_screen_t;

static void time_save(lv_event_t *e)
{
    const time_screen_t *t = lv_event_get_user_data(e);
    const int32_t v = s3w_picker_get_value(t->hour) * 60 + s3w_picker_get_value(t->minute);
    const esp_err_t err = s_be.set_int(t->node->setting, v, s_be.ctx);
    if (err != ESP_OK) {
        save_failed(err);
        return;
    }
    ui_nav_back();
}

static void time_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    time_screen_t *t = ui_screen_state(s);
    t->node = args;
    const int32_t v = s_be.get_int(t->node->setting, s_be.ctx);
    lv_obj_t *list = s3w_list_create(root);
    s3w_header_create(list, t->node->title);

    // 24 h pickers in either clock format, as in the alarm editor.
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, UI_SPACE_S, 0);
    t->hour = s3w_picker_create(row, 0, 23, 1, v / 60, true);
    shell_label(row, ":", UI_FONT_TITLE, UI_COLOR_TEXT);
    t->minute = s3w_picker_create(row, 0, 59, 1, v % 60, true);

    lv_obj_t *save = s3w_button_create(list, S3W_BUTTON_PRIMARY, "Save");
    lv_obj_set_width(save, LV_PCT(80));
    lv_obj_set_style_margin_top(save, UI_SPACE_M, 0);
    lv_obj_add_event_cb(save, time_save, LV_EVENT_CLICKED, t);
    bottom_pad(list);
}

static const screen_def_t settings_time_screen = {
    .id = "settings.time",
    .on_create = time_create,
    .flags = UI_SCREEN_NO_SWIPE_BACK,
    .state_size = sizeof(time_screen_t),
};
