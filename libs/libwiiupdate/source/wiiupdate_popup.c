/* The update popup's logic, shared by GUI apps: they only draw it. */
#include <stdio.h>
#include <wiiuse/wpad.h>
#include "wiiupdate.h"

static int open;

int WIIUPDATE_Popup(u32 down, wiiupdate_popup *p)
{
    if (WIIUPDATE_TakeNotice()) open = 1;
    if (!open) return WIIUPDATE_POPUP_CLOSED;

    int a = (down & WPAD_BUTTON_A) != 0, b = (down & WPAD_BUTTON_B) != 0;
    int result = WIIUPDATE_POPUP_OPEN;
    switch (WIIUPDATE_InstallState()) {
    case WIIUPDATE_INSTALL_IDLE:
        if (a && WIIUPDATE_CanInstall()) WIIUPDATE_Install();
        else if (a || b) open = 0;
        break;
    case WIIUPDATE_INSTALL_DOWNLOADING:
    case WIIUPDATE_INSTALL_EXTRACTING:
        if (b) WIIUPDATE_Cancel();
        break;
    case WIIUPDATE_INSTALL_DONE:
        if (a) result = WIIUPDATE_POPUP_EXIT;
        break;
    case WIIUPDATE_INSTALL_FAILED:
        if (a || b) open = 0;
        break;
    }
    if (!open) return WIIUPDATE_POPUP_CLOSED;

    /* Describe the state after this frame's input. */
    p->progress = -1;
    switch (WIIUPDATE_InstallState()) {
    case WIIUPDATE_INSTALL_IDLE:
        p->title = "Update available";
        snprintf(p->text, sizeof p->text, "%s", WIIUPDATE_Message());
        p->hint = WIIUPDATE_CanInstall() ? "A: Update now    B: Later" : "Press A to continue";
        p->detail = WIIUPDATE_CanInstall() ? "" : WIIUPDATE_Url();
        break;
    case WIIUPDATE_INSTALL_DOWNLOADING:
    case WIIUPDATE_INSTALL_EXTRACTING:
        p->title = "Updating...";
        snprintf(p->text, sizeof p->text, "%s %s",
                 WIIUPDATE_InstallState() == WIIUPDATE_INSTALL_DOWNLOADING ? "Downloading" : "Installing",
                 WIIUPDATE_Latest());
        p->hint = WIIUPDATE_InstallState() == WIIUPDATE_INSTALL_DOWNLOADING ? "B: Cancel" : "";
        p->detail = "";
        p->progress = WIIUPDATE_Progress();
        break;
    case WIIUPDATE_INSTALL_DONE:
        p->title = "Updated";
        snprintf(p->text, sizeof p->text, "Version %s is installed.", WIIUPDATE_Latest());
        p->hint = "Press A to return to the loader";
        p->detail = "";
        break;
    case WIIUPDATE_INSTALL_FAILED:
        p->title = "Update failed";
        snprintf(p->text, sizeof p->text, "Could not update: %s.", WIIUPDATE_InstallError());
        p->hint = "Press A to continue";
        p->detail = "";
        break;
    }
    return result;
}
