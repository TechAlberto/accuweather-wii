# libwiiupdate

Checks GitHub for a newer release of a Wii homebrew app and can install it in
place, built on libogc.

- Runs on its own thread: brings the network up, asks the GitHub releases API
  over HTTPS and compares the newest release tag with the running version
- Ships its own small mbedTLS (TLS 1.2, certificate chain and host name
  verified against the bundled roots; dates are not checked since many Wii
  clocks are wrong). Its symbols are renamed inside the archive, so apps that
  link their own mbedTLS are unaffected
- Picks the highest numbered, non-prerelease tag, so rolling tags such as
  `latest` are ignored
- Failures are quiet: no network or no reply just means no notice
- Updates in place: downloads the release's Homebrew Channel zip (streamed to
  the SD card, so size does not matter), then unpacks its `apps/<Name>/` folder
  over the running app's folder. Every file is written under a temporary name
  first, so a failed download or unpack leaves the installed copy untouched

## Build

Requires devkitPro with devkitPPC and libogc.

```sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
make            # produces lib/libwiiupdate.a
make install    # copies into $DEVKITPRO/portlibs/ppc
```

## Usage

### Text (console) apps

Once at boot, after the console and controllers are up:

```c
#include <wiiupdate.h>

WIIUPDATE_CheckConsole("quatric", "MyApp", APP_VERSION, NULL);
```

It prints `Checking for updates (B to skip)...` followed by `up to date.`,
`skipped.` or `could not check.` and returns. Only when a newer release exists
does it stop and offer `A: Update now    B: Later` (later after 20 seconds).
Updating shows a progress bar (B cancels the download), then
`Updated to v1.3.0. Press A to return to the loader.` and returns
`WIIUPDATE_UPDATED`, at which point the app should exit the way it normally
does:

```c
int main(int argc, char **argv)
{
    ...
    WIIUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
    if (WIIUPDATE_CheckConsole("quatric", "MyApp", APP_VERSION, NULL) == WIIUPDATE_UPDATED)
        return 0;
```

The last argument returns the `WPAD_BUTTON_*` bits pressed this frame. With
`NULL` the library scans Wii Remotes, Classic Controllers and GameCube pads
itself; apps on [libwiiinput](https://github.com/quatric/libwiiinput) pass a small wrapper instead:

```c
static u32 buttons(void) { INPUT_Update(); return INPUT_ButtonsDown(); }
WIIUPDATE_CheckConsole("quatric", "MyApp", APP_VERSION, buttons);
```

### GUI apps

Start the check at launch and show a popup when it finds something:

```c
WIIUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
WIIUPDATE_Start("quatric", "MyApp", APP_VERSION);

while (running) {
    if (WIIUPDATE_TakeNotice()) popup_open = true;  /* true once */
    if (popup_open) {
        draw_box(WIIUPDATE_Message());              /* "Version v1.3.0 is available (you have v1.2.0)." */
        switch (WIIUPDATE_InstallState()) {
        case WIIUPDATE_INSTALL_IDLE:
            draw_text(WIIUPDATE_CanInstall() ? "A: Update now   B: Later" : WIIUPDATE_Url());
            if ((buttons_down & WPAD_BUTTON_A) && WIIUPDATE_CanInstall()) WIIUPDATE_Install();
            else if (buttons_down & (WPAD_BUTTON_A | WPAD_BUTTON_B)) popup_open = false;
            break;
        case WIIUPDATE_INSTALL_DOWNLOADING:
        case WIIUPDATE_INSTALL_EXTRACTING:
            draw_progress(WIIUPDATE_Progress());    /* 0-100 */
            if (buttons_down & WPAD_BUTTON_B) WIIUPDATE_Cancel();
            break;
        case WIIUPDATE_INSTALL_DONE:
            draw_text("Updated. Press A to return to the loader.");
            if (buttons_down & WPAD_BUTTON_A) running = false;
            break;
        case WIIUPDATE_INSTALL_FAILED:
            draw_text(WIIUPDATE_InstallError());    /* "download failed", ... */
            if (buttons_down & (WPAD_BUTTON_A | WPAD_BUTTON_B)) popup_open = false;
            break;
        }
    }
}
```

Or let the library run that state machine and only draw the box:

```c
wiiupdate_popup pop;
int st = WIIUPDATE_Popup(buttons_down, &pop);
if (st == WIIUPDATE_POPUP_EXIT) running = false;   /* installed, back to the loader */
else if (st == WIIUPDATE_POPUP_OPEN) {
    buttons_down = 0;                              /* the popup has the controls */
    draw_box(pop.title, pop.text, pop.detail, pop.hint);
    if (pop.progress >= 0) draw_progress(pop.progress);
}
```

### Installing

`WIIUPDATE_Install()` picks the release's `.zip` asset (names containing
`hbc`, `homebrew` or `wii` win when there are several), or a `.dol` if there is
no zip. It needs the app's storage mounted through libfat; if the app has not
called `fatInitDefault()` the library does, provided the app links `-lfat`.

