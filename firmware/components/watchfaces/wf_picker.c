// Face picker and customize (docs/04-ui-ux.md §4a, P3-04).
//
// Picker: one scaled live copy of a face at a time (wf_preview_create), swipe
// left/right to the next/previous face, tap the preview to wear it, "Customize" for
// its slots and colour. Customize: a list of the face's slots and its colour; a row
// opens a chooser. Changes apply at once (the home face rebuilds) and reach the
// listener (wf_set_listener), which saves them.
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ui_theme.h"
#include "ui_widgets.h"
#include "wf_priv.h"

#define PREVIEW_SCALE  144 // 256 = 1:1; 0.56 -> 231x283 px
#define PREVIEW_CY     238 // preview centre on the screen
#define PREVIEW_W      (410 * PREVIEW_SCALE / 256)
#define PREVIEW_H      (502 * PREVIEW_SCALE / 256)
#define FRAME_RADIUS   60 // glass corner, scaled with the preview
#define FRAME_BORDER   6
#define DOTS_Y         392
#define BUTTON_W       240

static bool customizable(const wf_face_def_t *def)
{
    return def->slot_count > 0 || (def->flags & WF_FACE_COLOR);
}

// --- Picker ------------------------------------------------------------------------------

typedef struct {
    lv_obj_t *frame; // full-size container drawn scaled; parent of the preview
    lv_obj_t *name;
    lv_obj_t *dots;
    lv_obj_t *customize;
    wf_face_t *preview;
    size_t index;
    bool stale; // config may have changed while covered
} picker_t;

static void picker_show(picker_t *st, size_t index)
{
    const wf_face_def_t *def = wf_at(index);
    if (def == NULL) {
        return;
    }
    st->index = index;
    wf_preview_delete(st->preview);
    st->preview = wf_preview_create(st->frame, def);
    s3w_label_set_fit_text(st->name, def->name);
    s3w_page_dots_set_active(st->dots, (uint8_t)index);
    if (customizable(def)) {
        lv_obj_remove_flag(st->customize, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(st->customize, LV_OBJ_FLAG_HIDDEN);
    }
}

static void picker_gesture(lv_event_t *e)
{
    picker_t *st = lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    // A swipe never ends in a click on the preview (even past the first/last face).
    lv_indev_wait_release(indev);
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT && st->index + 1 < wf_count()) {
        picker_show(st, st->index + 1);
    } else if (dir == LV_DIR_RIGHT && st->index > 0) {
        picker_show(st, st->index - 1);
    }
}

static void picker_select(lv_event_t *e)
{
    picker_t *st = lv_event_get_user_data(e);
    const wf_face_def_t *def = wf_at(st->index);
    if (def && wf_set_active(def->id) == ESP_OK) {
        wf_notify(WF_CHANGE_ACTIVE);
    }
    ui_nav_home();
}

static void picker_customize(lv_event_t *e)
{
    picker_t *st = lv_event_get_user_data(e);
    const wf_face_def_t *def = wf_at(st->index);
    if (def) {
        ui_nav_push(&wf_customize_screen, def->id);
    }
}

