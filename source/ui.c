#include "ui.h"
#include "gfx.h"
#include "font.h"
#include "logo_assets.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#ifdef UI_BOOTINFO
#include <wiiuse/wpad.h>
#endif

app_state_t g_app;

#define C_TEXT       RGB(255, 255, 255)
#define C_DIM        RGB(0x9f, 0xc0, 0xda)
#define C_FAINT      RGBA(0x9f, 0xc0, 0xda, 140)
#define C_ACCENT     RGB(0xff, 0xc2, 0x4b)
#define C_ACCENT_SOFT RGBA(0xff, 0xc2, 0x4b, 60)
#define C_PANEL      RGBA(255, 255, 255, 16)
#define C_PANEL_HI   RGBA(255, 255, 255, 30)
#define C_LINE       RGBA(255, 255, 255, 40)
#define C_RAIN       RGB(0x6f, 0xc3, 0xff)
#define C_SNOW       RGB(0xdc, 0xeb, 0xff)
#define C_GOOD       RGB(0x58, 0xd6, 0x8d)
#define C_WARN       RGB(0xff, 0x8a, 0x5c)

/* FontAwesome 4.x codepoints bundled with the APK */
#define FA_SUN      0xf185
#define FA_MOON     0xf186
#define FA_CLOUD    0xf0c2
#define FA_BOLT     0xf0e7
#define FA_DROP     0xf043
#define FA_ASTERISK 0xf069
#define FA_UMBRELLA 0xf0e9
#define FA_WARNING  0xf071
#define FA_INFO     0xf05a
#define FA_CLOCK    0xf017
#define FA_REFRESH  0xf021
#define FA_GLOBE    0xf0ac
#define FA_EYE      0xf06e
#define FA_GAUGE    0xf0e4

