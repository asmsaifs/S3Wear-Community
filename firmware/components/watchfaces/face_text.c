// Modular (time, date, three gauges and a wide slot) and Minimal (light digits and
// the date, one optional slot). Both AOD variants: light dim time, dim date.
#include "ui_theme.h"
#include "wf_priv.h"

typedef struct {
    lv_obj_t *time;
    lv_obj_t *date;
} text_face_t;

typedef struct {
    const lv_font_t *time_font;
    lv_align_t align;
    int32_t time_y;
    int32_t date_y; // offset below the time label
    const char *date_pattern;
    bool date_accent;
} layout_t;

static void build(wf_face_t *f, lv_obj_t *root, const layout_t *l)
{
    text_face_t *st = wf_face_state(f);
    const bool aod = wf_face_aod(f);
    st->time = wf_label(root, ui_font_tabular(aod ? UI_FONT_DISPLAY_LIGHT : l->time_font),
                        aod ? UI_COLOR_TEXT_DIM : UI_COLOR_TEXT);
    lv_obj_align(st->time, l->align, 0, l->time_y);
    st->date = wf_label(root, aod ? UI_FONT_CAPTION : UI_FONT_BODY, UI_COLOR_TEXT_DIM);
    if (!aod && l->date_accent) {
        lv_obj_set_style_text_color(st->date, wf_face_accent(f), 0);
    }
    // Fixed offset (align_to is one-shot and the time text is set later); for
    // CENTER the time label's centre is time_y, so go half a line down.
    const int32_t line = lv_font_get_line_height(lv_obj_get_style_text_font(st->time, 0));
    const int32_t date_h = lv_font_get_line_height(lv_obj_get_style_text_font(st->date, 0));
    const int32_t below = l->align == LV_ALIGN_CENTER ? line / 2 + date_h / 2 : line;
    lv_obj_align(st->date, l->align, 0, l->time_y + below + l->date_y);
}

static void refresh(wf_face_t *f, uint32_t changed, const char *date_pattern)
{
    if (!(changed & WF_DATA_TIME)) {
        return;
    }
    text_face_t *st = wf_face_state(f);
    char buf[32];
    ui_clock_format(buf, sizeof buf);
    lv_label_set_text(st->time, buf);
    wf_date_upper(wf_face_ctx(f), date_pattern, buf, sizeof buf);
    lv_label_set_text(st->date, buf);
}

// --- Modular ---------------------------------------------------------------------------

static const wf_slot_def_t MODULAR_SLOTS[] = {
    {.id = "left", .x = 75, .y = 288, .style = WF_SLOT_CIRCLE, .def = WF_COMP_STEPS},
    {.id = "center", .x = WF_CENTER_X, .y = 288, .style = WF_SLOT_CIRCLE, .def = WF_COMP_BATTERY},
    {.id = "right", .x = 335, .y = 288, .style = WF_SLOT_CIRCLE, .def = WF_COMP_WEATHER},
    {.id = "bottom", .x = WF_CENTER_X, .y = 422, .style = WF_SLOT_LINE, .def = WF_COMP_NEXT_EVENT},
};

static const layout_t MODULAR = {
    .time_font = UI_FONT_DISPLAY,
    .align = LV_ALIGN_TOP_MID,
    .time_y = 40,
    .date_y = -8,
    .date_pattern = "EEE d MMM",
    .date_accent = true,
};

static void modular_create(wf_face_t *f, lv_obj_t *root)
{
    build(f, root, &MODULAR);
}

static void modular_update(wf_face_t *f, uint32_t changed)
{
    refresh(f, changed, MODULAR.date_pattern);
}

const wf_face_def_t wf_face_modular = {
    .id = "modular",
    .name = "Modular",
    .flags = WF_FACE_COLOR,
    .slots = MODULAR_SLOTS,
    .slot_count = sizeof MODULAR_SLOTS / sizeof MODULAR_SLOTS[0],
    .state_size = sizeof(text_face_t),
    .create = modular_create,
    .update = modular_update,
};

// --- Minimal ---------------------------------------------------------------------------

static const wf_slot_def_t MINIMAL_SLOTS[] = {
    {.id = "bottom", .x = WF_CENTER_X, .y = 430, .style = WF_SLOT_LINE, .def = WF_COMP_NONE},
};

static const layout_t MINIMAL = {
    .time_font = UI_FONT_DISPLAY_LIGHT,
    .align = LV_ALIGN_CENTER,
    .time_y = -30,
    .date_y = 6,
    .date_pattern = "EEEE d MMMM",
    .date_accent = true,
};

static void minimal_create(wf_face_t *f, lv_obj_t *root)
{
    build(f, root, &MINIMAL);
}

static void minimal_update(wf_face_t *f, uint32_t changed)
{
    refresh(f, changed, MINIMAL.date_pattern);
}

const wf_face_def_t wf_face_minimal = {
    .id = "minimal",
    .name = "Minimal",
    .flags = WF_FACE_COLOR,
    .slots = MINIMAL_SLOTS,
    .slot_count = sizeof MINIMAL_SLOTS / sizeof MINIMAL_SLOTS[0],
    .state_size = sizeof(text_face_t),
    .create = minimal_create,
    .update = minimal_update,
};
