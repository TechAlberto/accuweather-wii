/* Minimal text-mode app that checks for a newer release at boot and offers
 * to install it. Build with `make -C example`; AUTO=1 builds a variant that
 * presses A by itself, for unattended tests in an emulator. */
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <gccore.h>
#include <fat.h>
#include <wiiuse/wpad.h>
#include <wiiupdate.h>

#ifdef EXAMPLE_AUTO
static u32 auto_a(void)
{
    static int frames;
    return ++frames % 120 == 0 ? WPAD_BUTTON_A : 0;
}
#define BUTTONS auto_a
#else
#define BUTTONS NULL
#endif

int main(int argc, char **argv)
{
    VIDEO_Init();
    WPAD_Init();
    PAD_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(xfb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fatInitDefault();

    printf("\x1b[2;0HExample 1.0\n\n");
    WIIUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
    wiiupdate_state st = WIIUPDATE_CheckConsole("quatric", "GodModeWii", "0.1.0", BUTTONS);
    if (st == WIIUPDATE_UPDATED) {
#ifdef EXAMPLE_AUTO
        printf("\nInstalled to %s\n", WIIUPDATE_InstallDir());
        DIR *d = opendir("sd:/apps");
        for (struct dirent *e; d && (e = readdir(d));) printf(" %s", e->d_name);
        if (d) closedir(d);
        printf("\n");
#else
        exit(0);
#endif
    }

    printf("\nPress HOME to exit.\n");
    for (;;) {
        WPAD_ScanPads();
        if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME) exit(0);
        VIDEO_WaitVSync();
    }
}
