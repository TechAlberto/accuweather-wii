#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <gccore.h>
#include <network.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>
#include "wiiupdate_internal.h"

#define MAX_BODY    (512 * 1024)
#define STACK_SIZE  (128 * 1024)
#define DOWNLOAD_SHARE 80 /* percent of the progress bar spent downloading */

static volatile wiiupdate_state state = WIIUPDATE_IDLE;
static volatile wiiupdate_install_state install_state = WIIUPDATE_INSTALL_IDLE;
static volatile int cancelled;
static volatile int noticed;
static volatile int running; /* a thread is alive, even after a cancel */
static volatile int progress;
static char owner[64], repo[96], current[48], latest[48], url[200], message[200];
static char asset[1024], app_path[256], install_dir[256], install_error[160];
static const char *const *update_files;
static lwp_t thread = LWP_THREAD_NULL;
static u8 stack[STACK_SIZE] ATTRIBUTE_ALIGN(32);

#ifdef GEKKO
/* Weak, so apps without libfat still link; they simply cannot install. */
extern bool fatInitDefault(void) __attribute__((weak));
#endif

static int net_up(void)
{
    if (net_gethostip()) return 0;
    for (int attempt = 0; attempt < 3 && !cancelled; attempt++) {
        char ip[16] = "", mask[16] = "", gw[16] = "";
        if (if_config(ip, mask, gw, true, 20) >= 0) return 0;
    }
    return -1;
}

static int start_thread(void *(*fn)(void *))
{
    running = 1;
    if (LWP_CreateThread(&thread, fn, NULL, stack, STACK_SIZE, 40) < 0) {
        running = 0;
        return -1;
    }
    return 0;
}

/* ── Check ───────────────────────────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t len;
} membuf_t;

static int mem_sink(void *ctx, const unsigned char *data, size_t len)
{
    membuf_t *m = ctx;
    if (m->len + len > MAX_BODY) len = MAX_BODY - m->len; /* keep what fits */
    memcpy(m->data + m->len, data, len);
    m->len += len;
    return 0;
}

static void *check_thread(void *arg)
{
    (void)arg;
    wiiupdate_state result = WIIUPDATE_FAILED;
    char tag[sizeof latest];
    membuf_t body = { malloc(MAX_BODY + 1), 0 };

    if (body.data && net_up() == 0 && !cancelled) {
        char api[256];
        snprintf(api, sizeof api, "https://api.github.com/repos/%s/%s/releases?per_page=10", owner, repo);
        if (wiiupdate_http_get(api, "application/vnd.github+json", mem_sink, NULL, &body, &cancelled) == 0) {
            body.data[body.len] = 0;
            if (wiiupdate_newest_tag(body.data, tag, sizeof tag) == 0 && WIIUPDATE_CompareVersions(tag, current) > 0) {
                snprintf(latest, sizeof latest, "%s", tag);
                snprintf(message, sizeof message, "Version %s is available (you have %s%s).", latest,
                         isdigit((unsigned char)current[0]) ? "v" : "", current);
                if (wiiupdate_release_asset(body.data, tag, asset, sizeof asset) < 0) asset[0] = 0;
                result = WIIUPDATE_AVAILABLE;
            } else {
                result = WIIUPDATE_UP_TO_DATE;
            }
        }
    }
    free(body.data);
    state = cancelled ? WIIUPDATE_CANCELLED : result;
    running = 0;
    return NULL;
}

int WIIUPDATE_Start(const char *o, const char *r, const char *version)
{
    if (running) return -1;
    snprintf(owner, sizeof owner, "%s", o);
    snprintf(repo, sizeof repo, "%s", r);
    snprintf(current, sizeof current, "%s", version ? version : "0");
    snprintf(url, sizeof url, "github.com/%s/%s/releases", owner, repo);
    latest[0] = message[0] = asset[0] = 0;
    cancelled = noticed = 0;
    install_state = WIIUPDATE_INSTALL_IDLE;
    state = WIIUPDATE_CHECKING;
    if (start_thread(check_thread) < 0) {
        state = WIIUPDATE_FAILED;
        return -1;
    }
    return 0;
}

/* ── Install ─────────────────────────────────────────────────────────────── */

typedef struct {
    FILE *f;
    long long total, done;
} download_t;

static int file_sink(void *ctx, const unsigned char *data, size_t len)
{
    download_t *d = ctx;
    if (fwrite(data, 1, len, d->f) != len) return -1;
    d->done += len;
    if (d->total > 0) progress = (int)(d->done * DOWNLOAD_SHARE / d->total);
    return 0;
}

static void on_length(void *ctx, long long len) { ((download_t *)ctx)->total = len; }

