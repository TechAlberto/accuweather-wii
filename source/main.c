#include <gccore.h>
#include <ogc/system.h>
#include <wiiuse/wpad.h>
#include <input_wii.h>
#include <wiiupdate.h>
#include <wiiosk.h>

#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "font.h"
#include "ui.h"
#include "weather.h"
#include "net.h"
#include "wiiosk_input_wii.h"

/* Where the release check looks.  Change these two when the project gets its
 * own GitHub repository; until then the check simply reports "up to date" or
 * fails silently, and the app carries on. */
#define APP_OWNER   "TechAlberto"
#define APP_REPO    "accuweather-wii"
#define APP_VERSION "1.0.0"

const char g_build_id[] = "BUILDID"
#ifdef UI_AUTOCYCLE
    ":AUTOCYCLE"
#endif
    ;

/* Keeps the stamp linked in even with --gc-sections. */
volatile const char *g_build_id_ref = g_build_id;

#ifdef UI_BOOTINFO
/* Latched input, drawn by the UI_BOOTINFO footer so a capture can confirm that
 * buttons really arrive (a key press is shorter than a screenshot round-trip). */
u32 g_dbg_pressed = 0;
#endif

static void set_status(const char *msg)
{
    snprintf(g_app.status, sizeof(g_app.status), "%s", msg);
    g_app.status_frames = 200;
}

/* The release checker brings the network up on its own thread.  Never run a
 * fetch while it is doing that: libogc's lwIP is not happy with two callers. */
static void wait_for_update_check(void)
{
    if (WIIUPDATE_State() == WIIUPDATE_CHECKING)
        WIIUPDATE_Wait(20000);
}

/* Fetch synchronously on the main thread.
 *
 * This used to run on an LWP worker thread, but lwIP inside libogc is not
 * re-entrant against the render thread: the app wedged a frame or two after
 * the worker started using the socket.  Blocking the main thread for the
 * ~1 s the request takes is fine because a splash frame is already on screen.
 */
static int fetch_now(int loc)
{
    weather_t tmp;
    char err[64];
    char msg[96];

    err[0] = 0;
    g_app.loading = 1;

    if (weather_fetch(&tmp, &wx_locations[loc], err, sizeof(err)) == 0) {
        g_app.wx = tmp;
        g_app.wx.is_demo = 0;
        g_app.online = 1;
        g_app.loading = 0;
        snprintf(msg, sizeof(msg), "Live AccuWeather data for %s", g_app.wx.name);
        set_status(msg);
        return 0;
    }

    g_app.online = 0;
    g_app.loading = 0;
    snprintf(msg, sizeof(msg), "%s - showing bundled sample data",
             err[0] ? err : "Weather update failed");
    set_status(msg);
    return -1;
}

static void change_location(int delta)
{
    g_app.loc_index = (g_app.loc_index + delta + wx_location_count) % wx_location_count;
    g_app.has_place = 0;
    g_app.scroll = 0;
    char msg[96];
    snprintf(msg, sizeof(msg), "Loading %s...", wx_locations[g_app.loc_index].name);
    set_status(msg);

    /* Show the "loading" state on screen before the blocking request. */
    gfx_frame_begin();
    font_frame_begin();
    ui_draw_main();
    gfx_frame_end();

    if (g_app.online) {
        wait_for_update_check();
        fetch_now(g_app.loc_index);
    }
}

/* Refresh whatever the screen is showing: a city picked in the search, or one
 * of the bundled locations. */
static int fetch_place_now(void)
{
    weather_t tmp;
    char err[64];
    char msg[96];

    err[0] = 0;
    g_app.loading = 1;

    if (weather_fetch_place(&tmp, &g_app.place, err, sizeof(err)) == 0) {
        g_app.wx = tmp;
        g_app.wx.is_demo = 0;
        g_app.online = 1;
        g_app.loading = 0;
        snprintf(msg, sizeof(msg), "Live AccuWeather data for %s", g_app.wx.name);
        set_status(msg);
        return 0;
    }

    g_app.online = 0;
    g_app.loading = 0;
    snprintf(msg, sizeof(msg), "%s - keeping the previous data",
             err[0] ? err : "Weather update failed");
    set_status(msg);
    return -1;
}

