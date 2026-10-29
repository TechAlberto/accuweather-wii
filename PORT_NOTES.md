# Port notes

What the Android TV app does, how this port replaces each piece, and the
decisions that were not obvious.

## Why a rewrite instead of a wrapper

`com.accuweather.android_5.3.0` contains no native libraries: the UI is a web
view plus Java.  There is no binary to recompile and no sane way to run an
Android runtime on a Wii, so the app was reimplemented in C against libogc.
The layout, type scale, colour palette, icon set and wordmark were taken from
the APK so the result reads as the same app.

## Rendering

* `gfx.c` is a small immediate-mode 2D renderer on GX: solid rects, outlines,
  vertical gradients and IA8 textured quads.  Everything is one texture-less
  TEV stage with alpha blending; the C4/C8 fonts and the wordmark are IA8.
* Logical resolution is 854×480 in 16:9 and 640×480 in 4:3.  The EFB is always
  640×480, so in widescreen the projection is widened and the console's
  anamorphic stretch cancels out, keeping the Android TV proportions.
* Text comes from the APK's four Roboto weights (`font_light/regular/medium/
  bold.ttf`) rasterised with FreeType into one IA8 atlas before the first
  frame.  Glyphs used by the UI are pre-cached in `ui_init()`; the atlas is
  flushed once, which is why textures are never uploaded mid-frame.
* The wordmark is the APK's `widget_branding_dark_med.png`, converted offline
  into a two-mask IA8 image (`data/logo.bin`) so it can be drawn white over the
  dark hero and in AccuWeather orange elsewhere, exactly like the original.
* Dolphin: set **Aspect Ratio = Stretch** and
  `SafeTextureCacheColorSamples = 0`, otherwise the IA8 wordmark and font atlas
  are sampled from a stale texture cache.

## Networking and HTTPS

* libcurl is not available for devkitPPC.  HTTPS is done with mbedTLS 3.6.7
  (the copy vendored by libwiiupdate) on top of libogc's lwIP sockets, the same
  arrangement PictoChat-Client uses.  `net.c` opens a `net_tcp` socket, wraps it
  in an mbedTLS `ssl_context`, sends a minimal HTTP/1.1 GET with
  `Connection: close`, and hands the body back to the JSON parser.
* mbedTLS must be compiled with `config/mbedtls_wii.h` for **both** the library
  and the app (`-DMBEDTLS_CONFIG_FILE="mbedtls_wii.h"`), or structure sizes
  disagree at link time.
* Only GET is supported, no chunked encoding, replies are read into a 96 KB
  buffer.  AccuWeather's largest reply here is the 5-day forecast (~23 KB).
* The CA roots in `certs/` are appended to the mbedTLS trust store at start-up.
* Requests are synchronous on the main thread.  Running them on an LWP worker
  wedges libogc's lwIP after a frame or two; a splash frame is already on
  screen, so blocking for the ~1 s a request takes is invisible.
* The release check (libwiiupdate) starts only after the app's own network I/O
  is finished, because two lwIP users at once is what caused the wedge.

## Data

The endpoints are the ones RiiConnect24's File-Maker (`Forecast_Channel/
forecast.py`) uses for the Forecast Channel replacement, with the shared API
key that project ships:

```
/locations/v1/cities/geoposition/search.json?q=<lat>,<lon>
/locations/v1/cities/search.json?apikey=...&q=<city>
/currentconditions/v1/<key>?details=true
/forecasts/v1/hourly/12hour/<key>?details=true&metric=true
/forecasts/v1/daily/5day/<key>?details=true&metric=true
/alerts/v1/<key>?details=true
```

`json.c` is a small recursive-descent parser with dotted-path lookup
(`json_get(root, "Temperature.Metric.Value")`), which keeps the parsing code
readable without a schema.

If the network is down the app keeps the bundled fixture data
(`weather_load_demo()`), and the header badge switches from LIVE to DEMO.

## Input

`libs/libwiiinput` folds the Wii Remote (+ Nunchuk/Classic), a GameCube pad,
USB keyboards and USB mice into one `INPUT_*` API: buttons as `WPAD_BUTTON_*`
bits, a cursor in logical screen pixels, a key queue.  It is compiled straight
into the app rather than linked as an archive, as its README recommends, so the
`HAVE_*` options match the app.

The cursor is held for ~20 frames after the last live IR sample.  The remote
stops reporting IR the moment it stops moving, and a pointer click can arrive
in that gap; without the hold, tab clicks were ignored even though the button
press was seen.

## On-screen keyboard

`libs/libwiiosk` (quatric/libwiiosk 2.2.0, zlib licence) provides the Wii
system keyboard.  It is built as a static archive and only `osk_dict_en_us` is
referenced, so the linker drops the other eight dictionaries (~600 KB);
`OSK_UseSystemLanguage()` would have pulled all nine in.

* Input comes from the header-only `wiiosk_input_wii.h` adapter, which reads
  the app's existing `INPUT_*` state, so the app does not use
  `OSK_InitUSBKeyboard()` (libwiiinput already owns the keyboard).
* `cfg.screen_width/height` are the app's logical size and `OSK_FLAG_WIDESCREEN`
  is set in 16:9, which makes the pointer map correctly onto the keyboard's
  centred 640×480 canvas.
* `OSK_Draw()` clobbers the GX state, so it is called after `ui_draw_main()` and
  `gfx_frame_begin()` re-sets everything at the start of the next frame.
* The keyboard is created lazily on first use (~0.4 MB of generated textures
  and the unpacked dictionary) and kept for the session.
* Its dictionary, sound effects and Rodin font data are not zlib-licensed
  (they are the console's own keyboard assets, as the upstream README notes);
  keep that in mind before redistributing the DOL commercially.

## Third-party code and licences

| Component | Licence |
| --- | --- |
| libwiiinput | AGPL-3.0 (source drop in `libs/libwiiinput`) |
| libwiiupdate | see `libs/libwiiupdate/LICENSE` |
| libwiiosk | zlib (see `libs/libwiiosk/LICENSE`) |
| mbedTLS | Apache-2.0 |
| FreeType, libpng, libbrotli, zlib, bzip2 | their own licences (devkitPro portlibs) |

AccuWeather, the wordmark, the Roboto fonts and the icon artwork belong to
AccuWeather, Inc.; this is an unofficial port with no affiliation.
