// Notifications (docs/03 F7): swipe up on the face. Placeholder until the phone
// link delivers notifications (P6): an empty state. A swipe down, BACK or PWR
// closes it.
#include "shell_priv.h"
#include "ui_widgets.h"

static void notif_create(ui_screen_t *s, lv_obj_t *root, const void *args)
{
    (void)s;
    (void)args;
    shell_close_on_swipe(root, LV_DIR_BOTTOM);
    // Short enough not to scroll: a list that scrolls would take the swipe down.
    lv_obj_t *list = s3w_list_create(root);
    s3w_header_create(list, "Notifications");
    s3w_empty_state_create(list, LV_SYMBOL_BELL, "No notifications",
                           "Notifications from your phone appear here.");
}

const screen_def_t shell_notif_screen = {
    .id = "notifications",
    .on_create = notif_create,
    .flags = UI_SCREEN_NO_SWIPE_BACK, // closed by a swipe down
};
