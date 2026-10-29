#include "weather.h"
#include "net.h"
#include "json.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* Live data comes from the AccuWeather API, the same endpoints RiiConnect24's
 * File-Maker uses for the Forecast Channel replacement
 * (Channels/Forecast_Channel/forecast.py):
 *
 *   /locations/v1/cities/geoposition/search.json?q=<lat>,<lon>
 *   /currentconditions/v1/<key>?details=true
 *   /forecasts/v1/hourly/12hour/<key>?details=true&metric=true
 *   /forecasts/v1/daily/5day/<key>?details=true&metric=true
 *   /alerts/v1/<key>?details=true
 *
 * The key below is the shared one File-Maker ships; override it with
 *     make AW_API_KEY=...
 * if you have your own developer key.
 */
#ifndef AW_API_KEY
#define AW_API_KEY "6e30dc9ea2aa4d3eb99ad8f6630174cd"
#endif

#define AW_HOST "https://api.accuweather.com"
#define BODY_MAX (96 * 1024)
#define URL_MAX 512

const wx_location_t wx_locations[] = {
    { "State College", "Pennsylvania", "United States", 40.7934f, -77.8600f },
    { "New York",      "New York",     "United States", 40.7128f, -74.0060f },
    { "Los Angeles",   "California",   "United States", 34.0522f, -118.2437f },
    { "Chicago",       "Illinois",     "United States", 41.8781f, -87.6298f },
    { "London",        "England",      "United Kingdom", 51.5074f, -0.1278f },
    { "Tokyo",         "Tokyo",        "Japan",          35.6762f, 139.6503f },
    { "Sydney",        "New South Wales", "Australia",    -33.8688f, 151.2093f },
};
const int wx_location_count = (int)(sizeof(wx_locations) / sizeof(wx_locations[0]));

static const char *month_names[] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

/* One request body at a time: the fetches are sequential, so a single buffer
 * is enough and keeps 96 KB off the stack. */
static char body[BODY_MAX];

/* -------------------------------------------------------------- AccuWeather */

/* AccuWeather's icon set (1..44) folded onto the WMO-style codes the bundled
 * APK fixture data already uses, so wx_icon_kind() and wx_condition_text()
 * work for both sources. */
int wx_code_from_accuweather(int icon)
{
    switch (icon) {
    case 1: case 2: case 30: case 31: case 33: case 34:
        return 0;                       /* clear / sunny */
    case 3: case 4: case 5: case 35: case 36: case 37:
        return 2;                       /* partly cloudy */
    case 6: case 7: case 8: case 38:
        return 3;                       /* cloudy */
    case 11:
        return 45;                      /* fog */
    case 12: case 13: case 14: case 39: case 40:
        return 80;                      /* showers */
    case 15: case 16: case 17: case 41: case 42:
        return 95;                      /* thunderstorms */
    case 18: case 26:
        return 63;                      /* rain */
    case 19: case 20: case 21: case 43: case 44:
        return 71;                      /* flurries */
    case 22: case 23:
        return 73;                      /* snow */
    case 24: case 25: case 27: case 28: case 29:
        return 66;                      /* ice / sleet */
    case 32:
        return 3;                       /* windy */
    default:
        return 3;
    }
}

const char *wx_condition_text(int code)
{
    switch (code) {
    case 0: return "Sunny";
    case 1: return "Mostly Sunny";
    case 2: return "Partly Cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing Drizzle";
    case 61: return "Light Rain";
    case 63: return "Rain";
    case 65: return "Heavy Rain";
    case 66: case 67: return "Freezing Rain";
    case 71: return "Light Snow";
    case 73: return "Snow";
    case 75: return "Heavy Snow";
    case 77: return "Snow Grains";
    case 80: case 81: return "Rain Showers";
    case 82: return "Heavy Showers";
    case 85: case 86: return "Snow Showers";
    case 95: case 96: case 99: return "Thunderstorms";
    default: return "Fair";
    }
}