The target folder is the folder of `argv[0]` as given to
`WIIUPDATE_SetAppPath` (the Homebrew Channel passes e.g.
`sd:/apps/MyApp/boot.dol`). Without it, the zip's own `apps/<Name>/` folder on
`sd:` (or `usb:` when there is no SD card) is used. Files in the app folder
that the zip does not contain, such as saved settings, are left alone, and
files that are already identical are not rewritten.

Apps with large asset folders can limit what an update overwrites:

```c
static const char *const files[] = { "boot.dol", "meta.xml", "icon.png", NULL };
WIIUPDATE_SetUpdateFiles(files);
```

The listed files are replaced when they changed; every other file in the zip
is only written when it is missing from the SD card.

### Other calls

* `WIIUPDATE_State()`: `CHECKING`, `UP_TO_DATE`, `AVAILABLE`, `FAILED` or `CANCELLED`.
* `WIIUPDATE_Wait(ms)`: block until the check is done. Apps that bring up the
  network themselves should call this first so the two do not race.
* `WIIUPDATE_Cancel()`: drop the result; the thread winds down by itself.
* `WIIUPDATE_Latest()`, `WIIUPDATE_Current()`, `WIIUPDATE_CompareVersions(a, b)`.
* `WIIUPDATE_InstallDir()`: the folder the update went to.

### Using it from an app

Add it as a git submodule, build it before the app and link it:

```sh
git submodule add https://github.com/quatric/libwiiupdate.git libwiiupdate
```

```make
LIBS    := -lwiiupdate $(LIBS)        # ahead of -lfat -lwiiuse -lbte -logc
LIBDIRS += $(CURDIR)/libwiiupdate     # include/ and lib/

# after the $(BUILD) rule, so it stays the default goal
$(BUILD): libwiiupdate-lib
.PHONY: libwiiupdate-lib
libwiiupdate-lib:
	@$(MAKE) --no-print-directory -C libwiiupdate
```

The library builds from its own sources (mbedTLS and miniz are vendored), so CI needs
nothing beyond devkitPPC and `submodules: true` on checkout.

See `example/main.c` for a complete program.

## Tests

`tests/host_test.c` checks version comparison, release and asset parsing and
zip unpacking on a desktop; `build/host_check` runs the whole network path
against plain sockets for an end-to-end run against GitHub, and with an app
path also downloads and installs the update there:

```sh
make -f tests/Makefile.host
./build/host_test
./build/host_check quatric GodModeWii 0.1.0
./build/host_check quatric GodModeWii 0.1.0 build/sd/apps/GodModeWii/boot.dol
```

`make -C example` builds a small test program; `make -C example AUTO=1` presses
A by itself, for unattended runs in Dolphin.

## Root certificates

`source/wiiupdate_certs.h` is generated from the Mozilla CA bundle:

```sh
curl -sSLf -o cacert.pem https://curl.se/ca/cacert.pem
python3 scripts/gen_certs.py cacert.pem > source/wiiupdate_certs.h
```

## License

AGPL-3.0, see `LICENSE`. mbedTLS in `vendor/mbedtls` is Apache-2.0 (or
GPL-2.0-or-later), see `vendor/mbedtls/LICENSE`. miniz in `vendor/miniz` is
MIT, see `vendor/miniz/LICENSE`.