static const char *weekdays[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
static const char *tab_names[] = { "TODAY", "HOURLY", "DAILY", "ALERTS" };

/* The wordmark is the real lockup from the APK (res/drawable/
 * widget_branding_dark_med.png), split into a white and an orange mask. */
static void draw_logo(float x, float y, float w)
{
    float h = w * (float)LOGO_H / (float)LOGO_W;
    u32 accent = RGB(LOGO_ACCENT_R, LOGO_ACCENT_G, LOGO_ACCENT_B);
    gfx_sprite_quad(x, y, w, h, 0.0f, 0.0f, 1.0f, 0.5f, C_TEXT);
    gfx_sprite_quad(x, y, w, h, 0.0f, 0.5f, 1.0f, 1.0f, accent);
}

int ui_init(void)
{
    gfx_sprite_init((u8 *)logo_bin, LOGO_W, LOGO_H * 2);
    font_prewarm();
    return 0;
}

static float t_val(float c)
{
    return g_app.unit_c ? c : c * 9.0f / 5.0f + 32.0f;
}

static int t_round(float c)
{
    return (int)lroundf(t_val(c));
}

static const char *temp_unit(void)
{
    return g_app.unit_c ? "C" : "F";
}

static float s_val(float kmh)
{
    return g_app.unit_c ? kmh : kmh * 0.621371f;
}

static const char *speed_unit(void)
{
    return g_app.unit_c ? "km/h" : "mph";
}

static const char *uv_text(int uv)
{
    if (uv <= 2) return "Low";
    if (uv <= 5) return "Moderate";
    if (uv <= 7) return "High";
    if (uv <= 10) return "Very High";
    return "Extreme";
}

static void label(float x, float y, const char *text, u32 color)
{
    font_draw_sp(FONT_BOLD, 10, x, y, color, text, 1.1f);
}

static void key_hint(float x, float y, const char *key, const char *text)
{
    float kw = font_width(FONT_BOLD, 10, key) + 8.0f;
    gfx_rect(x, y - 3.0f, kw, 16.0f, RGBA(255, 255, 255, 26));
    font_draw(FONT_BOLD, 10, x + 4.0f, y, C_TEXT, key);
    font_draw(FONT_REGULAR, 10, x + kw + 5.0f, y, C_FAINT, text);
}

static void draw_spinner(float cx, float cy, float r, u32 color, int frame)
{
    for (int i = 0; i < 8; i++) {
        float a = (float)frame * 0.16f + (float)i * 0.785398f;
        int alpha = 40 + (i * 215) / 8;
        u32 c = (color & 0xffffff00u) | (u32)alpha;
        float x = cx + cosf(a) * r;
        float y = cy + sinf(a) * r;
        gfx_rect(x - 2.0f, y - 2.0f, 4.0f, 4.0f, c);
    }
}

static void draw_cloud_shape(float cx, float cy, float s, u32 color)
{
    font_draw_glyph_center(FONT_ICONS, (int)s, cx, cy, color, FA_CLOUD);
}

static void draw_wx_icon(float cx, float cy, float s, int code, int is_day)
{
    int kind = wx_icon_kind(code);
    u32 sun = is_day ? C_ACCENT : RGB(0xd9, 0xe6, 0xf2);
    u32 cloud = RGB(0xe9, 0xf2, 0xfa);

    switch (kind) {
    case WI_SUN:
        font_draw_glyph_center(FONT_ICONS, (int)(s * 0.92f), cx, cy, sun,
                               is_day ? FA_SUN : FA_MOON);
        break;

    case WI_PARTLY:
        font_draw_glyph_center(FONT_ICONS, (int)(s * 0.62f), cx - s * 0.17f, cy - s * 0.16f,
                               sun, is_day ? FA_SUN : FA_MOON);
        draw_cloud_shape(cx + s * 0.13f, cy + s * 0.13f, s * 0.74f, cloud);
        break;

    case WI_CLOUD:
        draw_cloud_shape(cx, cy, s * 0.98f, cloud);
        break;

    case WI_FOG:
        draw_cloud_shape(cx, cy - s * 0.10f, s * 0.82f, cloud);
        for (int i = 0; i < 3; i++) {
            float w = s * (i == 1 ? 0.38f : 0.54f);
            gfx_rect(cx - w * 0.5f, cy + s * (0.18f + i * 0.11f), w, 2.0f,
                     RGBA(0xe9, 0xf2, 0xfa, 160));
        }
        break;

    case WI_DRIZZLE:
    case WI_RAIN:
    case WI_SLEET:
    case WI_SNOW:
        draw_cloud_shape(cx, cy - s * 0.14f, s * 0.86f, cloud);
        if (kind == WI_SNOW || kind == WI_SLEET) {
            for (int i = 0; i < 3; i++)
                font_draw_glyph_center(FONT_ICONS, (int)(s * 0.18f),
                                       cx + s * (i - 1) * 0.24f, cy + s * 0.26f,
                                       C_SNOW, FA_ASTERISK);
        } else {
            for (int i = 0; i < 3; i++) {
                if (kind == WI_DRIZZLE && i == 2)
                    break;
                font_draw_glyph_center(FONT_ICONS, (int)(s * 0.18f),
                                       cx + s * (i - 1) * 0.24f, cy + s * 0.27f,
                                       C_RAIN, FA_DROP);
            }
        }
        break;

    case WI_STORM:
        draw_cloud_shape(cx, cy - s * 0.12f, s * 0.88f, cloud);
        font_draw_glyph_center(FONT_ICONS, (int)(s * 0.40f), cx, cy + s * 0.28f,
                               C_ACCENT, FA_BOLT);
        break;

    case WI_WIND:
    default:
        for (int i = 0; i < 3; i++) {
            float w = s * (i == 1 ? 0.42f : 0.60f);
            gfx_rect(cx - w * 0.5f, cy - s * 0.18f + i * s * 0.18f, w, 2.5f,
                     RGBA(0xe9, 0xf2, 0xfa, 200));
        }
        break;
    }
}

static void draw_clipped(int fnt, int px, float x, float y, float maxw, u32 color, const char *text)
{
    if (font_width(fnt, px, text) <= maxw) {
        font_draw(fnt, px, x, y, color, text);
        return;
    }
    char buf[160];
    int n = 0;
    while (text[n] && n < (int)sizeof(buf) - 4) {
        buf[n] = text[n];
        buf[n + 1] = 0;
        /* don't split a UTF-8 sequence */
        if (((unsigned char)text[n] & 0xc0) != 0x80) {
            char probe[164];
            snprintf(probe, sizeof(probe), "%s\u2026", buf);
            if (font_width(fnt, px, probe) > maxw)
                break;
        }
        n++;
    }
    buf[n] = 0;
    snprintf(buf + n, sizeof(buf) - (size_t)n, "\u2026");
    font_draw(fnt, px, x, y, color, buf);
}

static void draw_search_chip(void)
{
    float x, y, w, h;
    int hover;

    ui_search_bounds(&x, &y, &w, &h);
    hover = g_app.ptr_active &&
            (float)g_app.ptr_x >= x && (float)g_app.ptr_x < x + w &&
            (float)g_app.ptr_y >= y && (float)g_app.ptr_y <= y + h;

    gfx_rect(x, y, w, h, hover ? C_PANEL_HI : C_PANEL);
    gfx_rect_outline(x, y, w, h, 1.0f, hover ? C_ACCENT : C_LINE);

    /* magnifier: lens ring plus handle, drawn from thin rects so it needs no
     * extra glyph or texture */
    float lx = x + 15.0f, ly = y + 12.0f;
    gfx_rect_outline(lx - 4.0f, ly - 4.0f, 8.0f, 8.0f, 1.0f,
                     hover ? C_ACCENT : C_DIM);
    gfx_rect(lx + 3.5f, ly + 3.5f, 2.0f, 2.0f, hover ? C_ACCENT : C_DIM);
    gfx_rect(lx + 5.0f, ly + 5.0f, 4.0f, 2.0f, hover ? C_ACCENT : C_DIM);

    font_draw_sp(FONT_BOLD, 10, x + 30.0f, y + 7.0f,
                 hover ? C_TEXT : C_DIM, "SEARCH", 1.2f);
}

void ui_search_bounds(float *x, float *y, float *w, float *h)
{
    float rx = (float)gfx_w - 26.0f;
    if (x) *x = rx - 104.0f;
    if (y) *y = 41.0f;
    if (w) *w = 104.0f;
    if (h) *h = 24.0f;
}

static void draw_header(void)
{
    float m = 26.0f;
    float rx = (float)gfx_w - m;

    /* brand: the APK wordmark, drawn from the two-mask IA8 texture */
    draw_logo(m, 9.0f, 184.0f);

    /* badges, right aligned: [ DEMO / LIVE ]  [ °F ] */
    char unit[8];
    snprintf(unit, sizeof(unit), "\u00b0%s", temp_unit());
    float unit_w = font_width(FONT_BOLD, 11, unit) + 16.0f;
    float unit_x = rx - unit_w;
    gfx_rect(unit_x, 16.0f, unit_w, 20.0f, C_PANEL_HI);
    font_draw(FONT_BOLD, 11, unit_x + 8.0f, 20.0f, C_TEXT, unit);

    const char *mode = g_app.online ? "LIVE" : "DEMO";
    u32 mode_col = g_app.online ? C_GOOD : C_ACCENT;
    float mode_w = font_width(FONT_BOLD, 10, mode) + 22.0f;
    float mode_x = unit_x - mode_w - 8.0f;
    gfx_rect(mode_x, 16.0f, mode_w, 20.0f, RGBA(255, 255, 255, 18));
    gfx_rect(mode_x + 8.0f, 24.0f, 5.0f, 5.0f, mode_col);
    font_draw_sp(FONT_BOLD, 10, mode_x + 17.0f, 20.0f, mode_col, mode, 1.0f);

    /* location */
    font_draw(FONT_BOLD, 26, m, 42.0f, C_TEXT, g_app.wx.name);
    char sub[96];
    snprintf(sub, sizeof(sub), "%s, %s", g_app.wx.area, g_app.wx.country);
    font_draw(FONT_REGULAR, 11, m + 1.0f, 74.0f, C_DIM, sub);

    char upd[64], tm[32];
    wx_format_time(&g_app.wx.updated, tm, sizeof(tm));
    snprintf(upd, sizeof(upd), "Updated %s", tm);
    font_draw(FONT_REGULAR, 11, rx - font_width(FONT_REGULAR, 11, upd), 74.0f, C_FAINT, upd);

    draw_search_chip();
}

static void draw_hero(void)
{
    float m = 26.0f;
    float rx = (float)gfx_w - m;
    weather_t *w = &g_app.wx;

    char temp[16];
    snprintf(temp, sizeof(temp), "%d\u00b0", t_round(w->temp_c));
    font_draw(FONT_LIGHT, 92, m, 84.0f, C_TEXT, temp);

    float tx = m + font_width(FONT_LIGHT, 92, temp) + 16.0f;
    float col_right = rx - 128.0f;
    if (tx > col_right - 150.0f)
        tx = col_right - 150.0f;

    font_draw(FONT_MEDIUM, 23, tx, 96.0f, C_TEXT, w->condition);

    char feels[64];
    snprintf(feels, sizeof(feels), "Feels like %d\u00b0", t_round(w->feels_c));
    font_draw(FONT_REGULAR, 13, tx, 130.0f, C_DIM, feels);

    char hilo[64];
    snprintf(hilo, sizeof(hilo), "H %d\u00b0    L %d\u00b0", t_round(w->hi_c), t_round(w->lo_c));
    font_draw(FONT_BOLD, 13, tx, 150.0f, C_TEXT, hilo);

    if (w->day_count > 0) {
        char rise[24], set[24], s[72];
        wx_format_time(&w->days[0].sunrise, rise, sizeof(rise));
        wx_format_time(&w->days[0].sunset, set, sizeof(set));
        snprintf(s, sizeof(s), "Sunrise %s   Sunset %s", rise, set);
        font_draw(FONT_REGULAR, 10, tx, 172.0f, C_FAINT, s);
    }

    draw_wx_icon(rx - 62.0f, 138.0f, 112.0f, w->code, w->is_day);

    gfx_hline(m, 200.0f, (float)gfx_w - 2.0f * m, C_LINE);
}

void ui_tab_bounds(int index, float *x, float *y, float *w, float *h)
{
    float m = 26.0f;
    float width = ((float)gfx_w - 2.0f * m) / (float)TAB_COUNT;
    if (x) *x = m + width * (float)index;
    if (y) *y = 206.0f;
    if (w) *w = width;
    if (h) *h = 32.0f;
}

static int tab_at(float px, float py)
{
    if (py < 206.0f || py > 238.0f)
        return -1;
    for (int i = 0; i < TAB_COUNT; i++) {
        float x, y, w, h;
        ui_tab_bounds(i, &x, &y, &w, &h);
        if (px >= x && px < x + w)
            return i;
    }
    return -1;
}

static void draw_tabs(void)
{
    float m = 26.0f;
    float width = ((float)gfx_w - 2.0f * m) / (float)TAB_COUNT;

    int hover = g_app.ptr_active ? tab_at((float)g_app.ptr_x, (float)g_app.ptr_y) : -1;

    for (int i = 0; i < TAB_COUNT; i++) {
        float x = m + width * (float)i;
        int active = (i == g_app.tab);
        u32 col = active ? C_TEXT : (i == hover ? C_TEXT : C_FAINT);
        float tw = font_width(FONT_BOLD, 12, tab_names[i]);
        float tx = x + (width - tw) * 0.5f;
        if (i == hover && !active)
            gfx_rect(tx - 12.0f, 208.0f, tw + 24.0f, 24.0f, C_PANEL);
        font_draw_sp(FONT_BOLD, 12, tx, 212.0f, col, tab_names[i], 1.2f);
        if (active)
            gfx_rect(tx - 6.0f, 231.0f, tw + 12.0f, 2.0f, C_ACCENT);
    }
    gfx_hline(m, 234.0f, (float)gfx_w - 2.0f * m, C_LINE);

    if (g_app.loading) {
        draw_spinner((float)gfx_w - m - 44.0f, 219.0f, 6.0f, C_ACCENT, g_app.frame);
        font_draw_sp(FONT_BOLD, 9, (float)gfx_w - m - 33.0f, 215.0f, C_ACCENT, "UPDATING", 1.0f);
    }
}

static void draw_today(void)
{
    float m = 26.0f;
    float rx = (float)gfx_w - m;
    weather_t *w = &g_app.wx;

    /* hourly strip */
    gfx_rect(m, 240.0f, rx - m, 80.0f, C_PANEL);
    int n = w->hour_count < 6 ? w->hour_count : 6;
    float cw = (rx - m) / 6.0f;
    for (int i = 0; i < n; i++) {
        wx_hour_t *h = &w->hours[i];
        float cx = m + cw * ((float)i + 0.5f);
        char buf[24];
        if (i == 0)
            snprintf(buf, sizeof(buf), "NOW");
        else
            wx_format_hour(&h->t, buf, sizeof(buf));
        font_draw_sp(FONT_BOLD, 10, cx - font_width_sp(FONT_BOLD, 10, buf, 0.6f) * 0.5f,
                     246.0f, i == 0 ? C_ACCENT : C_DIM, buf, 0.6f);

        draw_wx_icon(cx, 274.0f, 26.0f, h->code, h->is_day);

        snprintf(buf, sizeof(buf), "%d\u00b0", t_round(h->temp_c));
        font_draw(FONT_MEDIUM, 14, cx - font_width(FONT_MEDIUM, 14, buf) * 0.5f, 288.0f,
                  C_TEXT, buf);

        if (h->precip_prob > 0) {
            snprintf(buf, sizeof(buf), "%d%%", h->precip_prob);
            font_draw(FONT_REGULAR, 9, cx - font_width(FONT_REGULAR, 9, buf) * 0.5f, 305.0f,
                      C_RAIN, buf);
        }
        if (i < n - 1)
            gfx_rect(m + cw * (float)(i + 1), 250.0f, 1.0f, 60.0f, RGBA(255, 255, 255, 22));
    }

    /* details grid */
    gfx_rect(m, 330.0f, rx - m, 110.0f, C_PANEL);
    float colw = (rx - m) / 3.0f;
    char val[64];

    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            float x = m + colw * (float)col;
            float y = 330.0f + (float)row * 55.0f;
            const char *name = "";
            char valbuf[64];

            switch (idx) {
            case 0:
                name = "HUMIDITY";
                snprintf(valbuf, sizeof(valbuf), "%d%%", w->humidity);
                break;
            case 1:
                name = "WIND";
                snprintf(valbuf, sizeof(valbuf), "%s %.0f %s", wx_wind_name(w->wind_dir),
                         s_val(w->wind_kmh), speed_unit());
                break;
            case 2:
                name = "UV INDEX";
                snprintf(valbuf, sizeof(valbuf), "%d  %s", w->uv, uv_text(w->uv));
                break;
            case 3:
                name = "VISIBILITY";
                if (g_app.unit_c)
                    snprintf(valbuf, sizeof(valbuf), "%.0f km", w->vis_km);
                else
                    snprintf(valbuf, sizeof(valbuf), "%.0f mi", w->vis_km * 0.621371f);
                break;
            case 4:
                name = "PRESSURE";
                if (g_app.unit_c)
                    snprintf(valbuf, sizeof(valbuf), "%.0f mb", w->pressure_hpa);
                else
                    snprintf(valbuf, sizeof(valbuf), "%.2f inHg", w->pressure_hpa * 0.02953f);
                break;
            default:
                name = "DEW POINT";
                snprintf(valbuf, sizeof(valbuf), "%d\u00b0%s", t_round(w->dew_c), temp_unit());
                break;
            }
            (void)val;
            label(x + 14.0f, y + 10.0f, name, C_FAINT);
            font_draw(FONT_MEDIUM, 15, x + 14.0f, y + 26.0f, C_TEXT, valbuf);

            if (col > 0)
                gfx_rect(x, y + 12.0f, 1.0f, 34.0f, RGBA(255, 255, 255, 22));
        }
    }
    gfx_rect(m, 385.0f, rx - m, 1.0f, RGBA(255, 255, 255, 22));
}

