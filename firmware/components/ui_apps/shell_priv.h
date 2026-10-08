// ui_apps internals: the shell screens and helpers they share.
#pragma once

#include "shell.h"

extern const screen_def_t shell_qs_screen;
extern const screen_def_t shell_notif_screen;
extern const screen_def_t shell_tiles_screen;
extern const screen_def_t shell_launcher_screen;

/** A swipe in dir on root (or a child that lets gestures bubble) closes the screen
 *  (ui_nav_back). The release after it never counts as a click. */
void shell_close_on_swipe(lv_obj_t *root, lv_dir_t dir);

/** Round app icon: the symbol in white on the app's colour, d px across. */
lv_obj_t *shell_app_icon_create(lv_obj_t *parent, const shell_app_t *app, int32_t d);

/** Recently opened apps, newest first (index < SHELL_RECENT_MAX); NULL past the end. */
const shell_app_t *shell_recent_at(size_t index);

/** Plain label (font and colour, nothing else). */
lv_obj_t *shell_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color_hex);