int wx_icon_kind(int code)
{
    if (code == 0) return WI_SUN;
    if (code == 1 || code == 2) return WI_PARTLY;
    if (code == 3) return WI_CLOUD;
    if (code == 45 || code == 48) return WI_FOG;
    if (code >= 51 && code <= 57) return WI_DRIZZLE;
    if (code >= 61 && code <= 67) return WI_RAIN;
    if (code >= 71 && code <= 77) return WI_SNOW;
    if (code >= 80 && code <= 82) return WI_RAIN;
    if (code == 85 || code == 86) return WI_SNOW;
    if (code >= 95) return WI_STORM;
    return WI_CLOUD;
}

const char *wx_wind_name(int deg)
{
    static const char *dirs[] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                  "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
    int i = ((deg % 360) + 360) % 360;
    i = (i + 11) / 22;
    if (i > 15) i = 0;
    return dirs[i];
}

int wx_weekday(const wx_time_t *t)
{
    int y = t->y, m = t->mo, d = t->d;
    static const int off[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y -= 1;
    int w = (y + y / 4 - y / 100 + y / 400 + off[m - 1] + d) % 7;
    if (w < 0)
        w += 7;
    return w;
}

void wx_format_time(const wx_time_t *t, char *buf, int n)
{
    int h = t->hh % 12;
    if (h == 0)
        h = 12;
    snprintf(buf, (size_t)n, "%d:%02d %s", h, t->mm, t->hh >= 12 ? "PM" : "AM");
}

void wx_format_hour(const wx_time_t *t, char *buf, int n)
{
    int h = t->hh % 12;
    if (h == 0)
        h = 12;
    snprintf(buf, (size_t)n, "%d %s", h, t->hh >= 12 ? "PM" : "AM");
}

void wx_format_date(const wx_time_t *t, char *buf, int n)
{
    const char *mon = (t->mo >= 1 && t->mo <= 12) ? month_names[t->mo - 1] : "---";
    snprintf(buf, (size_t)n, "%s %d", mon, t->d);
}

static void parse_iso(const char *s, wx_time_t *t)
{
    memset(t, 0, sizeof(*t));
    if (!s)
        return;
    int y = 0, mo = 0, d = 0, hh = 0, mm = 0;
    if (sscanf(s, "%d-%d-%dT%d:%d", &y, &mo, &d, &hh, &mm) >= 3) {
        t->y = y;
        t->mo = mo;
        t->d = d;
        t->hh = hh;
        t->mm = mm;
    }
}

void weather_load_demo(weather_t *w)
{
    extern const weather_t g_demo_weather;
    *w = g_demo_weather;
    w->is_demo = 1;
}

/* --------------------------------------------------------------- the fetch */

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!src)
        src = "";
    snprintf(dst, cap, "%s", src);
}

static int get_json(const char *url, json_value **out, char *err, int errsz)
{
    int len = net_http_get(url, body, (int)sizeof(body), err, errsz);
    if (len <= 0)
        return -1;
    *out = json_parse(body);
    if (!*out) {
        if (err && errsz)
            snprintf(err, (size_t)errsz, "Bad JSON reply");
        return -1;
    }
    return 0;
}

/* Percent-encode a search string for the query parameter. */
static void url_encode(const char *src, char *dst, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;

    for (; *src && n + 4 < cap; src++) {
        unsigned char c = (unsigned char)*src;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            dst[n++] = (char)c;
        } else if (c == ' ') {
            dst[n++] = '+';
        } else {
            dst[n++] = '%';
            dst[n++] = hex[c >> 4];
            dst[n++] = hex[c & 0x0f];
        }
    }
    dst[n] = 0;
}