static void draw_hourly(void)
{
    float m = 26.0f;
    float rx = (float)gfx_w - m;
    weather_t *w = &g_app.wx;

    label(m + 6.0f, 243.0f, "TIME", C_FAINT);
    label(m + 92.0f, 243.0f, "CONDITIONS", C_FAINT);
    label(rx - 210.0f, 243.0f, "TEMP", C_FAINT);
    label(rx - 140.0f, 243.0f, "PRECIP", C_FAINT);
    label(rx - 80.0f, 243.0f, "WIND", C_FAINT);

    int visible = 8;
    int max_scroll = w->hour_count > visible ? w->hour_count - visible : 0;
    if (g_app.scroll > max_scroll)
        g_app.scroll = max_scroll;
    if (g_app.scroll < 0)
        g_app.scroll = 0;

    for (int i = 0; i < visible; i++) {
        int idx = g_app.scroll + i;
        if (idx >= w->hour_count)
            break;
        wx_hour_t *h = &w->hours[idx];
        float y = 258.0f + (float)i * 22.5f;

        if (i & 1)
            gfx_rect(m, y - 2.0f, rx - m, 22.0f, RGBA(255, 255, 255, 10));

        char buf[32];
        if (idx == 0)
            snprintf(buf, sizeof(buf), "NOW");
        else
            wx_format_hour(&h->t, buf, sizeof(buf));
        font_draw(FONT_BOLD, 12, m + 6.0f, y + 3.0f, idx == 0 ? C_ACCENT : C_TEXT, buf);

        draw_wx_icon(m + 100.0f, y + 10.0f, 18.0f, h->code, h->is_day);
        draw_clipped(FONT_REGULAR, 12, m + 116.0f, y + 3.0f, 150.0f, C_DIM, h->phrase);

        snprintf(buf, sizeof(buf), "%d\u00b0", t_round(h->temp_c));
        font_draw(FONT_MEDIUM, 13, rx - font_width(FONT_MEDIUM, 13, buf) - 178.0f, y + 2.0f,
                  C_TEXT, buf);

        snprintf(buf, sizeof(buf), "%d%%", h->precip_prob);
        font_draw(FONT_REGULAR, 12, rx - font_width(FONT_REGULAR, 12, buf) - 110.0f, y + 3.0f,
                  h->precip_prob >= 40 ? C_RAIN : C_FAINT, buf);

        snprintf(buf, sizeof(buf), "%.0f %s", s_val(h->wind_kmh), speed_unit());
        font_draw(FONT_REGULAR, 11, rx - font_width(FONT_REGULAR, 11, buf), y + 4.0f, C_DIM, buf);
    }

    if (max_scroll > 0) {
        float track_h = 170.0f;
        float thumb_h = track_h * (float)visible / (float)w->hour_count;
        float thumb_y = 258.0f + (track_h - thumb_h) * (float)g_app.scroll / (float)max_scroll;
        gfx_rect(rx + 4.0f, 258.0f, 2.0f, track_h, RGBA(255, 255, 255, 18));
        gfx_rect(rx + 4.0f, thumb_y, 2.0f, thumb_h, C_ACCENT_SOFT);
    }
}