static void refresh(void)
{
    set_status("Refreshing weather data...");
    gfx_frame_begin();
    font_frame_begin();
    ui_draw_main();
    gfx_frame_end();

    if (g_app.online) {
        wait_for_update_check();
        if (g_app.has_place)
            fetch_place_now();
        else
            fetch_now(g_app.loc_index);
    } else {
        set_status("No network - showing bundled sample data");
    }
}

/* ---------------------------------------------------------------------------
 * City search, typed on the Wii system-style keyboard from libwiiosk.  The
 * keyboard is created on first use (its textures and the dictionary are
 * unpacked then, which would otherwise slow the splash down) and kept for the
 * rest of the session.
 * ------------------------------------------------------------------------ */
static osk_keyboard *g_osk;
static int g_osk_open;

static void open_search(void)
{
    if (!g_osk) {
        osk_config cfg;
        OSK_DefaultConfig(&cfg);
        cfg.title        = "Search for a city";
        cfg.initial      = g_app.wx.name;
        cfg.max_chars    = 39;
        cfg.layout       = OSK_LAYOUT_QWERTY;
        cfg.screen_width = (unsigned)gfx_w;
        cfg.screen_height = (unsigned)gfx_h;
        if (gfx_is_widescreen())
            cfg.flags |= OSK_FLAG_WIDESCREEN;
        /* Only the US English dictionary is referenced, so the linker drops
         * the other eight (~600 KB). */
        cfg.dictionary   = &osk_dict_en_us;

        g_osk = OSK_Create(&cfg);
        if (!g_osk) {
            set_status("Could not create the on-screen keyboard");
            return;
        }
    } else {
        OSK_Resume(g_osk);
    }

    g_osk_open = 1;
    g_app.ptr_active = 0;
}

static void run_search(const char *query)
{
    wx_place_t places[MAX_PLACES];
    char err[64];
    char msg[sizeof(g_app.status)];

    if (!query[0]) {
        set_status("Type a city name to search for");
        return;
    }

    snprintf(msg, sizeof(msg), "Searching for \"%s\"...", query);
    set_status(msg);

    /* Put the message on screen before the blocking request. */
    gfx_frame_begin();
    font_frame_begin();
    ui_draw_main();
    gfx_frame_end();

    if (!g_app.online) {
        set_status("No network - the city search needs a connection");
        return;
    }

    err[0] = 0;
    wait_for_update_check();
    int found = weather_search(query, places, MAX_PLACES, err, sizeof(err));
    if (found <= 0) {
        snprintf(msg, sizeof(msg), "%s", err[0] ? err : "No matching city");
        set_status(msg);
        return;
    }

    weather_t tmp;
    g_app.loading = 1;
    if (weather_fetch_place(&tmp, &places[0], err, sizeof(err)) == 0) {
        g_app.wx = tmp;
        g_app.wx.is_demo = 0;
        g_app.online = 1;
        g_app.loading = 0;
        g_app.place = places[0];
        g_app.has_place = 1;
        g_app.tab = TAB_TODAY;
        g_app.scroll = 0;
        snprintf(msg, sizeof(msg), "Showing %s, %s%s", tmp.name, tmp.country,
                 found > 1 ? " (first match)" : "");
        set_status(msg);
    } else {
        g_app.loading = 0;
        snprintf(msg, sizeof(msg), "%s", err[0] ? err : "Could not load that city");
        set_status(msg);
    }
}

