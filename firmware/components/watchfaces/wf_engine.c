// Watch face engine: registry, the home screen that hosts the active face, AOD
// variant, complication slots, per-face slot/colour configuration and data
// (wf_engine.h). Picker and customize screens: wf_picker.c.
#include <stdio.h>
#include <string.h>

#include "ui_overlay.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "wf_cfg.h"
#include "wf_shift.h"
#include "wf_priv.h"

#define SLOT_DEFAULT 0xFF
#define SECOND_MS    1000 // WF_FACE_SECONDS faces only, screen on and not AOD (second hand)

struct wf_face {
    const wf_face_def_t *def;
    lv_obj_t *root; // container for one build, child of the screen root
    void *state;
    bool aod;
    wf_ctx_t ctx;
    bool preview; // picker preview: static, slots not clickable, no hints
    lv_obj_t *slot_obj[WF_MAX_SLOTS];
    wf_comp_t slot_comp[WF_MAX_SLOTS];
};

static const wf_face_def_t *s_faces[WF_MAX_FACES];
static uint8_t s_slot_cfg[WF_MAX_FACES][WF_MAX_SLOTS]; // wf_comp_t or SLOT_DEFAULT
static uint32_t s_color[WF_MAX_FACES];                  // 0xRRGGBB or WF_COLOR_DEFAULT
static size_t s_face_n;
static const wf_face_def_t *s_active;
static bool s_aod;
static wf_data_t s_data;

static wf_face_t s_face;        // the face on the home screen (s_face.root != NULL while built)
static ui_screen_t *s_screen;   // home screen instance, NULL if none
static lv_timer_t *s_sec_timer; // only while a WF_FACE_SECONDS face is built, not in AOD
static uint32_t s_pending;      // changes while the face was not visible
static void (*s_listener)(wf_change_t what, void *ctx);
static void *s_listener_ctx;

const wf_color_t wf_colors[] = {
    {"Blue", 0x3D8BFF},   {"Green", 0x30D158}, {"Orange", 0xFF9F0A}, {"Pink", 0xFF375F},
    {"Purple", 0xBF5AF2}, {"Teal", 0x40C8E0},  {"Red", 0xFF453A},    {"Yellow", 0xFFD60A},
    {"White", 0xFFFFFF},
};
const size_t wf_color_count = sizeof wf_colors / sizeof wf_colors[0];

const wf_face_def_t *wf_face_def(const wf_face_t *f)
{
    return f->def;
}

bool wf_face_aod(const wf_face_t *f)
{
    return f->aod;
}

void *wf_face_state(const wf_face_t *f)
{
    return f->state;
}

const wf_ctx_t *wf_face_ctx(const wf_face_t *f)
{
    return &f->ctx;
}

lv_color_t wf_face_accent_or(const wf_face_t *f, lv_color_t def)
{
    const uint32_t c = wf_get_color(f->def);
    return c == WF_COLOR_DEFAULT ? def : ui_color(c);
}

lv_color_t wf_face_accent(const wf_face_t *f)
{
    return wf_face_accent_or(f, ui_theme_accent_color());
}

// --- Registry ----------------------------------------------------------------------------

static int face_index(const wf_face_def_t *def)
{
    for (size_t i = 0; i < s_face_n; i++) {
        if (s_faces[i] == def) {
            return (int)i;
        }
    }
    return -1;
}