static void draw_daily(void)
{
    float m = 26.0f;
    float rx = (float)gfx_w - m;
    weather_t *w = &g_app.wx;

    float week_min = 1000.0f, week_max = -1000.0f;
    for (int i = 0; i < w->day_count; i++) {
        if (w->days[i].lo_c < week_min) week_min = w->days[i].lo_c;
        if (w->days[i].hi_c > week_max) week_max = w->days[i].hi_c;
    }
    if (week_max - week_min < 1.0f)
        week_max = week_min + 1.0f;

    for (int i = 0; i < w->day_count && i < 7; i++) {
        wx_day_t *d = &w->days[i];
        float y = 240.0f + (float)i * 28.5f;

        if (i & 1)
            gfx_rect(m, y - 1.0f, rx - m, 28.0f, RGBA(255, 255, 255, 10));

        font_draw_sp(FONT_BOLD, 12, m + 4.0f, y + 6.0f, i == 0 ? C_ACCENT : C_TEXT,
                     i == 0 ? "TODAY" : weekdays[wx_weekday(&d->date)], 0.6f);

        char date[16];
        wx_format_date(&d->date, date, sizeof(date));
        font_draw(FONT_REGULAR, 10, m + 56.0f, y + 7.0f, C_FAINT, date);

        draw_wx_icon(m + 118.0f, y + 14.0f, 20.0f, d->code, 1);
        draw_clipped(FONT_REGULAR, 12, m + 136.0f, y + 6.0f, rx - m - 420.0f, C_DIM, d->phrase);

        char buf[24];
        if (d->precip_prob > 0) {
            snprintf(buf, sizeof(buf), "%d%%", d->precip_prob);
            font_draw(FONT_REGULAR, 11, rx - 300.0f - font_width(FONT_REGULAR, 11, buf),
                      y + 7.0f, C_RAIN, buf);
        }

        /* temperature range bar */
        float bar_x = rx - 274.0f, bar_w = 110.0f;
        gfx_rect(bar_x, y + 12.0f, bar_w, 4.0f, RGBA(255, 255, 255, 26));
        float lo = (d->lo_c - week_min) / (week_max - week_min);
        float hi = (d->hi_c - week_min) / (week_max - week_min);
        gfx_rect(bar_x + bar_w * lo, y + 12.0f, bar_w * (hi - lo) + 1.0f, 4.0f, C_ACCENT);

        snprintf(buf, sizeof(buf), "%d\u00b0", t_round(d->lo_c));
        font_draw(FONT_REGULAR, 12, rx - 150.0f - font_width(FONT_REGULAR, 12, buf), y + 6.0f,
                  C_FAINT, buf);
        snprintf(buf, sizeof(buf), "%d\u00b0", t_round(d->hi_c));
        font_draw(FONT_BOLD, 13, rx - 104.0f - font_width(FONT_BOLD, 13, buf), y + 5.0f,
                  C_TEXT, buf);
    }
}

