// ui_apps internals: the shell screens and helpers they share.
#pragma once

#include "shell.h"

extern const screen_def_t shell_qs_screen;
extern const screen_def_t shell_notif_screen;
extern const screen_def_t shell_tiles_screen;
extern const screen_def_t shell_launcher_screen;

/** The Media app's phone-drawn title + artist (media_apps.c): a hidden white A8 image with its own
 *  descriptor (*dsc, freed with the image; NULL if out of memory). */
lv_obj_t *media_apps_text_create(lv_obj_t *parent, lv_image_dsc_t **dsc);

/** Show the session's phone-drawn title in img, or hide it; true while shown. */
bool media_apps_text_show(lv_obj_t *img, lv_image_dsc_t *dsc);

/** The Home tile's body (ha_apps.c): up to four entity buttons around y = cy in a tile page. */
void ha_apps_tile_create(lv_obj_t *page, int32_t cy);

/** The Home tile came into view: read the states again (at most every 30 s unless the last call failed). */
void ha_apps_tile_shown(void);

/** A swipe in dir on root (or a child that lets gestures bubble) closes the screen
 *  (ui_nav_back). The release after it never counts as a click. */
void shell_close_on_swipe(lv_obj_t *root, lv_dir_t dir);

/** Round app icon, d px across: the app's image, or its symbol in white on its colour. Dimmed while
 *  pressed (when made clickable). */
lv_obj_t *shell_app_icon_create(lv_obj_t *parent, const shell_app_t *app, int32_t d);

/** Recently opened apps, newest first (index < SHELL_RECENT_MAX); NULL past the end. */
const shell_app_t *shell_recent_at(size_t index);

/** Plain label (font and colour, nothing else). */
lv_obj_t *shell_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color_hex);