static void picker_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)args;
    picker_t *st = ui_screen_state(s);

    // Gesture target: the swipe bubbles up to this page from the preview and button.
    lv_obj_t *page = lv_obj_create(root);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(page, picker_gesture, LV_EVENT_GESTURE, st);

    st->name = wf_label(page, UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_obj_set_width(st->name, LV_PCT(80));
    lv_obj_set_style_text_align(st->name, LV_TEXT_ALIGN_CENTER, 0);
    s3w_label_fit(st->name);
    lv_obj_align(st->name, LV_ALIGN_TOP_MID, 0, UI_SAFE_INSET + UI_SPACE_S);

    // The face at full size, drawn scaled around its centre, inside a glass outline.
    st->frame = lv_obj_create(page);
    lv_obj_remove_style_all(st->frame);
    lv_obj_remove_flag(st->frame, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(st->frame, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(st->frame, 0, PREVIEW_CY - WF_CENTER_Y);
    lv_obj_set_style_bg_opa(st->frame, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(st->frame, ui_color(UI_COLOR_BG), 0);
    lv_obj_set_style_radius(st->frame, FRAME_RADIUS, 0);
    lv_obj_set_style_border_width(st->frame, FRAME_BORDER, 0);
    lv_obj_set_style_border_color(st->frame, ui_color(UI_COLOR_SURFACE_HI), 0);
    lv_obj_set_style_border_post(st->frame, true, 0);
    lv_obj_set_style_transform_pivot_x(st->frame, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(st->frame, LV_PCT(50), 0);
    lv_obj_set_style_transform_scale(st->frame, PREVIEW_SCALE, 0);

    // Tap target over the scaled preview (the face's own objects stay passive).
    lv_obj_t *hit = lv_obj_create(page);
    lv_obj_remove_style_all(hit);
    lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hit, PREVIEW_W, PREVIEW_H);
    lv_obj_set_pos(hit, WF_CENTER_X - PREVIEW_W / 2, PREVIEW_CY - PREVIEW_H / 2);
    lv_obj_add_event_cb(hit, picker_select, LV_EVENT_CLICKED, st);

    st->dots = s3w_page_dots_create(page, (uint8_t)wf_count());
    lv_obj_align(st->dots, LV_ALIGN_TOP_MID, 0, DOTS_Y);

    st->customize = s3w_button_create(page, S3W_BUTTON_SECONDARY, "Customize");
    lv_obj_set_width(st->customize, BUTTON_W);
    lv_obj_align(st->customize, LV_ALIGN_BOTTOM_MID, 0, -UI_SAFE_INSET);
    lv_obj_add_event_cb(st->customize, picker_customize, LV_EVENT_CLICKED, st);

    size_t index = 0;
    while (index < wf_count() && wf_at(index) != wf_active()) {
        index++;
    }
    picker_show(st, index < wf_count() ? index : 0);
}

static void picker_resume(ui_screen_t *s)
{
    picker_t *st = ui_screen_state(s);
    if (st->stale) {
        st->stale = false;
        picker_show(st, st->index);
    }
}

static void picker_pause(ui_screen_t *s)
{
    picker_t *st = ui_screen_state(s);
    st->stale = true;
}

static void picker_destroy(ui_screen_t *s)
{
    picker_t *st = ui_screen_state(s);
    wf_preview_delete(st->preview);
    st->preview = NULL;
}

const screen_def_t wf_picker_screen = {
    .id = "face.picker",
    .on_create = picker_create,
    .on_resume = picker_resume,
    .on_pause = picker_pause,
    .on_destroy = picker_destroy,
    .flags = UI_SCREEN_NO_SWIPE_BACK, // horizontal swipes page through faces
    .state_size = sizeof(picker_t),
};

// --- Chooser (one slot's complication, or the colour) --------------------------------------

#define CHOOSE_COLOR 0xFF // choose_args_t.slot: the colour
#define SWATCH       28

typedef struct {
    char face[WF_DECL_ID_MAX];
    uint8_t slot; // or CHOOSE_COLOR
} choose_args_t;

typedef struct {
    choose_args_t a;
} choose_t;

static void choose_clicked(lv_event_t *e)
{
    choose_t *st = lv_event_get_user_data(e);
    const int32_t v = (int32_t)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    esp_err_t err;
    if (st->a.slot == CHOOSE_COLOR) {
        err = wf_set_color(st->a.face, v < 0 ? WF_COLOR_DEFAULT : wf_colors[v].rgb);
    } else {
        err = wf_set_slot(st->a.face, st->a.slot, (wf_comp_t)v);
    }
    if (err == ESP_OK) {
        wf_notify(WF_CHANGE_CONFIG);
    }
    ui_nav_pop();
}

static lv_obj_t *choose_row(lv_obj_t *list, choose_t *st, const char *title, bool current, int32_t value)
{
    lv_obj_t *row = s3w_list_add_row(list, NULL, title, NULL, current ? LV_SYMBOL_OK : NULL);
    lv_obj_set_user_data(row, (void *)(intptr_t)value);
    lv_obj_add_event_cb(row, choose_clicked, LV_EVENT_CLICKED, st);
    return row;
}

static void swatch(lv_obj_t *row, lv_color_t c)
{
    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, SWATCH, SWATCH);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, c, 0);
    lv_obj_move_to_index(dot, 0);
}

static void choose_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    choose_t *st = ui_screen_state(s);
    st->a = *(const choose_args_t *)args;
    const wf_face_def_t *def = wf_find(st->a.face);
    lv_obj_t *list = s3w_list_create(root);
    if (def == NULL) {
        return;
    }
    lv_obj_t *current = NULL;
    if (st->a.slot == CHOOSE_COLOR) {
        s3w_header_create(list, "Colour");
        const uint32_t now = wf_get_color(def);
        lv_obj_t *row = choose_row(list, st, "Default", now == WF_COLOR_DEFAULT, -1);
        swatch(row, ui_theme_accent_color());
        current = now == WF_COLOR_DEFAULT ? row : current;
        for (size_t i = 0; i < wf_color_count; i++) {
            row = choose_row(list, st, wf_colors[i].name, now == wf_colors[i].rgb, (int32_t)i);
            swatch(row, ui_color(wf_colors[i].rgb));
            current = now == wf_colors[i].rgb ? row : current;
        }
    } else {
        s3w_header_create(list, "Complication");
        const wf_comp_t now = wf_get_slot(def, st->a.slot);
        for (int c = WF_COMP_NONE; c < WF_COMP_COUNT; c++) {
            lv_obj_t *row = choose_row(list, st, wf_comp_info((wf_comp_t)c)->name, c == (int)now, c);
            current = c == (int)now ? row : current;
        }
    }
    if (current) {
        lv_obj_update_layout(list);
        lv_obj_scroll_to_view(current, LV_ANIM_OFF);
    }
}

