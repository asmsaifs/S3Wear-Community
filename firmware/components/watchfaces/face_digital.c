// Digital Bold: hours over minutes in 160 px bold digits, date on top, one slot.
// AOD: light 96 px "HH:MM" and the date, dimmed.
#include <stdio.h>

#include "ui_theme.h"
#include "wf_priv.h"

typedef struct {
    lv_obj_t *hours;
    lv_obj_t *minutes;
    lv_obj_t *time; // AOD
    lv_obj_t *date;
} digital_t;

static const wf_slot_def_t SLOTS[] = {
    {.id = "bottom", .x = WF_CENTER_X, .y = 448, .style = WF_SLOT_LINE, .def = WF_COMP_BATTERY},
};

static void create(wf_face_t *f, lv_obj_t *root)
{
    digital_t *st = wf_face_state(f);
    if (wf_face_aod(f)) {
        st->time = wf_label(root, ui_font_tabular(UI_FONT_DISPLAY_LIGHT), UI_COLOR_TEXT_DIM);
        lv_obj_align(st->time, LV_ALIGN_CENTER, 0, -20);
        st->date = wf_label(root, UI_FONT_CAPTION, UI_COLOR_TEXT_DIM);
        lv_obj_align(st->date, LV_ALIGN_CENTER, 0, 52);
        return;
    }
    st->date = wf_label(root, UI_FONT_BODY, UI_COLOR_TEXT_DIM);
    lv_obj_align(st->date, LV_ALIGN_TOP_MID, 0, 40);
    st->hours = wf_label(root, ui_font_tabular(UI_FONT_DISPLAY_BOLD), UI_COLOR_TEXT);
    lv_obj_align(st->hours, LV_ALIGN_CENTER, 0, -84);
    st->minutes = wf_label(root, ui_font_tabular(UI_FONT_DISPLAY_BOLD), UI_COLOR_TEXT);
    lv_obj_set_style_text_color(st->minutes, wf_face_accent(f), 0);
    lv_obj_align(st->minutes, LV_ALIGN_CENTER, 0, 62);
}

static void update(wf_face_t *f, uint32_t changed)
{
    if (!(changed & WF_DATA_TIME)) {
        return;
    }
    digital_t *st = wf_face_state(f);
    const wf_ctx_t *ctx = wf_face_ctx(f);
    char buf[24];
    wf_date_upper(ctx, "EEE d MMM", buf, sizeof buf);
    lv_label_set_text(st->date, buf);
    if (wf_face_aod(f)) {
        ui_clock_format(buf, sizeof buf);
        lv_label_set_text(st->time, buf);
        return;
    }
    if (!ctx->time_valid) {
        lv_label_set_text_static(st->hours, "--");
        lv_label_set_text_static(st->minutes, "--");
        return;
    }
    const int h = ctx->h24 ? ctx->tm.tm_hour : (ctx->tm.tm_hour % 12 == 0 ? 12 : ctx->tm.tm_hour % 12);
    snprintf(buf, sizeof buf, ctx->h24 ? "%02d" : "%d", h);
    lv_label_set_text(st->hours, buf);
    snprintf(buf, sizeof buf, "%02d", ctx->tm.tm_min);
    lv_label_set_text(st->minutes, buf);
}

const wf_face_def_t wf_face_digital = {
    .id = "digital",
    .name = "Digital Bold",
    .flags = WF_FACE_COLOR,
    .slots = SLOTS,
    .slot_count = sizeof SLOTS / sizeof SLOTS[0],
    .state_size = sizeof(digital_t),
    .create = create,
    .update = update,
};
