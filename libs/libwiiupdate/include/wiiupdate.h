/* libwiiupdate: check GitHub for a newer release of a Wii homebrew app, and
 * optionally install it over the running copy.
 *
 * The check runs on its own thread (network bring-up, HTTPS request to the
 * GitHub releases API, version comparison) so it never blocks the app.
 *
 * GUI apps:
 *
 *     WIIUPDATE_Start("quatric", "MyApp", "1.2.0");
 *     ...each frame...
 *     if (WIIUPDATE_TakeNotice()) show_popup(WIIUPDATE_Message());
 *
 * Text (console) apps, once at boot:
 *
 *     WIIUPDATE_CheckConsole("quatric", "MyApp", "1.2.0", NULL);
 *
 * which prints one line, lets B skip the check, and only stops to offer the
 * update when a newer version is out. Failures are silent apart from a short
 * "could not check" note; the app just carries on.
 *
 * Installing: call WIIUPDATE_SetAppPath(argv[0]) first so the update lands in
 * the app's own folder, then WIIUPDATE_Install() and poll
 * WIIUPDATE_InstallState() / WIIUPDATE_Progress(). The release's HBC zip is
 * streamed to the SD card, then unpacked over the app folder. Once it reports
 * WIIUPDATE_INSTALL_DONE the app should exit back to the loader.
 */
#ifndef WIIUPDATE_H
#define WIIUPDATE_H

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIIUPDATE_IDLE = 0,     /* WIIUPDATE_Start not called yet */
    WIIUPDATE_CHECKING,     /* thread running */
    WIIUPDATE_UP_TO_DATE,   /* no release newer than the running version */
    WIIUPDATE_AVAILABLE,    /* WIIUPDATE_Latest() is newer */
    WIIUPDATE_FAILED,       /* no network, TLS or HTTP error, bad reply */
    WIIUPDATE_CANCELLED,    /* WIIUPDATE_Cancel was called */
    WIIUPDATE_UPDATED,      /* only from WIIUPDATE_CheckConsole: the update was
                               installed; exit to the loader */
} wiiupdate_state;

typedef enum {
    WIIUPDATE_INSTALL_IDLE = 0,
    WIIUPDATE_INSTALL_DOWNLOADING,
    WIIUPDATE_INSTALL_EXTRACTING,
    WIIUPDATE_INSTALL_DONE,     /* new files are in place */
    WIIUPDATE_INSTALL_FAILED,   /* see WIIUPDATE_InstallError(); the old copy is untouched */
} wiiupdate_install_state;

/* Start a background check of github.com/<owner>/<repo>/releases against
 * current_version (e.g. "1.2.0" or "v1.2.0"). Brings the network up if the
 * app has not. Returns 0 when the thread started. Strings are copied. */
int WIIUPDATE_Start(const char *owner, const char *repo, const char *current_version);

wiiupdate_state WIIUPDATE_State(void);

/* Block until the check thread has finished (cancelled or not) or timeout_ms
 * passes (<0 waits forever). Call before the app brings up or shuts down the
 * network itself, so the two do not race. */
wiiupdate_state WIIUPDATE_Wait(int timeout_ms);

/* Stop caring about the result. The thread finishes in the background; its
 * result is discarded and the state becomes WIIUPDATE_CANCELLED. */
void WIIUPDATE_Cancel(void);

/* Returns 1 exactly once, the first time it is called after a newer release
 * was found. Meant for a GUI popup: poll it every frame. */
int WIIUPDATE_TakeNotice(void);

/* Newest release tag found, e.g. "v1.3.0" ("" until known). */
const char *WIIUPDATE_Latest(void);
/* The running version as passed to WIIUPDATE_Start. */
const char *WIIUPDATE_Current(void);
/* "github.com/<owner>/<repo>/releases" */
const char *WIIUPDATE_Url(void);
/* A short human-readable line for popups, e.g.
 * "Version v1.3.0 is available (you have 1.2.0)." */