esp_err_t wf_register(const wf_face_def_t *def)
{
    if (def == NULL || def->id == NULL || def->create == NULL || def->slot_count > WF_MAX_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (wf_find(def->id)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_face_n >= WF_MAX_FACES) {
        return ESP_ERR_NO_MEM;
    }
    memset(s_slot_cfg[s_face_n], SLOT_DEFAULT, WF_MAX_SLOTS);
    s_color[s_face_n] = WF_COLOR_DEFAULT;
    s_faces[s_face_n++] = def;
    return ESP_OK;
}

size_t wf_count(void)
{
    return s_face_n;
}

const wf_face_def_t *wf_at(size_t index)
{
    return index < s_face_n ? s_faces[index] : NULL;
}

const wf_face_def_t *wf_find(const char *id)
{
    for (size_t i = 0; id && i < s_face_n; i++) {
        if (strcmp(s_faces[i]->id, id) == 0) {
            return s_faces[i];
        }
    }
    return NULL;
}

wf_comp_t wf_get_slot(const wf_face_def_t *face, uint8_t slot)
{
    const int i = face_index(face);
    if (i < 0 || slot >= face->slot_count) {
        return WF_COMP_NONE;
    }
    return s_slot_cfg[i][slot] == SLOT_DEFAULT ? face->slots[slot].def : (wf_comp_t)s_slot_cfg[i][slot];
}

// --- Build and update ----------------------------------------------------------------------

static void ctx_refresh(wf_face_t *f)
{
    wf_ctx_init(&f->ctx, &s_data, ui_clock_now(), ui_clock_is_24h(), ui_clock_is_valid());
}

static void face_update(wf_face_t *f, uint32_t changed)
{
    if (f->root == NULL) {
        return;
    }
    ctx_refresh(f);
    if (f->def->update) {
        f->def->update(f, changed);
    }
    for (int i = 0; i < WF_MAX_SLOTS; i++) {
        if (f->slot_obj[i] && (wf_comp_info(f->slot_comp[i])->deps & changed)) {
            wf_comp_view_t v;
            wf_comp_render(f->slot_comp[i], &f->ctx, &v);
            wf_comp_widget_set(f->slot_obj[i], &v);
        }
    }
}

// AOD: move the face one burn-in step per minute (wf_shift.h).
static void aod_shift_apply(void)
{
    if (s_face.root && s_face.aod) {
        int8_t dx;
        int8_t dy;
        wf_aod_shift((uint32_t)(ui_clock_now() / 60), &dx, &dy);
        lv_obj_set_pos(s_face.root, dx, dy);
    }
}

static void update(uint32_t changed)
{
    face_update(&s_face, changed);
    if (changed & WF_DATA_TIME) {
        aod_shift_apply();
    }
}

/** Apply now if the face is visible, else when it becomes visible. */
static void changed(uint32_t mask)
{
    if (s_screen && ui_screen_is_visible(s_screen)) {
        update(mask | s_pending);
        s_pending = 0;
    } else {
        s_pending |= mask;
    }
}

static void second_cb(lv_timer_t *t)
{
    (void)t;
    changed(WF_DATA_SECOND);
}

static void sec_timer_sync(void)
{
    const bool want = s_face.root && !s_face.aod && (s_face.def->flags & WF_FACE_SECONDS);
    if (want && s_sec_timer == NULL) {
        s_sec_timer = lv_timer_create(second_cb, SECOND_MS, NULL);
    } else if (!want && s_sec_timer) {
        lv_timer_delete(s_sec_timer);
        s_sec_timer = NULL;
    }
    if (s_sec_timer) {
        if (s_screen && ui_screen_is_visible(s_screen)) {
            lv_timer_resume(s_sec_timer);
        } else {
            lv_timer_pause(s_sec_timer);
        }
    }
}

static void slot_clicked(lv_event_t *e)
{
    const int slot = (int)(intptr_t)lv_event_get_user_data(e);
    const wf_comp_info_t *info = wf_comp_info(s_face.slot_comp[slot]);
    if (info->app && ui_nav_push_id(info->app, NULL) == ESP_OK) {
        return;
    }
    // The app does not exist yet (system apps arrive with P3-06..P5).
    char msg[48];
    snprintf(msg, sizeof msg, "%s: no app yet", info->name);
    ui_toast_show(msg, 0);
}

static void face_teardown(wf_face_t *f)
{
    if (f->root) {
        lv_obj_delete(f->root);
    }
    lv_free(f->state);
    memset(f, 0, sizeof *f);
}

/** Long-press anywhere on the face reaches the screen root (picker). */
static void bubble_all(lv_obj_t *obj)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    const uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; i++) {
        bubble_all(lv_obj_get_child(obj, (int32_t)i));
    }
}