static int fetch_location(weather_t *w, const wx_location_t *loc,
                          char *key, int keysz, char *err, int errsz)
{
    char url[URL_MAX];
    snprintf(url, sizeof(url),
             AW_HOST "/locations/v1/cities/geoposition/search.json"
             "?q=%.4f,%.4f&apikey=%s",
             loc->lat, loc->lon, AW_API_KEY);

    json_value *root = NULL;
    if (get_json(url, &root, err, errsz) < 0)
        return -1;

    const char *k = json_str(json_get(root, "Key"), "");
    if (!k[0]) {
        json_free(root);
        if (err && errsz)
            snprintf(err, (size_t)errsz, "Location not found");
        return -1;
    }
    copy_str(key, (size_t)keysz, k);

    copy_str(w->name, sizeof(w->name), json_str(json_get(root, "LocalizedName"), loc->name));
    copy_str(w->area, sizeof(w->area),
             json_str(json_get(root, "AdministrativeArea.LocalizedName"), loc->area));
    copy_str(w->country, sizeof(w->country),
             json_str(json_get(root, "Country.LocalizedName"), loc->country));

    float lat = (float)json_num(json_get(root, "GeoPosition.Latitude"), loc->lat);
    float lon = (float)json_num(json_get(root, "GeoPosition.Longitude"), loc->lon);
    w->lat = lat;
    w->lon = lon;

    json_free(root);
    return 0;
}

static int fetch_current(weather_t *w, const char *key, char *err, int errsz)
{
    char url[URL_MAX];
    snprintf(url, sizeof(url),
             AW_HOST "/currentconditions/v1/%s?apikey=%s&details=true",
             key, AW_API_KEY);

    json_value *root = NULL;
    if (get_json(url, &root, err, errsz) < 0)
        return -1;

    json_value *cur = json_at(root, 0);
    if (!cur) {
        json_free(root);
        if (err && errsz)
            snprintf(err, (size_t)errsz, "No current conditions");
        return -1;
    }

    parse_iso(json_str(json_get(cur, "LocalObservationDateTime"), ""), &w->updated);

    int icon = json_int(json_get(cur, "WeatherIcon"), 0);
    w->code = wx_code_from_accuweather(icon);
    copy_str(w->condition, sizeof(w->condition),
             json_str(json_get(cur, "WeatherText"), wx_condition_text(w->code)));

    w->temp_c = (float)json_num(json_get(cur, "Temperature.Metric.Value"), 0);
    w->feels_c = (float)json_num(json_get(cur, "RealFeelTemperature.Metric.Value"), w->temp_c);
    w->dew_c = (float)json_num(json_get(cur, "DewPoint.Metric.Value"), 0);
    w->humidity = json_int(json_get(cur, "RelativeHumidity"), 0);
    w->is_day = json_bool(json_get(cur, "IsDayTime"), 1);
    w->precip_mm = (float)json_num(json_get(cur, "Precip1hr.Metric.Value"), 0);
    w->cloud = json_int(json_get(cur, "CloudCover"), 0);
    w->pressure_hpa = (float)json_num(json_get(cur, "Pressure.Metric.Value"), 0);
    w->wind_kmh = (float)json_num(json_get(cur, "Wind.Speed.Metric.Value"), 0);
    w->gust_kmh = (float)json_num(json_get(cur, "WindGust.Speed.Metric.Value"), 0);
    w->wind_dir = json_int(json_get(cur, "Wind.Direction.Degrees"), 0);
    w->vis_km = (float)json_num(json_get(cur, "Visibility.Metric.Value"), 0);
    w->uv = json_int(json_get(cur, "UVIndex"), 0);

    json_free(root);
    return 0;
}

static int fetch_hourly(weather_t *w, const char *key)
{
    char url[URL_MAX];
    snprintf(url, sizeof(url),
             AW_HOST "/forecasts/v1/hourly/12hour/%s"
             "?apikey=%s&details=true&metric=true",
             key, AW_API_KEY);

    char err[64];
    json_value *root = NULL;
    if (get_json(url, &root, err, sizeof(err)) < 0)
        return -1;

    int count = json_count(root);
    for (int i = 0; i < count && w->hour_count < MAX_HOURS; i++) {
        json_value *h = json_at(root, i);
        wx_hour_t *out = &w->hours[w->hour_count];

        parse_iso(json_str(json_get(h, "DateTime"), ""), &out->t);
        out->temp_c = (float)json_num(json_get(h, "Temperature.Value"), 0);
        out->precip_prob = json_int(json_get(h, "PrecipitationProbability"), 0);
        out->is_day = json_bool(json_get(h, "IsDaylight"), 1);
        out->wind_kmh = (float)json_num(json_get(h, "Wind.Speed.Value"), 0);
        out->code = wx_code_from_accuweather(json_int(json_get(h, "WeatherIcon"), 0));
        copy_str(out->phrase, sizeof(out->phrase),
                 json_str(json_get(h, "IconPhrase"), wx_condition_text(out->code)));
        w->hour_count++;
    }

    json_free(root);
    return w->hour_count > 0 ? 0 : -1;
}