const char *WIIUPDATE_Message(void);

/* Tell the library where the running app lives, e.g. argv[0] from main
 * ("sd:/apps/MyApp/boot.dol"). Without it, updates go to
 * sd:/apps/<folder named in the zip>/ (or usb:/apps/ when there is no SD card). */
void WIIUPDATE_SetAppPath(const char *argv0);

/* Limit which installed files an update may overwrite: a NULL-terminated
 * list of paths relative to the app folder, e.g.
 *     static const char *const files[] = { "boot.dol", "meta.xml", "icon.png", NULL };
 *     WIIUPDATE_SetUpdateFiles(files);
 * Listed files are replaced when the release has different contents; every
 * other file in the zip (assets such as sounds or images) is only written
 * when it is missing, so an update does not rewrite large asset folders.
 * The list is not copied and must stay valid. Without it (or with NULL) every
 * file in the zip may be replaced. Either way files that are already
 * identical are skipped. */
void WIIUPDATE_SetUpdateFiles(const char *const *files);

/* 1 when an update was found and its release has an installable asset (an
 * HBC .zip, or a .dol). */
int WIIUPDATE_CanInstall(void);

/* Download and install the update on a background thread. Brings up libfat
 * (fatInitDefault) if the app has not mounted its storage. WIIUPDATE_Cancel
 * stops the download; unpacking is not interrupted. Returns 0 if started. */
int WIIUPDATE_Install(void);

wiiupdate_install_state WIIUPDATE_InstallState(void);
/* 0-100 over download and unpacking. */
int WIIUPDATE_Progress(void);
/* Why the install failed, e.g. "download failed" ("" otherwise). */
const char *WIIUPDATE_InstallError(void);
/* The folder the update was installed to. */
const char *WIIUPDATE_InstallDir(void);

/* Ready-made popup logic for GUI apps. Call once per frame with the
 * WPAD_BUTTON_* bits pressed this frame; while it returns OPEN, draw a box
 * from *popup and keep the buttons away from the app. A installs (or closes
 * the popup when there is nothing to install), B closes it or cancels the
 * download. EXIT means the update is installed and the user pressed A: leave
 * the app the way it normally exits to the loader. */
enum { WIIUPDATE_POPUP_CLOSED = 0, WIIUPDATE_POPUP_OPEN, WIIUPDATE_POPUP_EXIT };
typedef struct {
    const char *title;   /* "Update available", "Updating...", "Updated", "Update failed" */
    char text[160];      /* e.g. "Version v1.3.0 is available (you have v1.2.0)." */
    const char *detail;  /* a second line (the releases URL) or "" */
    const char *hint;    /* buttons, e.g. "A: Update now    B: Later", or "" */
    int progress;        /* 0-100 while updating (draw a bar), else -1 */
} wiiupdate_popup;
int WIIUPDATE_Popup(u32 buttons_down, wiiupdate_popup *popup);

/* Compare two version strings numerically ("v1.10" > "1.9.2"). Leading text
 * before the first digit is skipped. Returns <0, 0 or >0. */
int WIIUPDATE_CompareVersions(const char *a, const char *b);

/* Console helper for text-mode apps: prints "Checking for updates..." on the
 * current console, waits for the check while polling buttons, and returns
 * the final state. buttons_down returns WPAD_BUTTON_* bits pressed this frame
 * (e.g. a wrapper around INPUT_Update/INPUT_ButtonsDown); pass NULL to have
 * the library scan Wii Remotes and GameCube pads itself. B skips the check.
 * When an update is available it offers "A: update now, B: later" (later is
 * picked after 20 seconds). Returns WIIUPDATE_UPDATED once a new version was
 * installed and the user pressed A; the app should then exit to the loader. */
wiiupdate_state WIIUPDATE_CheckConsole(const char *owner, const char *repo, const char *current_version,
                                       u32 (*buttons_down)(void));

#ifdef __cplusplus
}
#endif

#endif