int main(int argc, char **argv)
{
    if (g_build_id_ref == NULL) /* keeps the build stamp in the DOL */
        return 1;

    if (gfx_init() != 0)
        return 1;
    if (font_init() != 0)
        return 1;
    if (ui_init() != 0)
        return 1;

    /* One input layer for Wii Remote (+ Nunchuk/Classic), GameCube pad, USB
     * keyboard and mouse.  Buttons are reported as WPAD_BUTTON_* bits. */
    INPUT_Init();
    INPUT_SetScreenSize(gfx_w, gfx_h);
    INPUT_SetCursorMode(INPUT_CURSOR_IR | INPUT_CURSOR_MOUSE);

    memset(&g_app, 0, sizeof(g_app));
    g_app.loc_index = 0;
    g_app.tab = TAB_TODAY;
    g_app.unit_c = 0;
    weather_load_demo(&g_app.wx);

    if (font_atlas_full())
        set_status("Font cache full - some text may be missing");

    /* Bring the network stack up on the main thread (libogc's net_init cannot
       be called from a secondary LWP thread), then fetch while the splash is
       on screen. */
    ui_draw_splash("Starting network...");
    gfx_frame_end();

    int net_ok = (net_startup() == 0);
    g_app.online = net_ok;

    if (net_ok) {
        ui_draw_splash("Fetching the latest conditions...");
        gfx_frame_end();
        fetch_now(g_app.loc_index);

#ifdef AW_TEST_SEARCH
        /* Headless check of the city search endpoints: search for Boston,
         * adopt the first match and let the normal UI draw it.  Build with
         * `make AW_TEST_SEARCH=1` to verify the search path in an emulator. */
        {
            wx_place_t places[MAX_PLACES];
            char err[64];
            err[0] = 0;
            int found = weather_search("Boston", places, MAX_PLACES,
                                       err, sizeof(err));
            if (found > 0) {
                weather_t tmp;
                if (weather_fetch_place(&tmp, &places[0], err, sizeof(err)) == 0) {
                    g_app.wx = tmp;
                    g_app.place = places[0];
                    g_app.has_place = 1;
                    snprintf(err, sizeof(err), "selftest: %s", tmp.name);
                }
            }
            if (err[0])
                set_status(err);
        }
#endif

        /* Now that the app's own network I/O is finished, let the release
         * check use the network in the background. */
        WIIUPDATE_Start(APP_OWNER, APP_REPO, APP_VERSION);
        if (argc > 0 && argv[0])
            WIIUPDATE_SetAppPath(argv[0]);
    } else {
        g_app.loading = 0;
        set_status("No network - showing bundled sample data");
    }

    /* A couple of splash frames so the logo is not just a flash. */
    for (int i = 0; i < 12; i++) {
        INPUT_Update();
        g_app.frame++;
        gfx_frame_begin();
        font_frame_begin();
        ui_draw_splash(net_ok ? "Loading the forecast..."
                              : "No network - using bundled data");
        gfx_frame_end();
        if (INPUT_ButtonsDown() & WPAD_BUTTON_HOME)
            SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
    }

    g_app.loading = 0;

    /* The remote stops reporting IR the moment it stops moving, and a click
     * can arrive inside that gap, so the cursor is held for a moment after
     * the last live sample instead of vanishing immediately. */
    int ptr_lost = 1000;

    for (;;) {
        INPUT_Update();
        INPUT_SetScreenSize(gfx_w, gfx_h);

        if (INPUT_CursorActive()) {
            int px = 0, py = 0;
            INPUT_CursorPosition(&px, &py);
            if (px < 0) px = 0;
            if (px > gfx_w - 1) px = gfx_w - 1;
            if (py < 0) py = 0;
            if (py > gfx_h - 1) py = gfx_h - 1;
            g_app.ptr_x = px;
            g_app.ptr_y = py;
            g_app.ptr_active = 1;
            ptr_lost = 0;
        } else if (ptr_lost < 20) {
            ptr_lost++;
        } else {
            g_app.ptr_active = 0;
        }

        /* The on-screen keyboard owns input and the screen while it is up. */
        if (g_osk_open) {
            osk_input in;
            osk_status st;

            OSK_ReadInputWii(&in);
            st = OSK_Update(g_osk, &in);

            if (st == OSK_STATUS_DONE) {
                char query[64];
                OSK_CopyText(g_osk, query, sizeof(query));
                g_osk_open = 0;
                run_search(query);
            } else if (st == OSK_STATUS_CANCELLED) {
                g_osk_open = 0;
                set_status("Search cancelled");
            }

            gfx_frame_begin();
            font_frame_begin();
            ui_draw_main();
            if (g_osk_open)
                OSK_Draw(g_osk);   /* dims the screen and covers it */
            gfx_frame_end();

            if (g_app.status_frames > 0)
                g_app.status_frames--;
            g_app.frame++;
            continue;
        }

        u32 pressed = INPUT_ButtonsDown();
#ifdef UI_BOOTINFO
        g_dbg_pressed |= pressed;
#endif

        if (pressed & WPAD_BUTTON_HOME)
            SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);

        /* The update prompt owns A and B while it is up. */
        wiiupdate_popup popup;
        int popup_state = WIIUPDATE_Popup(pressed, &popup);
        if (popup_state == WIIUPDATE_POPUP_EXIT)
            SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);

        if (popup_state == WIIUPDATE_POPUP_CLOSED) {
            int tab_click = -1;
            int search_click = 0;

            if ((pressed & WPAD_BUTTON_A) && g_app.ptr_active) {
                float sx, sy, sw, sh;
                ui_search_bounds(&sx, &sy, &sw, &sh);
                if ((float)g_app.ptr_x >= sx && (float)g_app.ptr_x < sx + sw &&
                    (float)g_app.ptr_y >= sy && (float)g_app.ptr_y <= sy + sh) {
                    search_click = 1;
                } else {
                    for (int i = 0; i < TAB_COUNT; i++) {
                        float tx, ty, tw, th;
                        ui_tab_bounds(i, &tx, &ty, &tw, &th);
                        if ((float)g_app.ptr_x >= tx && (float)g_app.ptr_x < tx + tw &&
                            (float)g_app.ptr_y >= ty && (float)g_app.ptr_y <= ty + th) {
                            tab_click = i;
                            break;
                        }
                    }
                }
            }

            if (search_click || (pressed & WPAD_BUTTON_2)) {
                open_search();
            } else if (tab_click >= 0) {
                g_app.tab = tab_click;
                g_app.scroll = 0;
            } else {
                if (pressed & (WPAD_BUTTON_LEFT | PAD_BUTTON_LEFT | PAD_TRIGGER_L)) {
                    g_app.tab = (g_app.tab + TAB_COUNT - 1) % TAB_COUNT;
                    g_app.scroll = 0;
                }
                if (pressed & (WPAD_BUTTON_RIGHT | PAD_BUTTON_RIGHT | PAD_TRIGGER_R)) {
                    g_app.tab = (g_app.tab + 1) % TAB_COUNT;
                    g_app.scroll = 0;
                }
                if (pressed & (WPAD_BUTTON_UP | PAD_BUTTON_UP)) {
                    if (g_app.scroll > 0)
                        g_app.scroll--;
                }
                if (pressed & (WPAD_BUTTON_DOWN | PAD_BUTTON_DOWN)) {
                    int max = g_app.wx.hour_count > 8 ? g_app.wx.hour_count - 8 : 0;
                    if (g_app.scroll < max)
                        g_app.scroll++;
                }
                if (pressed & (WPAD_BUTTON_A | PAD_BUTTON_A)) {
                    if (!g_app.loading)
                        refresh();
                }
                if (pressed & (WPAD_BUTTON_B | WPAD_BUTTON_1 | PAD_BUTTON_B)) {
                    g_app.unit_c = !g_app.unit_c;
                    set_status(g_app.unit_c ? "Units: Celsius" : "Units: Fahrenheit");
                }
                if (pressed & (WPAD_BUTTON_PLUS | PAD_BUTTON_Y))
                    change_location(1);
                if (pressed & (WPAD_BUTTON_MINUS | PAD_BUTTON_X))
                    change_location(-1);
            }
        }

        if (g_app.status_frames > 0)
            g_app.status_frames--;

#ifdef UI_AUTOCYCLE
        if (g_app.frame > 0 && (g_app.frame % 300) == 0) {
            g_app.tab = (g_app.tab + 1) % TAB_COUNT;
            g_app.scroll = 0;
        }
#endif

        gfx_frame_begin();
        font_frame_begin();
        if (popup_state != WIIUPDATE_POPUP_CLOSED) {
            ui_draw_main();
            ui_draw_update_popup(&popup);
        } else {
            ui_draw_main();
        }
        gfx_frame_end();

        g_app.frame++;
    }

    return 0;
}
