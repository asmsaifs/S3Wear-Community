// Launcher (docs/04 §3): swipe right on the face or BOOT on the home screen. A list
// with sections (Recent, System apps, Mini apps, Games) or a honeycomb grid of round
// icons; the button at the end switches between them (saved by the listener). A
// swipe left, BACK or PWR closes it.
#include "shell_priv.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define ROW_ICON_D  48
#define HEX_D       100 // grid icon diameter (>= 64 px touch target)
#define HEX_PITCH_X 116
#define HEX_PITCH_Y 100 // ~ HEX_PITCH_X * sqrt(3) / 2: rows interlock

static bool s_grid;
static void (*s_listener)(bool grid, void *ctx);
static void *s_listener_ctx;
static ui_screen_t *s_screen; // open launcher, NULL if none

typedef struct {
    lv_obj_t *list;
} launcher_t;

static void build(launcher_t *l, lv_obj_t *root);

void shell_launcher_set_grid(bool grid)
{
    if (grid == s_grid) {
        return;
    }
    s_grid = grid;
    if (s_screen) {
        build(ui_screen_state(s_screen), ui_screen_root(s_screen));
    }
}

bool shell_launcher_grid(void)
{
    return s_grid;
}

void shell_launcher_set_listener(void (*cb)(bool grid, void *ctx), void *ctx)
{
    s_listener = cb;
    s_listener_ctx = ctx;
}

static void app_clicked(lv_event_t *e)
{
    shell_app_open(lv_event_get_user_data(e));
}

static void layout_clicked(lv_event_t *e)
{
    (void)e;
    shell_launcher_set_grid(!s_grid);
    if (s_listener) {
        s_listener(s_grid, s_listener_ctx);
    }
}

static void add_row(lv_obj_t *list, const shell_app_t *app)
{
    lv_obj_t *row = s3w_list_add_row(list, NULL, app->name, NULL, NULL);
    lv_obj_t *icon = shell_app_icon_create(row, app, ROW_ICON_D);
    lv_obj_move_to_index(icon, 0);
    lv_obj_add_event_cb(row, app_clicked, LV_EVENT_CLICKED, (void *)app);
}

/** Apps of one kind as rows under a section title; false if there are none. */
static bool add_section(lv_obj_t *list, const char *title, shell_app_kind_t kind)
{
    bool any = false;
    for (size_t i = 0; i < shell_app_count(); i++) {
        const shell_app_t *app = shell_app_at(i);
        if (app->kind == kind) {
            if (!any) {
                s3w_list_add_section(list, title);
                any = true;
            }
            add_row(list, app);
        }
    }
    return any;
}

static void build_list(lv_obj_t *list)
{
    if (shell_recent_at(0)) {
        s3w_list_add_section(list, "Recent");
        for (size_t i = 0; shell_recent_at(i); i++) {
            add_row(list, shell_recent_at(i));
        }
    }
    add_section(list, "System apps", SHELL_APP_SYSTEM);
    if (!add_section(list, "Mini apps", SHELL_APP_MINI)) {
        s3w_list_add_section(list, "Mini apps");
        lv_obj_t *hint = shell_label(list, "Install mini apps from the phone app.", UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_pad_hor(hint, UI_SPACE_L, 0);
    }
    add_section(list, "Games", SHELL_APP_GAME);
}

/** Rows of 3 and 2 icons, interlocked. Every app, registry order. */
static void build_grid(lv_obj_t *list)
{
    const size_t n = shell_app_count();
    if (n == 0) {
        return;
    }
    size_t rows = 0;
    for (size_t placed = 0; placed < n; rows++) {
        placed += rows % 2 == 0 ? 3 : 2;
    }
    lv_obj_t *hive = lv_obj_create(list);
    lv_obj_remove_style_all(hive);
    lv_obj_remove_flag(hive, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(hive, LV_PCT(100), (int32_t)(rows - 1) * HEX_PITCH_Y + HEX_D);
    lv_obj_update_layout(list);
    const int32_t cx = lv_obj_get_content_width(list) / 2;
    size_t i = 0;
    for (size_t r = 0; r < rows; r++) {
        const int cols = r % 2 == 0 ? 3 : 2;
        for (int c = 0; c < cols && i < n; c++, i++) {
            const shell_app_t *app = shell_app_at(i);
            lv_obj_t *icon = shell_app_icon_create(hive, app, HEX_D);
            lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_opa(icon, LV_OPA_70, LV_STATE_PRESSED);
            const int32_t x = cx + (2 * c - (cols - 1)) * HEX_PITCH_X / 2;
            lv_obj_set_pos(icon, x - HEX_D / 2, (int32_t)r * HEX_PITCH_Y);
            lv_obj_add_event_cb(icon, app_clicked, LV_EVENT_CLICKED, (void *)app);
        }
    }
}

static void build(launcher_t *l, lv_obj_t *root)
{
    if (l->list) {
        // May run from a click on the layout button inside the old list.
        lv_obj_add_flag(l->list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(l->list);
    }
    l->list = s3w_list_create(root);
    s3w_header_create(l->list, "Apps");
    if (s_grid) {
        build_grid(l->list);
    } else {
        build_list(l->list);
    }
    lv_obj_t *btn = s3w_button_create(l->list, S3W_BUTTON_SECONDARY,
                                      s_grid ? LV_SYMBOL_LIST "  List view" : LV_SYMBOL_BARS "  Grid view");
    lv_obj_set_width(btn, LV_PCT(90));
    lv_obj_set_style_margin_top(btn, UI_SPACE_M, 0);
    lv_obj_add_event_cb(btn, layout_clicked, LV_EVENT_CLICKED, NULL);
}

static void launcher_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    s_screen = s;
    shell_close_on_swipe(root, LV_DIR_LEFT);
    build(ui_screen_state(s), root);
}

static void launcher_destroy(ui_screen_t *s)
{
    if (s_screen == s) {
        s_screen = NULL;
    }
}

const screen_def_t shell_launcher_screen = {
    .id = "launcher",
    .on_create = launcher_create,
    .on_destroy = launcher_destroy,
    .flags = UI_SCREEN_NO_SWIPE_BACK, // closed by a swipe left
    .state_size = sizeof(launcher_t),
};