static void on_extract(void *ctx, unsigned long long done, unsigned long long total)
{
    (void)ctx;
    if (total) progress = DOWNLOAD_SHARE + (int)(done * (100 - DOWNLOAD_SHARE) / total);
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Where downloads go before the app folder is known: "<dev>:/apps". */
static void storage_root(char *out, size_t len)
{
    char dev[16] = "sd";
    const char *colon = strchr(app_path, ':');
    if (colon && (size_t)(colon - app_path) < sizeof dev) {
        memcpy(dev, app_path, colon - app_path);
        dev[colon - app_path] = 0;
    }
    snprintf(out, len, "%s:/apps", dev);
#ifdef GEKKO
    if (!is_dir(out) && fatInitDefault) fatInitDefault();
    if (!is_dir(out) && !strcmp(dev, "sd") && is_dir("usb:/apps")) snprintf(out, len, "usb:/apps");
#endif
}

static int fail(const char *why)
{
    snprintf(install_error, sizeof install_error, "%s", why);
    return -1;
}

static int install(void)
{
    char *tmp = malloc(320);
    char root[64];
    int result = -1;
    if (!tmp) return fail("out of memory");

    if (app_path[0]) {
        /* argv[0] of the running app: install next to it. */
        snprintf(install_dir, sizeof install_dir, "%s", app_path);
        char *slash = strrchr(install_dir, '/');
        if (slash && slash > install_dir) *slash = 0;
#ifdef GEKKO
        if (!is_dir(install_dir) && fatInitDefault) fatInitDefault();
#endif
        snprintf(root, sizeof root, "%s", install_dir);
    } else {
        install_dir[0] = 0;
        storage_root(root, sizeof root);
    }
    if (!is_dir(root)) {
        free(tmp);
        return fail("no SD card or USB drive found");
    }

    int is_zip = strlen(asset) > 4 && !strcasecmp(asset + strlen(asset) - 4, ".zip");
    snprintf(tmp, 320, "%s/wiiupdate-download.wutmp", root);
    download_t d = { fopen(tmp, "wb"), -1, 0 };
    if (!d.f) {
        free(tmp);
        return fail("cannot write to the SD card");
    }
    install_state = WIIUPDATE_INSTALL_DOWNLOADING;
    int ok = net_up() == 0 && wiiupdate_http_get(asset, "application/octet-stream", file_sink, on_length, &d, &cancelled) == 0;
    if (fclose(d.f) != 0) ok = 0;
    if (!ok) {
        fail(cancelled ? "cancelled" : "download failed");
        goto out;
    }
    progress = DOWNLOAD_SHARE;
    install_state = WIIUPDATE_INSTALL_EXTRACTING;

    if (!install_dir[0]) {
        char name[96];
        if (!is_zip || wiiupdate_zip_app_name(tmp, name, sizeof name) < 0) snprintf(name, sizeof name, "%s", repo);
        snprintf(install_dir, sizeof install_dir, "%s/%s", root, name);
    }

    if (is_zip) {
        if (wiiupdate_extract(tmp, install_dir, update_files, on_extract, NULL, install_error, sizeof install_error) < 0) goto out;
    } else {
        /* A bare .dol replaces the running executable. */
        char dol[320];
        const char *base = app_path[0] ? strrchr(app_path, '/') : NULL;
        base = base ? base + 1 : "boot.dol";
        if (strlen(base) < 4 || strcasecmp(base + strlen(base) - 4, ".dol")) base = "boot.dol";
        mkdir(install_dir, 0777);
        snprintf(dol, sizeof dol, "%s/%s", install_dir, base);
        remove(dol);
        if (rename(tmp, dol) != 0) {
            fail("could not replace the app");
            goto out;
        }
    }
    progress = 100;
    result = 0;

out:
    remove(tmp);
    free(tmp);
    return result;
}

static void *install_thread(void *arg)
{
    (void)arg;
    install_state = install() == 0 ? WIIUPDATE_INSTALL_DONE : WIIUPDATE_INSTALL_FAILED;
    running = 0;
    return NULL;
}

void WIIUPDATE_SetAppPath(const char *argv0)
{
    /* Only a real path is useful; some loaders pass nothing or a bare name. */
    if (argv0 && strchr(argv0, '/')) snprintf(app_path, sizeof app_path, "%s", argv0);
    else app_path[0] = 0;
}

void WIIUPDATE_SetUpdateFiles(const char *const *files) { update_files = files; }

int WIIUPDATE_CanInstall(void) { return state == WIIUPDATE_AVAILABLE && asset[0]; }

int WIIUPDATE_Install(void)
{
    WIIUPDATE_Wait(2000); /* the check thread may still be returning */
    if (running || !WIIUPDATE_CanInstall()) return -1;
    if (install_state == WIIUPDATE_INSTALL_DOWNLOADING || install_state == WIIUPDATE_INSTALL_EXTRACTING) return -1;
    cancelled = 0;
    progress = 0;
    install_error[0] = 0;
    install_state = WIIUPDATE_INSTALL_DOWNLOADING;
    if (start_thread(install_thread) < 0) {
        install_state = WIIUPDATE_INSTALL_FAILED;
        fail("could not start");
        return -1;
    }
    return 0;
}

wiiupdate_install_state WIIUPDATE_InstallState(void) { return install_state; }
int WIIUPDATE_Progress(void) { return progress; }
const char *WIIUPDATE_InstallError(void) { return install_error; }
const char *WIIUPDATE_InstallDir(void) { return install_dir; }

/* ── Shared ──────────────────────────────────────────────────────────────── */

wiiupdate_state WIIUPDATE_State(void) { return state; }

wiiupdate_state WIIUPDATE_Wait(int timeout_ms)
{
    u64 start = gettime();
    while (running) { /* also covers a cancelled check whose thread is still winding down */
        if (timeout_ms >= 0 && ticks_to_millisecs(diff_ticks(start, gettime())) >= (u64)timeout_ms) break;
        usleep(10000);
    }
    return state;
}

void WIIUPDATE_Cancel(void)
{
    cancelled = 1;
    if (install_state == WIIUPDATE_INSTALL_IDLE &&
        (state == WIIUPDATE_CHECKING || state == WIIUPDATE_AVAILABLE))
        state = WIIUPDATE_CANCELLED;
}

int WIIUPDATE_TakeNotice(void)
{
    if (state != WIIUPDATE_AVAILABLE || noticed) return 0;
    noticed = 1;
    return 1;
}

const char *WIIUPDATE_Latest(void) { return latest; }
const char *WIIUPDATE_Current(void) { return current; }
const char *WIIUPDATE_Url(void) { return url; }
const char *WIIUPDATE_Message(void) { return message; }