static void face_build(wf_face_t *f, const wf_face_def_t *def, bool aod, bool preview, lv_obj_t *parent)
{
    face_teardown(f);
    f->def = def;
    f->aod = aod;
    f->preview = preview;
    if (def->state_size > 0) {
        f->state = lv_malloc_zeroed(def->state_size);
    }
    f->root = lv_obj_create(parent);
    lv_obj_remove_style_all(f->root);
    lv_obj_remove_flag(f->root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(f->root, LV_PCT(100), LV_PCT(100));
    ctx_refresh(f);

    // Slots first, so face content (hands) draws over them. None in AOD.
    for (uint8_t i = 0; !aod && i < def->slot_count; i++) {
        f->slot_comp[i] = wf_get_slot(def, i);
        if (f->slot_comp[i] == WF_COMP_NONE) {
            continue;
        }
        f->slot_obj[i] = wf_comp_widget_create(f->root, &def->slots[i]);
        if (preview) {
            lv_obj_remove_flag(f->slot_obj[i], LV_OBJ_FLAG_CLICKABLE);
        } else {
            // SHORT_CLICKED: no app after a long-press (that opens the picker).
            lv_obj_add_event_cb(f->slot_obj[i], slot_clicked, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
        }
    }
    def->create(f, f->root);

    if (!aod && !preview) {
        // Time unknown (RTC lost power, never synced): say how to fix it.
        lv_obj_t *hint = wf_label(f->root, UI_FONT_CAPTION, UI_COLOR_WARNING);
        lv_label_set_text_static(hint, "Time not set");
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, UI_SAFE_INSET);
        ui_clock_bind_unknown(hint, true);
    }
    if (!preview) {
        bubble_all(f->root);
    }
    face_update(f, WF_DATA_ALL);
}

static void build(lv_obj_t *parent)
{
    face_build(&s_face, s_active, s_aod, false, parent);
    aod_shift_apply();
    s_pending = 0;
    sec_timer_sync();
}

wf_face_t *wf_preview_create(lv_obj_t *parent, const wf_face_def_t *def)
{
    wf_face_t *f = lv_malloc_zeroed(sizeof *f);
    if (f) {
        face_build(f, def, false, true, parent);
    }
    return f;
}

void wf_preview_delete(wf_face_t *f)
{
    if (f) {
        face_teardown(f);
        lv_free(f);
    }
}

static void rebuild(void)
{
    if (s_screen) {
        build(ui_screen_root(s_screen));
    }
}

// --- Home screen ---------------------------------------------------------------------------

static void home_long_pressed(lv_event_t *e)
{
    (void)e;
    // LVGL checks long-press time in the same input pass right after the gesture: a
    // slow swipe that opened a panel must not also open the picker over it.
    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) {
        return;
    }
    if (!s_aod && ui_nav_top() == s_screen) {
        ui_nav_push(&wf_picker_screen, NULL);
    }
}

/** Swipes open the panels around the face (quick settings, tiles, ...; P3-06). */
static void home_gesture(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL || s_aod) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (ui_nav_home_swipe(dir)) {
        // The release must not reach a complication as a click.
        lv_indev_wait_release(indev);
    }
}

static void home_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    s_screen = s;
    lv_obj_add_event_cb(root, home_long_pressed, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE); // swipes on the face end here
    lv_obj_add_event_cb(root, home_gesture, LV_EVENT_GESTURE, NULL);
    build(root);
}

static void home_resume(ui_screen_t *s)
{
    (void)s;
    update(s_pending | WF_DATA_TIME);
    s_pending = 0;
    sec_timer_sync();
}

static void home_pause(ui_screen_t *s)
{
    (void)s;
    sec_timer_sync();
}

static void home_destroy(ui_screen_t *s)
{
    (void)s;
    s_screen = NULL;
    if (s_sec_timer) {
        lv_timer_delete(s_sec_timer);
        s_sec_timer = NULL;
    }
    // The screen root and its children are deleted by the navigation after this.
    lv_free(s_face.state);
    memset(&s_face, 0, sizeof s_face);
}

const screen_def_t wf_home_screen = {
    .id = "face",
    .on_create = home_create,
    .on_resume = home_resume,
    .on_pause = home_pause,
    .on_destroy = home_destroy,
};

static void clock_changed(void *ctx)
{
    (void)ctx;
    changed(WF_DATA_TIME);
}

void wf_init(void)
{
    static bool s_done;
    if (s_done) {
        return;
    }
    s_done = true;
    wf_data_init(&s_data);
    wf_register(&wf_face_digital);
    wf_register(&wf_face_analog);
    wf_register(&wf_face_modular);
    wf_register(&wf_face_minimal);
    wf_decl_register_samples();
    s_active = s_faces[0];
    ui_nav_register(&wf_picker_screen);
    ui_nav_register(&wf_customize_screen);
    ui_clock_add_listener(clock_changed, NULL);
    ui_set_home(&wf_home_screen);
}

