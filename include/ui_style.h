#ifndef APP_UI_STYLE_H_
#define APP_UI_STYLE_H_

#include <lvgl.h>

#define UI_SCREEN_W 240
#define UI_SCREEN_H 320
#define UI_CONTENT_H 240
#define UI_KEYBAR_Y 240
#define UI_KEYBAR_H 80
#define UI_TILE_W 120
#define UI_TILE_H 120
#define UI_KEY_W 60

#if defined(LV_FONT_MONTSERRAT_48) && LV_FONT_MONTSERRAT_48
#define UI_FONT_48 (&lv_font_montserrat_48)
#else
#define UI_FONT_48 LV_FONT_DEFAULT
#endif

#if defined(LV_FONT_MONTSERRAT_40) && LV_FONT_MONTSERRAT_40
#define UI_FONT_40 (&lv_font_montserrat_40)
#else
#define UI_FONT_40 UI_FONT_48
#endif

#if defined(LV_FONT_MONTSERRAT_36) && LV_FONT_MONTSERRAT_36
#define UI_FONT_36 (&lv_font_montserrat_36)
#else
#define UI_FONT_36 UI_FONT_40
#endif

#if defined(LV_FONT_MONTSERRAT_32) && LV_FONT_MONTSERRAT_32
#define UI_FONT_32 (&lv_font_montserrat_32)
#else
#define UI_FONT_32 UI_FONT_36
#endif

#if defined(LV_FONT_MONTSERRAT_30) && LV_FONT_MONTSERRAT_30
#define UI_FONT_30 (&lv_font_montserrat_30)
#else
#define UI_FONT_30 UI_FONT_32
#endif

#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
#define UI_FONT_24 (&lv_font_montserrat_24)
#else
#define UI_FONT_24 UI_FONT_30
#endif

#if defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
#define UI_FONT_20 (&lv_font_montserrat_20)
#else
#define UI_FONT_20 LV_FONT_DEFAULT
#endif

#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
#define UI_FONT_16 (&lv_font_montserrat_16)
#else
#define UI_FONT_16 LV_FONT_DEFAULT
#endif

#endif
