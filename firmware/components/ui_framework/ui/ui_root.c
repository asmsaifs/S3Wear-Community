#include "ui_root.h"

#include "hal_input.h"

static void pointer_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x = 0;
    uint16_t y = 0;
    const bool pressed = hal_touch_read(&x, &y);
    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static lv_indev_t *s_pointer;

lv_indev_t *ui_pointer_create(lv_display_t *disp)
{
    if (!disp) {
        return NULL;
    }
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, disp);
    lv_indev_set_read_cb(indev, pointer_read_cb);
    s_pointer = indev;
    return indev;
}

void ui_pointer_set_enabled(bool enabled)
{
    if (!s_pointer) {
        return;
    }
    // Event mode pauses the read timer (LV_DEF_REFR_PERIOD polling).
    lv_indev_set_mode(s_pointer, enabled ? LV_INDEV_MODE_TIMER : LV_INDEV_MODE_EVENT);
    if (enabled) {
        lv_indev_reset(s_pointer, NULL);
        lv_indev_wait_release(s_pointer);
    }
}

void ui_boot_screen_show(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "S3Wear");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_obj_center(label);
}