static void draw_alerts(void)
{
    float m = 26.0f;
    float rx = (float)gfx_w - m;

    if (g_app.wx.alert_count == 0) {
        font_draw_glyph_center(FONT_ICONS, 42, (float)gfx_w * 0.5f, 300.0f, C_GOOD, FA_UMBRELLA);
        font_draw_align(FONT_MEDIUM, 20, m, 330.0f, rx - m, ALIGN_CENTER, C_TEXT,
                        "No active weather alerts");
        font_draw_align(FONT_REGULAR, 11, m, 360.0f, rx - m, ALIGN_CENTER, C_DIM,
                        "There are no severe weather alerts for this location right now.");
        return;
    }

    wx_alert_t *a = &g_app.wx.alerts[0];
    u32 sev = a->severity >= 3 ? C_WARN : (a->severity == 2 ? C_ACCENT : C_RAIN);

    gfx_rect(m, 240.0f, rx - m, 42.0f, RGBA(255, 138, 92, 26));
    gfx_rect(m, 240.0f, 3.0f, 42.0f, sev);
    font_draw_glyph_center(FONT_ICONS, 16, m + 20.0f, 261.0f, sev, FA_WARNING);
    font_draw(FONT_BOLD, 15, m + 34.0f, 248.0f, C_TEXT, a->title);
    label(m + 34.0f, 270.0f,
          a->severity >= 3 ? "SEVERE WEATHER WARNING" :
          a->severity == 2 ? "WEATHER WATCH" : "WEATHER ADVISORY", C_FAINT);

    int lines = font_draw_wrapped(FONT_REGULAR, 12, m, 294.0f, rx - m, 9, 3.0f, C_DIM, a->body);
    (void)lines;

    font_draw(FONT_REGULAR, 10, m, 424.0f, C_FAINT,
              g_app.online ? "Source: AccuWeather alerts"
                           : "Source: bundled Android app fixtures");
}