static const screen_def_t choose_screen = {
    .id = "face.choose",
    .on_create = choose_create,
    .state_size = sizeof(choose_t),
};

// --- Customize -----------------------------------------------------------------------------

typedef struct {
    char face[WF_DECL_ID_MAX];
    lv_obj_t *root;
    lv_obj_t *list;
    bool stale;
} customize_t;

/** "left" -> "Left", "steps_ring" -> "Steps ring". */
static void slot_title(const char *id, char *buf, size_t len)
{
    snprintf(buf, len, "%s", id);
    for (char *p = buf; *p; p++) {
        if (*p == '_' || *p == '-' || *p == '.') {
            *p = ' ';
        }
    }
    buf[0] = (char)toupper((unsigned char)buf[0]);
}

static void customize_row_clicked(lv_event_t *e)
{
    customize_t *st = lv_event_get_user_data(e);
    choose_args_t a = {.slot = (uint8_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e))};
    snprintf(a.face, sizeof a.face, "%s", st->face);
    ui_nav_push(&choose_screen, &a);
}

static void customize_build(customize_t *st)
{
    if (st->list) {
        lv_obj_delete(st->list);
    }
    st->list = s3w_list_create(st->root);
    const wf_face_def_t *def = wf_find(st->face);
    if (def == NULL) {
        s3w_empty_state_create(st->list, LV_SYMBOL_WARNING, "Face not found", NULL);
        return;
    }
    s3w_header_create(st->list, def->name);
    if (def->slot_count > 0) {
        s3w_list_add_section(st->list, "Complications");
    }
    for (uint8_t i = 0; i < def->slot_count; i++) {
        char title[WF_DECL_SLOT_ID_MAX];
        slot_title(def->slots[i].id, title, sizeof title);
        lv_obj_t *row = s3w_list_add_row(st->list, NULL, title, NULL, wf_comp_info(wf_get_slot(def, i))->name);
        lv_obj_set_user_data(row, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, customize_row_clicked, LV_EVENT_CLICKED, st);
    }
    if (def->flags & WF_FACE_COLOR) {
        s3w_list_add_section(st->list, "Style");
        const uint32_t c = wf_get_color(def);
        const char *name = "Default";
        for (size_t i = 0; i < wf_color_count; i++) {
            name = wf_colors[i].rgb == c ? wf_colors[i].name : name;
        }
        lv_obj_t *row = s3w_list_add_row(st->list, NULL, "Colour", NULL, name);
        swatch(row, c == WF_COLOR_DEFAULT ? ui_theme_accent_color() : ui_color(c));
        lv_obj_set_user_data(row, (void *)(uintptr_t)CHOOSE_COLOR);
        lv_obj_add_event_cb(row, customize_row_clicked, LV_EVENT_CLICKED, st);
    }
}

static void customize_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    customize_t *st = ui_screen_state(s);
    const wf_face_def_t *def = wf_active();
    snprintf(st->face, sizeof st->face, "%s", args ? (const char *)args : (def ? def->id : ""));
    st->root = root;
    customize_build(st);
}

static void customize_resume(ui_screen_t *s)
{
    customize_t *st = ui_screen_state(s);
    if (st->stale) {
        st->stale = false;
        customize_build(st);
    }
}

static void customize_pause(ui_screen_t *s)
{
    customize_t *st = ui_screen_state(s);
    st->stale = true;
}

const screen_def_t wf_customize_screen = {
    .id = "face.customize",
    .on_create = customize_create,
    .on_resume = customize_resume,
    .on_pause = customize_pause,
    .state_size = sizeof(customize_t),
};
