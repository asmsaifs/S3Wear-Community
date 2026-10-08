// Analog Classic: minute track, 12 and 6, hour/minute/second hands, subdials at 9
// and 3 o'clock. AOD: 12 hour dots and thin dim hour/minute hands, no seconds.
#include "ui_theme.h"
#include "wf_priv.h"

#define DIAL_D     384 // tick ring outer diameter
#define NUMERAL_R  140
#define HOUR_LEN   92
#define MINUTE_LEN 150
#define SECOND_LEN 168
#define SECOND_TAIL 30
#define AOD_DOT_R  176

typedef struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *second; // NULL in AOD
    lv_obj_t *cap;
} analog_t;

static const wf_slot_def_t SLOTS[] = {
    {.id = "left", .x = 110, .y = WF_CENTER_Y, .style = WF_SLOT_CIRCLE, .def = WF_COMP_DATE},
    {.id = "right", .x = 300, .y = WF_CENTER_Y, .style = WF_SLOT_CIRCLE, .def = WF_COMP_STEPS},
};

static lv_obj_t *dial_create(lv_obj_t *root)
{
    lv_obj_t *scale = lv_scale_create(root);
    lv_obj_remove_style_all(scale);
    lv_obj_remove_flag(scale, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(scale, DIAL_D, DIAL_D);
    lv_obj_center(scale);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_total_tick_count(scale, 60);
    lv_scale_set_major_tick_every(scale, 5);
    lv_scale_set_label_show(scale, false);
    lv_scale_set_range(scale, 0, 59);
    lv_scale_set_angle_range(scale, 354); // 60 ticks, 6° apart, none doubled at 12
    lv_scale_set_rotation(scale, 270);
    lv_obj_set_style_length(scale, 18, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 5, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(scale, ui_color(UI_COLOR_TEXT), LV_PART_INDICATOR);
    lv_obj_set_style_length(scale, 8, LV_PART_ITEMS);
    lv_obj_set_style_line_width(scale, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale, ui_color(UI_COLOR_TEXT_DIM), LV_PART_ITEMS);
    lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);
    return scale;
}

static void create(wf_face_t *f, lv_obj_t *root)
{
    analog_t *st = wf_face_state(f);
    const bool aod = wf_face_aod(f);
    if (aod) {
        for (int h = 0; h < 12; h++) {
            const int32_t s = lv_trigo_sin((int16_t)(h * 30));
            const int32_t c = lv_trigo_sin((int16_t)(h * 30 + 90));
            wf_dot_create(root, WF_CENTER_X + (AOD_DOT_R * s >> LV_TRIGO_SHIFT),
                          WF_CENTER_Y - (AOD_DOT_R * c >> LV_TRIGO_SHIFT), h % 3 == 0 ? 8 : 4, UI_COLOR_TEXT_DIM);
        }
        st->hour = wf_hand_create(root, 6, UI_COLOR_TEXT_DIM);
        st->minute = wf_hand_create(root, 4, UI_COLOR_TEXT_DIM);
        st->cap = wf_dot_create(root, WF_CENTER_X, WF_CENTER_Y, 10, UI_COLOR_TEXT_DIM);
        return;
    }
    dial_create(root);
    lv_obj_t *twelve = wf_label(root, UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_label_set_text_static(twelve, "12");
    lv_obj_align(twelve, LV_ALIGN_CENTER, 0, -NUMERAL_R);
    lv_obj_t *six = wf_label(root, UI_FONT_TITLE, UI_COLOR_TEXT);
    lv_label_set_text_static(six, "6");
    lv_obj_align(six, LV_ALIGN_CENTER, 0, NUMERAL_R);

    st->hour = wf_hand_create(root, 12, UI_COLOR_TEXT);
    st->minute = wf_hand_create(root, 8, UI_COLOR_TEXT);
    // Second hand and centre dot: the chosen colour, red by default.
    const lv_color_t second = wf_face_accent_or(f, ui_color(UI_COLOR_DANGER));
    st->second = wf_hand_create(root, 3, UI_COLOR_DANGER);
    lv_obj_set_style_line_color(st->second, second, 0);
    st->cap = wf_dot_create(root, WF_CENTER_X, WF_CENTER_Y, 18, UI_COLOR_TEXT);
    lv_obj_set_style_bg_color(wf_dot_create(st->cap, 9, 9, 8, UI_COLOR_DANGER), second, 0);
}

static void update(wf_face_t *f, uint32_t changed)
{
    if (!(changed & (WF_DATA_TIME | WF_DATA_SECOND))) {
        return;
    }
    analog_t *st = wf_face_state(f);
    const wf_ctx_t *ctx = wf_face_ctx(f);
    lv_obj_t *hands[] = {st->hour, st->minute, st->second, st->cap};
    for (size_t i = 0; i < sizeof hands / sizeof hands[0]; i++) {
        if (hands[i]) {
            if (ctx->time_valid) {
                lv_obj_remove_flag(hands[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(hands[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (!ctx->time_valid) {
        return;
    }
    const struct tm *tm = &ctx->tm;
    const int32_t sec = tm->tm_sec;
    // AOD updates once a minute: keep the minute hand on the minute.
    const int32_t minute_x10 = tm->tm_min * 60 + (wf_face_aod(f) ? 0 : sec);
    const int32_t hour_x10 = ((tm->tm_hour % 12) * 3600 + tm->tm_min * 60) / 12;
    wf_hand_set(st->hour, WF_CENTER_X, WF_CENTER_Y, hour_x10, HOUR_LEN, 0);
    wf_hand_set(st->minute, WF_CENTER_X, WF_CENTER_Y, minute_x10, MINUTE_LEN, 0);
    if (st->second) {
        wf_hand_set(st->second, WF_CENTER_X, WF_CENTER_Y, sec * 60, SECOND_LEN, SECOND_TAIL);
    }
}

const wf_face_def_t wf_face_analog = {
    .id = "analog",
    .name = "Analog Classic",
    .flags = WF_FACE_SECONDS | WF_FACE_COLOR,
    .slots = SLOTS,
    .slot_count = sizeof SLOTS / sizeof SLOTS[0],
    .state_size = sizeof(analog_t),
    .create = create,
    .update = update,
};