static void draw_footer(void)
{
    float m = 26.0f;
    gfx_hline(m, 447.0f, (float)gfx_w - 2.0f * m, C_LINE);

    float y = 454.0f;
    float x = m;
#ifdef UI_BOOTINFO
    char mi[96];
    gfx_mode_info(mi, sizeof(mi));
    font_draw(FONT_REGULAR, 11, m, y, C_TEXT, mi);
    extern u32 g_dbg_pressed;
    static const struct { u32 bit; const char *name; } dbg_btn[] = {
        { WPAD_BUTTON_LEFT, "L" }, { WPAD_BUTTON_RIGHT, "R" },
        { WPAD_BUTTON_UP, "U" }, { WPAD_BUTTON_DOWN, "D" },
        { WPAD_BUTTON_A, "A" }, { WPAD_BUTTON_B, "B" },
        { WPAD_BUTTON_1, "1" }, { WPAD_BUTTON_2, "2" },
        { WPAD_BUTTON_PLUS, "+" }, { WPAD_BUTTON_MINUS, "-" },
        { WPAD_BUTTON_HOME, "H" },
    };
    char ib[96];
    int n = snprintf(ib, sizeof(ib), "btn:");
    for (unsigned b = 0; b < sizeof(dbg_btn) / sizeof(dbg_btn[0]); b++)
        if (g_dbg_pressed & dbg_btn[b].bit)
            n += snprintf(ib + n, sizeof(ib) - n, "%s ", dbg_btn[b].name);
    snprintf(ib + n, sizeof(ib) - n, "ptr %d,%d act %d f %d",
             g_app.ptr_x, g_app.ptr_y, g_app.ptr_active, g_app.frame);
    font_draw(FONT_REGULAR, 11, m, y + 14.0f, C_TEXT, ib);
    /* A pixel-exact readout of the latched button word: bit i is a block at
     * 26 + 10*i, filled when the bit has been seen.  OCR is unreliable on an
     * 11px glyph run, a filled block is not. */
    for (int b = 0; b < 32; b++) {
        float bx = m + 10.0f * (float)b;
        if (g_dbg_pressed & (1u << b))
            gfx_rect(bx, 434.0f, 8.0f, 8.0f, C_ACCENT);
        else
            gfx_rect_outline(bx, 434.0f, 8.0f, 8.0f, 1.0f, C_LINE);
    }
#else
    key_hint(x, y, "\u25c0\u25b6", "Tabs");   x += 66.0f;
    key_hint(x, y, "\u25b2\u25bc", "Scroll"); x += 74.0f;
    key_hint(x, y, "A", "Refresh");           x += 76.0f;
    key_hint(x, y, "B", "Units");             x += 68.0f;
    key_hint(x, y, "2", "Search");            x += 76.0f;
    key_hint(x, y, "+/\u2212", "City");       x += 78.0f;
    key_hint(x, y, "HOME", "Exit");
#endif
}