esp_err_t wf_set_active(const char *id)
{
    const wf_face_def_t *def = wf_find(id);
    if (def == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (def != s_active) {
        s_active = def;
        rebuild();
    }
    return ESP_OK;
}

const wf_face_def_t *wf_active(void)
{
    return s_active;
}

void wf_set_aod(bool aod)
{
    if (aod != s_aod) {
        s_aod = aod;
        rebuild();
    }
}

bool wf_is_aod(void)
{
    return s_aod;
}

esp_err_t wf_set_slot(const char *face_id, uint8_t slot, wf_comp_t comp)
{
    const wf_face_def_t *def = wf_find(face_id);
    if (def == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (slot >= def->slot_count || (unsigned)comp > WF_COMP_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool dflt = comp == WF_COMP_COUNT || comp == def->slots[slot].def;
    s_slot_cfg[face_index(def)][slot] = dflt ? SLOT_DEFAULT : (uint8_t)comp;
    if (def == s_active) {
        rebuild();
    }
    return ESP_OK;
}

esp_err_t wf_set_color(const char *face_id, uint32_t rgb)
{
    const wf_face_def_t *def = wf_find(face_id);
    if (def == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (rgb != WF_COLOR_DEFAULT && rgb > 0xFFFFFFu) {
        return ESP_ERR_INVALID_ARG;
    }
    s_color[face_index(def)] = rgb;
    if (def == s_active) {
        rebuild();
    }
    return ESP_OK;
}

uint32_t wf_get_color(const wf_face_def_t *face)
{
    const int i = face_index(face);
    return i < 0 ? WF_COLOR_DEFAULT : s_color[i];
}

// --- Saved configuration ---------------------------------------------------------------------

static bool cfg_item(const char *face, const char *key, const char *value, void *ctx)
{
    (void)ctx;
    const wf_face_def_t *def = wf_find(face);
    if (def == NULL) {
        return false;
    }
    const int fi = face_index(def);
    if (strcmp(key, WF_CFG_COLOR_KEY) == 0) {
        return wf_cfg_color_parse(value, &s_color[fi]);
    }
    for (uint8_t i = 0; i < def->slot_count; i++) {
        if (strcmp(def->slots[i].id, key) == 0) {
            const wf_comp_t c = wf_comp_find(value);
            if (c == WF_COMP_COUNT) {
                return false;
            }
            s_slot_cfg[fi][i] = c == def->slots[i].def ? SLOT_DEFAULT : (uint8_t)c;
            return true;
        }
    }
    return false;
}

int wf_config_load(const char *cfg)
{
    const int ai = face_index(s_active);
    uint8_t old_slots[WF_MAX_SLOTS] = {0};
    uint32_t old_color = 0;
    if (ai >= 0) {
        memcpy(old_slots, s_slot_cfg[ai], sizeof old_slots);
        old_color = s_color[ai];
    }
    for (size_t i = 0; i < s_face_n; i++) {
        memset(s_slot_cfg[i], SLOT_DEFAULT, WF_MAX_SLOTS);
        s_color[i] = WF_COLOR_DEFAULT;
    }
    const int bad = wf_cfg_parse(cfg, cfg_item, NULL);
    if (ai >= 0 && (memcmp(old_slots, s_slot_cfg[ai], sizeof old_slots) != 0 || old_color != s_color[ai])) {
        rebuild();
    }
    return bad;
}

esp_err_t wf_config_save(char *buf, size_t len)
{
    wf_cfg_writer_t w;
    wf_cfg_writer_init(&w, buf, len);
    bool ok = true;
    for (size_t i = 0; i < s_face_n; i++) {
        const wf_face_def_t *def = s_faces[i];
        for (uint8_t n = 0; n < def->slot_count; n++) {
            if (s_slot_cfg[i][n] != SLOT_DEFAULT) {
                ok &= wf_cfg_put(&w, def->id, def->slots[n].id, wf_comp_info((wf_comp_t)s_slot_cfg[i][n])->id);
            }
        }
        if (s_color[i] != WF_COLOR_DEFAULT) {
            char hex[8];
            wf_cfg_color_str(s_color[i], hex);
            ok &= wf_cfg_put(&w, def->id, WF_CFG_COLOR_KEY, hex);
        }
    }
    return ok ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

void wf_set_listener(void (*cb)(wf_change_t what, void *ctx), void *ctx)
{
    s_listener = cb;
    s_listener_ctx = ctx;
}

void wf_notify(wf_change_t what)
{
    if (s_listener) {
        s_listener(what, s_listener_ctx);
    }
}

wf_data_t *wf_data_edit(void)
{
    return &s_data;
}

const wf_data_t *wf_data_get(void)
{
    return &s_data;
}

void wf_data_changed(uint32_t mask)
{
    changed(mask);
}
