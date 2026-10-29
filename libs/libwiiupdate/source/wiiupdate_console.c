#include <stdio.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <ogc/lwp_watchdog.h>
#include "wiiupdate.h"

#define CHECK_LIMIT_MS  20000
#define NOTICE_MS       20000

/* Wii Remotes (and their extensions) plus GameCube pads, folded onto WPAD bits. */
static u32 default_buttons(void)
{
    WPAD_ScanPads();
    PAD_ScanPads();
    u32 down = 0;
    for (int i = 0; i < 4; i++) {
        down |= WPAD_ButtonsDown(i);
        u16 pad = PAD_ButtonsDown(i);
        if (pad & PAD_BUTTON_A) down |= WPAD_BUTTON_A;
        if (pad & PAD_BUTTON_B) down |= WPAD_BUTTON_B;
    }
    if (down & WPAD_CLASSIC_BUTTON_A) down |= WPAD_BUTTON_A;
    if (down & WPAD_CLASSIC_BUTTON_B) down |= WPAD_BUTTON_B;
    return down;
}

static int elapsed_ms(u64 since)
{
    return (int)ticks_to_millisecs(diff_ticks(since, gettime()));
}

static void wait_for_a(u32 (*buttons_down)(void))
{
    while (!(buttons_down() & WPAD_BUTTON_A)) VIDEO_WaitVSync();
}

/* Download and unpack with a progress line; B cancels while downloading. */
static wiiupdate_state install_console(u32 (*buttons_down)(void))
{
    if (WIIUPDATE_Install() < 0) {
        printf("  Could not start the update.\n");
        return WIIUPDATE_AVAILABLE;
    }
    wiiupdate_install_state ist;
    int shown = -1;
    while ((ist = WIIUPDATE_InstallState()) == WIIUPDATE_INSTALL_DOWNLOADING || ist == WIIUPDATE_INSTALL_EXTRACTING) {
        VIDEO_WaitVSync();
        int pct = WIIUPDATE_Progress(), bar = pct / 4;
        if (pct != shown) {
            printf("\r  %s [%.*s%*s] %3d%%", ist == WIIUPDATE_INSTALL_DOWNLOADING ? "Downloading" : "Installing ",
                   bar, "#########################", 25 - bar, "", pct);
            fflush(stdout);
            shown = pct;
        }
        if (ist == WIIUPDATE_INSTALL_DOWNLOADING && (buttons_down() & WPAD_BUTTON_B)) WIIUPDATE_Cancel();
    }
    WIIUPDATE_Wait(5000);
    if (ist == WIIUPDATE_INSTALL_DONE) {
        printf("\n\n  Updated to %s. Press A to return to the loader.\n", WIIUPDATE_Latest());
        wait_for_a(buttons_down);
        return WIIUPDATE_UPDATED;
    }
    printf("\n\n  Update failed: %s.\n  Press A to continue.\n\n", WIIUPDATE_InstallError());
    wait_for_a(buttons_down);
    return WIIUPDATE_AVAILABLE;
}

wiiupdate_state WIIUPDATE_CheckConsole(const char *owner, const char *repo, const char *current_version,
                                       u32 (*buttons_down)(void))
{
    if (!buttons_down) buttons_down = default_buttons;

    printf("Checking for updates (B to skip)... ");
    fflush(stdout);
    if (WIIUPDATE_Start(owner, repo, current_version) < 0) {
        printf("could not check.\n");
        return WIIUPDATE_FAILED;
    }

    u64 start = gettime();
    while (WIIUPDATE_State() == WIIUPDATE_CHECKING) {
        VIDEO_WaitVSync();
        int skip = (buttons_down() & WPAD_BUTTON_B) != 0;
        if (skip || elapsed_ms(start) > CHECK_LIMIT_MS) {
            WIIUPDATE_Cancel();
            printf(skip ? "skipped.\n" : "timed out.\n");
            return WIIUPDATE_CANCELLED;
        }
    }

    wiiupdate_state st = WIIUPDATE_State();
    if (st == WIIUPDATE_UP_TO_DATE) {
        printf("up to date.\n");
    } else if (st == WIIUPDATE_AVAILABLE) {
        WIIUPDATE_TakeNotice();
        int can = WIIUPDATE_CanInstall();
        printf("\n\n  %s\n", WIIUPDATE_Message());
        if (can) printf("\n  A: Update now    B: Later\n\n");
        else printf("  Download it from %s\n\n  Press A to continue.\n\n", WIIUPDATE_Url());
        u32 pressed = 0;
        start = gettime();
        while (elapsed_ms(start) < NOTICE_MS) {
            VIDEO_WaitVSync();
            if ((pressed = buttons_down() & (WPAD_BUTTON_A | WPAD_BUTTON_B))) break;
        }
        if (can && (pressed & WPAD_BUTTON_A)) return install_console(buttons_down);
    } else {
        printf("could not check.\n");
    }
    return st;
}