static int fetch_daily(weather_t *w, const char *key)
{
    char url[URL_MAX];
    snprintf(url, sizeof(url),
             AW_HOST "/forecasts/v1/daily/5day/%s"
             "?apikey=%s&details=true&metric=true",
             key, AW_API_KEY);

    char err[64];
    json_value *root = NULL;
    if (get_json(url, &root, err, sizeof(err)) < 0)
        return -1;

    json_value *days = json_get(root, "DailyForecasts");
    int count = json_count(days);

    for (int i = 0; i < count && w->day_count < MAX_DAYS; i++) {
        json_value *d = json_at(days, i);
        wx_day_t *out = &w->days[w->day_count];

        parse_iso(json_str(json_get(d, "Date"), ""), &out->date);
        parse_iso(json_str(json_get(d, "Sun.Rise"), ""), &out->sunrise);
        parse_iso(json_str(json_get(d, "Sun.Set"), ""), &out->sunset);

        out->hi_c = (float)json_num(json_get(d, "Temperature.Maximum.Value"), 0);
        out->lo_c = (float)json_num(json_get(d, "Temperature.Minimum.Value"), 0);
        out->code = wx_code_from_accuweather(json_int(json_get(d, "Day.Icon"), 0));
        out->precip_prob = json_int(json_get(d, "Day.PrecipitationProbability"), 0);

        /* UV comes back inside the air-and-pollen list */
        json_value *pollen = json_get(d, "AirAndPollen");
        int pcount = json_count(pollen);
        for (int p = 0; p < pcount; p++) {
            json_value *entry = json_at(pollen, p);
            if (!strcmp(json_str(json_get(entry, "Name"), ""), "UVIndex")) {
                out->uv = json_int(json_get(entry, "Value"), 0);
                break;
            }
        }

        copy_str(out->phrase, sizeof(out->phrase),
                 json_str(json_get(d, "Day.IconPhrase"), wx_condition_text(out->code)));
        w->day_count++;
    }

    json_free(root);

    if (w->day_count > 0) {
        w->hi_c = w->days[0].hi_c;
        w->lo_c = w->days[0].lo_c;
        if (w->days[0].uv)
            w->uv = w->days[0].uv;
    }
    return w->day_count > 0 ? 0 : -1;
}

static int fetch_alerts(weather_t *w, const char *key)
{
    char url[URL_MAX];
    snprintf(url, sizeof(url),
             AW_HOST "/alerts/v1/%s?apikey=%s&details=true",
             key, AW_API_KEY);

    char err[64];
    json_value *root = NULL;
    if (get_json(url, &root, err, sizeof(err)) < 0)
        return -1;

    int count = json_count(root);
    for (int i = 0; i < count && w->alert_count < MAX_ALERTS; i++) {
        json_value *a = json_at(root, i);
        wx_alert_t *out = &w->alerts[w->alert_count];

        const char *title = json_str(json_get(a, "Description.English"), NULL);
        if (!title)
            title = json_str(json_get(a, "Description.Localized"), NULL);
        if (!title)
            title = json_str(json_get(a, "Type"), "Weather Alert");
        copy_str(out->title, sizeof(out->title), title);

        json_value *texts = json_get(a, "Texts");
        copy_str(out->body, sizeof(out->body),
                 json_str(json_get(json_at(texts, 0), "Text"), title));

        const char *type = json_str(json_get(a, "Type"), "");
        if (!strcasecmp(type, "warning"))
            out->severity = 3;
        else if (!strcasecmp(type, "watch"))
            out->severity = 2;
        else
            out->severity = 1;

        w->alert_count++;
    }

    json_free(root);
    return 0;
}