static void draw_status(void)
{
    if (g_app.status_frames <= 0 || !g_app.status[0])
        return;
    float w = font_width(FONT_REGULAR, 11, g_app.status) + 34.0f;
    float x = ((float)gfx_w - w) * 0.5f;
    gfx_rect(x, 418.0f, w, 22.0f, RGBA(10, 24, 40, 235));
    gfx_rect_outline(x, 418.0f, w, 22.0f, 1.0f, RGBA(255, 255, 255, 40));
    font_draw(FONT_REGULAR, 11, x + 17.0f, 422.0f, C_TEXT, g_app.status);
}

static void draw_cursor(void)
{
    if (!g_app.ptr_active)
        return;
    float x = (float)g_app.ptr_x;
    float y = (float)g_app.ptr_y;

    /* A crosshair reads clearly over both the light hero and dark panels and
     * needs no extra texture. */
    gfx_rect(x - 7.0f, y - 1.0f, 15.0f, 2.0f, RGBA(0, 0, 0, 140));
    gfx_rect(x - 1.0f, y - 7.0f, 2.0f, 15.0f, RGBA(0, 0, 0, 140));
    gfx_rect(x - 6.0f, y - 1.0f, 13.0f, 2.0f, C_TEXT);
    gfx_rect(x - 1.0f, y - 6.0f, 2.0f, 13.0f, C_TEXT);
    gfx_rect(x - 2.0f, y - 2.0f, 4.0f, 4.0f, C_ACCENT);
}

