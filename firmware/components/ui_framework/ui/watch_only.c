// WATCH-ONLY screen (ui_root.h). Drawn by app_main before the WATCH-ONLY deep sleep
// and on every minute tick boot; the panel keeps showing it while the chip sleeps.
#include <stdio.h>

#include "ui_nav.h"
#include "ui_root.h"
#include "ui_theme.h"

#define TIME_Y    -20 // time centre above the middle, battery under it
#define BATTERY_Y 70

void ui_watch_only_show(const ui_watch_only_args_t *args)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    char buf[16] = "--:--";
    if (ui_clock_is_valid()) {
        const time_t at = args->at ? args->at : ui_clock_now();
        struct tm tm;
        localtime_r(&at, &tm);
        if (ui_clock_is_24h()) {
            snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
        } else {
            snprintf(buf, sizeof buf, "%d:%02d", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min);
        }
    }
    lv_obj_t *time = lv_label_create(scr);
    lv_label_set_text(time, buf);
    lv_obj_set_style_text_font(time, ui_font_tabular(UI_FONT_DISPLAY_LIGHT), 0);
    lv_obj_set_style_text_color(time, ui_color(UI_COLOR_TEXT), 0);
    lv_obj_align(time, LV_ALIGN_CENTER, args->dx, TIME_Y + args->dy);

    if (args->battery_pct >= 0) {
        snprintf(buf, sizeof buf, "%d%%", args->battery_pct);
        lv_obj_t *bat = lv_label_create(scr);
        lv_label_set_text(bat, buf);
        lv_obj_set_style_text_font(bat, UI_FONT_CAPTION, 0);
        lv_obj_set_style_text_color(bat, ui_color(args->battery_pct <= 10 ? UI_COLOR_DANGER : UI_COLOR_TEXT_DIM), 0);
        lv_obj_align(bat, LV_ALIGN_CENTER, args->dx, BATTERY_Y + args->dy);
    }
    lv_screen_load(scr);
}
