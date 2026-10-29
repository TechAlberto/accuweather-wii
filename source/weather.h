#ifndef WEATHER_H
#define WEATHER_H

#define MAX_HOURS 24
#define MAX_DAYS  7
#define MAX_ALERTS 4

typedef struct {
    int y, mo, d, hh, mm;
} wx_time_t;

typedef struct {
    wx_time_t t;
    float temp_c;
    float wind_kmh;
    int code;
    int is_day;
    int precip_prob;
    char phrase[48];
} wx_hour_t;

typedef struct {
    wx_time_t date;
    wx_time_t sunrise;
    wx_time_t sunset;
    float hi_c, lo_c;
    int code;
    int precip_prob;
    int uv;
    char phrase[48];
} wx_day_t;

typedef struct {
    char title[64];
    char body[512];
    int severity; /* 0 info, 1 advisory, 2 watch, 3 warning */
} wx_alert_t;

typedef struct {
    char name[40];
    char area[40];
    char country[24];
    float lat, lon;

    wx_time_t updated;

    float temp_c, feels_c, hi_c, lo_c, dew_c;
    float wind_kmh, gust_kmh, pressure_hpa, vis_km, precip_mm;
    int humidity, uv, cloud, is_day, code, wind_dir;
    char condition[40];

    int hour_count;
    wx_hour_t hours[MAX_HOURS];

    int day_count;
    wx_day_t days[MAX_DAYS];

    int alert_count;
    wx_alert_t alerts[MAX_ALERTS];

    int is_demo;
} weather_t;

typedef struct {
    const char *name;
    const char *area;
    const char *country;
    float lat, lon;
} wx_location_t;

/* A city returned by the AccuWeather location search, with the location key
 * the forecast endpoints are driven by. */
typedef struct {
    char key[24];
    char name[40];
    char area[40];
    char country[24];
    float lat, lon;
} wx_place_t;

#define MAX_PLACES 8

/* Weather icon categories (shared by the UI and the demo importer). */
enum {
    WI_SUN = 0,
    WI_PARTLY,
    WI_CLOUD,
    WI_FOG,
    WI_DRIZZLE,
    WI_RAIN,
    WI_SLEET,
    WI_SNOW,
    WI_STORM,
    WI_WIND
};

int  weather_fetch(weather_t *w, const wx_location_t *loc, char *err, int errsz);

/* City search via /locations/v1/cities/search.json.  Fills up to `max` places
 * and returns the number found (0 for no match, -1 on a network error). */
int  weather_search(const char *query, wx_place_t *out, int max,
                    char *err, int errsz);

/* Forecast for a place returned by weather_search(), driven by its location
 * key instead of lat/lon. */
int  weather_fetch_place(weather_t *w, const wx_place_t *place,
                         char *err, int errsz);

void weather_load_demo(weather_t *w);

int         wx_icon_kind(int wmo_code);
const char *wx_condition_text(int wmo_code);
const char *wx_wind_name(int degrees);

/* AccuWeather icon number (1..44) -> the WMO-style code the UI and the bundled
 * fixture data use. */
int         wx_code_from_accuweather(int icon);

int  wx_weekday(const wx_time_t *t); /* 0 = Sunday */
void wx_format_time(const wx_time_t *t, char *buf, int n);   /* 1:42 PM */
void wx_format_hour(const wx_time_t *t, char *buf, int n);   /* 1 PM */
void wx_format_date(const wx_time_t *t, char *buf, int n);   /* OCT 28 */

extern const wx_location_t wx_locations[];
extern const int wx_location_count;

#endif