void ui_draw_main(void)
{
    weather_t *w = &g_app.wx;

    if (w->is_day)        gfx_vgrad(0, 0, (float)gfx_w, (float)gfx_h,
                  RGB(0x0a, 0x25, 0x40), RGB(0x18, 0x64, 0x99));
    else
        gfx_vgrad(0, 0, (float)gfx_w, (float)gfx_h,
                  RGB(0x04, 0x0a, 0x16), RGB(0x0e, 0x2e, 0x4d));

    draw_header();
    draw_hero();
    draw_tabs();

    switch (g_app.tab) {
    case TAB_HOURLY: draw_hourly(); break;
    case TAB_DAILY:  draw_daily(); break;
    case TAB_ALERTS: draw_alerts(); break;
    default:         draw_today(); break;
    }

    draw_footer();
    draw_status();
    draw_cursor();
}

void ui_draw_splash(const char *status)
{
    gfx_vgrad(0, 0, (float)gfx_w, (float)gfx_h,
              RGB(0x07, 0x18, 0x2c), RGB(0x11, 0x45, 0x72));

    float cx = (float)gfx_w * 0.5f;
    draw_logo(cx - 220.0f, 150.0f, 440.0f);

    font_draw_sp(FONT_BOLD, 12, cx - font_width_sp(FONT_BOLD, 12, "FOR  N I N T E N D O  W I I", 1.0f) * 0.5f,
                 250.0f, C_ACCENT, "FOR  N I N T E N D O  W I I", 1.0f);
    font_draw_align(FONT_REGULAR, 10, 0.0f, 272.0f, (float)gfx_w, ALIGN_CENTER, C_FAINT,
                    "Unofficial native port  \u00b7  reconstructed from the Android 5.3.0 assets");

    draw_spinner(cx, 400.0f, 10.0f, C_ACCENT, g_app.frame);
    if (status)
        font_draw_align(FONT_REGULAR, 11, 0.0f, 424.0f, (float)gfx_w, ALIGN_CENTER, C_DIM, status);
}

void ui_draw_update_popup(const wiiupdate_popup *popup)
{
    gfx_rect(0.0f, 0.0f, (float)gfx_w, (float)gfx_h, RGBA(0, 0, 0, 170));

    float w = 520.0f;
    float x = ((float)gfx_w - w) * 0.5f;
    float y = 140.0f, h = 190.0f;

    gfx_rect(x, y, w, h, RGBA(8, 22, 38, 245));
    gfx_rect_outline(x, y, w, h, 1.0f, RGBA(255, 255, 255, 50));
    gfx_rect(x, y, 3.0f, h, C_ACCENT);

    font_draw(FONT_BOLD, 15, x + 22.0f, y + 16.0f, C_ACCENT, popup->title);
    font_draw_wrapped(FONT_REGULAR, 12, x + 22.0f, y + 44.0f, w - 44.0f, 3, 2.0f, C_TEXT,
                      popup->text);

    if (popup->detail && popup->detail[0])
        font_draw(FONT_REGULAR, 10, x + 22.0f, y + 104.0f, C_FAINT, popup->detail);

    if (popup->progress >= 0) {
        float bw = w - 44.0f;
        gfx_rect(x + 22.0f, y + 126.0f, bw, 8.0f, RGBA(255, 255, 255, 30));
        gfx_rect(x + 22.0f, y + 126.0f, bw * (float)popup->progress / 100.0f, 8.0f, C_ACCENT);
    }

    if (popup->hint && popup->hint[0])
        font_draw(FONT_REGULAR, 10, x + 22.0f, y + h - 26.0f, C_DIM, popup->hint);
}
