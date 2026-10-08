/* LVGL configuration for the desktop simulator.
 * Only overrides are listed; everything else takes LVGL's defaults from
 * lv_conf_internal.h. Keep colour depth equal to the target (RGB565). */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

/* Host has plenty of RAM: use libc instead of LVGL's fixed pool. */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE

#define LV_USE_LOG      1
#define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF   1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

/* SDL window, mouse = touch. */
#define LV_USE_SDL             1
#define LV_SDL_INCLUDE_PATH    <SDL2/SDL.h>
#define LV_SDL_RENDER_MODE     LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_SDL_BUF_COUNT       1
#define LV_SDL_ACCELERATED     1
#define LV_SDL_FULLSCREEN      0
#define LV_SDL_DIRECT_EXIT     1

/* QR codes (s3w_qr), same as CONFIG_LV_USE_QRCODE on the watch. */
#define LV_USE_QRCODE 1

/* Same refresh period as the watch (CONFIG_LV_DEF_REFR_PERIOD). */
#define LV_DEF_REFR_PERIOD 16

/* lodepng: PNG encoder used by --screenshot. */
#define LV_USE_LODEPNG 1

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS    0

#endif /* LV_CONF_H */
