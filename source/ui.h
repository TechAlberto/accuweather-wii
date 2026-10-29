#ifndef UI_H
#define UI_H

#include "weather.h"
#include <wiiupdate.h>

typedef struct {
    weather_t wx;
    int loc_index;
    int tab;
    int scroll;
    int unit_c;
    int loading;
    int online;
    int frame;

    /* Set when the forecast came from a searched city instead of the bundled
     * wx_locations[] list, so refresh keeps showing that city. */
    wx_place_t place;
    int has_place;

    /* Wii Remote IR / USB mouse cursor in logical screen coordinates. */
    int ptr_x, ptr_y;
    int ptr_active;

    char status[96];
    int status_frames;
} app_state_t;

enum { TAB_TODAY = 0, TAB_HOURLY, TAB_DAILY, TAB_ALERTS, TAB_COUNT };

extern app_state_t g_app;

/* One-time setup: uploads the wordmark texture and rasterises every glyph the
 * UI uses, before the first frame uploads the font atlas. */
int  ui_init(void);

void ui_draw_splash(const char *status);
void ui_draw_main(void);

/* Draws the libwiiupdate prompt; call while WIIUPDATE_Popup() says OPEN. */
void ui_draw_update_popup(const wiiupdate_popup *popup);

/* Tab strip geometry, shared by drawing and pointer hit-testing. */
void ui_tab_bounds(int index, float *x, float *y, float *w, float *h);

/* City search chip in the header, shared by drawing and hit-testing. */
void ui_search_bounds(float *x, float *y, float *w, float *h);

#endif