/* Everything after the location key is known: current conditions plus the
 * nice-to-have forecasts and alerts. */
static int fetch_by_key(weather_t *w, const char *key, char *err, int errsz)
{
    if (fetch_current(w, key, err, errsz) < 0)
        return -1;

    /* The forecasts and alerts are nice-to-have: a stale or missing one must
     * not throw away perfectly good current conditions. */
    fetch_hourly(w, key);
    fetch_daily(w, key);
    fetch_alerts(w, key);

    w->is_demo = 0;
    return 0;
}

int weather_fetch(weather_t *w, const wx_location_t *loc, char *err, int errsz)
{
    char key[24];

    if (net_startup() != 0) {
        if (err && errsz) snprintf(err, (size_t)errsz, "No network");
        return -1;
    }

    weather_t out;
    memset(&out, 0, sizeof(out));
    copy_str(out.name, sizeof(out.name), loc->name);
    copy_str(out.area, sizeof(out.area), loc->area);
    copy_str(out.country, sizeof(out.country), loc->country);
    out.lat = loc->lat;
    out.lon = loc->lon;

    if (fetch_location(&out, loc, key, sizeof(key), err, errsz) < 0)
        return -1;
    if (fetch_by_key(&out, key, err, errsz) < 0)
        return -1;

    *w = out;
    return 0;
}

int weather_search(const char *query, wx_place_t *out, int max,
                   char *err, int errsz)
{
    char q[128];
    char url[URL_MAX];

    if (!query || !query[0])
        return 0;
    if (max <= 0)
        return 0;
    if (net_startup() != 0) {
        if (err && errsz) snprintf(err, (size_t)errsz, "No network");
        return -1;
    }

    url_encode(query, q, sizeof(q));
    snprintf(url, sizeof(url),
             AW_HOST "/locations/v1/cities/search.json"
             "?apikey=%s&q=%s&language=en-us&details=false",
             AW_API_KEY, q);

    json_value *root = NULL;
    if (get_json(url, &root, err, errsz) < 0)
        return -1;

    int count = json_count(root);
    int n = 0;
    for (int i = 0; i < count && n < max; i++) {
        json_value *p = json_at(root, i);
        const char *k = json_str(json_get(p, "Key"), "");
        if (!k[0])
            continue;

        wx_place_t *o = &out[n++];
        memset(o, 0, sizeof(*o));
        copy_str(o->key, sizeof(o->key), k);
        copy_str(o->name, sizeof(o->name),
                 json_str(json_get(p, "LocalizedName"), query));
        copy_str(o->area, sizeof(o->area),
                 json_str(json_get(p, "AdministrativeArea.LocalizedName"), ""));
        copy_str(o->country, sizeof(o->country),
                 json_str(json_get(p, "Country.LocalizedName"), ""));
        o->lat = (float)json_num(json_get(p, "GeoPosition.Latitude"), 0);
        o->lon = (float)json_num(json_get(p, "GeoPosition.Longitude"), 0);
    }

    json_free(root);

    if (n == 0 && err && errsz)
        snprintf(err, (size_t)errsz, "No matches for \"%s\"", query);
    return n;
}

int weather_fetch_place(weather_t *w, const wx_place_t *place,
                        char *err, int errsz)
{
    if (net_startup() != 0) {
        if (err && errsz) snprintf(err, (size_t)errsz, "No network");
        return -1;
    }

    weather_t out;
    memset(&out, 0, sizeof(out));
    copy_str(out.name, sizeof(out.name), place->name);
    copy_str(out.area, sizeof(out.area), place->area);
    copy_str(out.country, sizeof(out.country), place->country);
    out.lat = place->lat;
    out.lon = place->lon;

    if (fetch_by_key(&out, place->key, err, errsz) < 0)
        return -1;

    *w = out;
    return 0;
}
