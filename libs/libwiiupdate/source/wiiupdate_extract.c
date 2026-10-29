/* Unpacking a downloaded HBC zip over the installed app. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "miniz.h"
#include "wiiupdate_internal.h"

#define TMP_SUFFIX ".wutmp"
#define PATH_LEN   512

/* Offset just past ".../apps/<Name>/" in an entry name, or 0 if the entry
 * has no such folder. name_len receives the length of <Name>. */
static size_t apps_prefix(const char *entry, size_t *name_len)
{
    for (const char *p = entry; (p = strstr(p, "apps/")); p++) {
        if (p != entry && p[-1] != '/') continue;
        const char *name = p + 5, *slash = strchr(name, '/');
        if (!slash || slash == name) continue;
        if (name_len) *name_len = (size_t)(slash - name);
        return (size_t)(slash + 1 - entry);
    }
    return 0;
}

/* The prefix every installed entry must start with: ".../apps/<Name>/" from
 * the first entry that has one, or "" when the zip has no apps/ folder. */
static void zip_prefix(mz_zip_archive *zip, char *prefix, size_t len)
{
    prefix[0] = 0;
    mz_uint count = mz_zip_reader_get_num_files(zip);
    for (mz_uint i = 0; i < count; i++) {
        char name[PATH_LEN];
        mz_zip_reader_get_filename(zip, i, name, sizeof name);
        size_t n = apps_prefix(name, NULL);
        if (n && n < len) {
            memcpy(prefix, name, n);
            prefix[n] = 0;
            return;
        }
    }
}

int wiiupdate_zip_app_name(const char *zip_path, char *out, size_t out_len)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    out[0] = 0;
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return -1;
    char prefix[PATH_LEN];
    zip_prefix(&zip, prefix, sizeof prefix);
    mz_zip_reader_end(&zip);
    size_t name_len;
    size_t n = apps_prefix(prefix, &name_len);
    if (!n || name_len >= out_len) return -1;
    memcpy(out, prefix + n - 1 - name_len, name_len);
    out[name_len] = 0;
    return 0;
}

static int safe_relative(const char *rel)
{
    if (!*rel || rel[0] == '/' || strchr(rel, ':') || strchr(rel, '\\')) return 0;
    for (const char *p = rel; *p;) {
        size_t n = strcspn(p, "/");
        if ((n == 2 && !strncmp(p, "..", 2)) || (n == 1 && p[0] == '.')) return 0;
        p += n;
        while (*p == '/') p++;
    }
    return 1;
}

/* mkdir -p for every folder above `path`. */
static int make_parents(char *path)
{
    char *start = strchr(path, ':');
    start = start ? start + 2 : path + 1; /* skip "sd:/" or the leading "/" */
    for (char *p = start; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        int ok = mkdir(path, 0777) == 0 || errno == EEXIST;
        if (!ok) {
            struct stat st;
            ok = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
        }
        *p = '/';
        if (!ok) return -1;
    }
    return 0;
}

/* 1 when the file at `path` already holds exactly the entry's bytes. */
static int same_file(const char *path, const mz_zip_archive_file_stat *st, char *buf, size_t buf_len)
{
    struct stat fs;
    if (stat(path, &fs) != 0 || (mz_uint64)fs.st_size != st->m_uncomp_size) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    mz_ulong crc = MZ_CRC32_INIT;
    size_t n;
    while ((n = fread(buf, 1, buf_len, f)) > 0) crc = mz_crc32(crc, (const mz_uint8 *)buf, n);
    int err = ferror(f);
    fclose(f);
    return !err && crc == st->m_crc32;
}

static int listed(const char *rel, const char *const *files)
{
    for (; *files; files++)
        if (!strcasecmp(rel, *files)) return 1;
    return 0;
}

typedef struct {
    FILE *f;
    unsigned long long done, total;
    void (*progress)(void *, unsigned long long, unsigned long long);
    void *ctx;
} writer_t;

static size_t write_cb(void *opaque, mz_uint64 ofs, const void *buf, size_t n)
{
    (void)ofs;
    writer_t *w = opaque;
    size_t put = fwrite(buf, 1, n, w->f);
    w->done += put;
    if (w->progress) w->progress(w->ctx, w->done, w->total);
    return put;
}

int wiiupdate_extract(const char *zip_path, const char *app_dir, const char *const *update_files,
                      void (*progress)(void *, unsigned long long, unsigned long long), void *ctx, char *err,
                      size_t err_len)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    snprintf(err, err_len, "bad zip");
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return -1;

    char prefix[PATH_LEN];
    zip_prefix(&zip, prefix, sizeof prefix);
    size_t prefix_len = strlen(prefix);
    mz_uint count = mz_zip_reader_get_num_files(&zip);
    unsigned char *install = calloc(count ? count : 1, 1);
    char *path = malloc(PATH_LEN), *tmp = malloc(PATH_LEN);
    char *iobuf = malloc(64 * 1024);
    int result = -1, eligible = 0;
    writer_t w = { NULL, 0, 0, progress, ctx };
    if (!install || !path || !tmp || !iobuf) {
        snprintf(err, err_len, "out of memory");
        goto out;
    }

    /* Which entries to install, and how many bytes that is. Files that are
     * already there with the same contents are left alone, and so is every
     * file outside update_files (when given) that already exists, so big
     * asset folders are not rewritten on every update. */
    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
        if (strncmp(st.m_filename, prefix, prefix_len)) continue;
        const char *rel = st.m_filename + prefix_len;
        if (!safe_relative(rel)) continue;
        eligible = 1;
        if (snprintf(path, PATH_LEN, "%s/%s", app_dir, rel) >= PATH_LEN) continue;
        struct stat fs;
        if (stat(path, &fs) == 0) {
            if (update_files && !listed(rel, update_files)) continue;
            if (same_file(path, &st, iobuf, 64 * 1024)) continue;
        }
        install[i] = 1;
        w.total += st.m_uncomp_size;
    }
    if (!eligible) {
        snprintf(err, err_len, "nothing to install in the zip");
        goto out;
    }

    /* Pass 1: write everything under temporary names. */
    for (mz_uint i = 0; i < count; i++) {
        if (!install[i]) continue;
        mz_zip_archive_file_stat st;
        mz_zip_reader_file_stat(&zip, i, &st);
        if (snprintf(tmp, PATH_LEN, "%s/%s" TMP_SUFFIX, app_dir, st.m_filename + prefix_len) >= PATH_LEN) {
            snprintf(err, err_len, "path too long");
            goto out;
        }
        if (make_parents(tmp) < 0 || !(w.f = fopen(tmp, "wb"))) {
            snprintf(err, err_len, "cannot write to %s", app_dir);
            goto out;
        }
        setvbuf(w.f, iobuf, _IOFBF, 64 * 1024);
        int ok = mz_zip_reader_extract_to_callback(&zip, i, write_cb, &w, 0);
        if (fclose(w.f) != 0) ok = 0;
        w.f = NULL;
        if (!ok) {
            snprintf(err, err_len, "could not unpack %s", st.m_filename + prefix_len);
            goto out;
        }
    }
    /* Pass 2: swap the new files in. */
    for (mz_uint i = 0; i < count; i++) {
        if (!install[i]) continue;
        mz_zip_archive_file_stat st;
        mz_zip_reader_file_stat(&zip, i, &st);
        snprintf(path, PATH_LEN, "%s/%s", app_dir, st.m_filename + prefix_len);
        snprintf(tmp, PATH_LEN, "%s" TMP_SUFFIX, path);
        remove(path);
        if (rename(tmp, path) != 0) {
            snprintf(err, err_len, "could not replace %s", st.m_filename + prefix_len);
            goto out;
        }
        install[i] = 0;
    }
    result = 0;
    err[0] = 0;

out:
    if (w.f) fclose(w.f);
    /* Drop temporary files left by a failed pass 1. */
    for (mz_uint i = 0; result < 0 && install && tmp && i < count; i++) {
        if (!install[i]) continue;
        mz_zip_archive_file_stat st;
        mz_zip_reader_file_stat(&zip, i, &st);
        snprintf(tmp, PATH_LEN, "%s/%s" TMP_SUFFIX, app_dir, st.m_filename + prefix_len);
        remove(tmp);
    }
    mz_zip_reader_end(&zip);
    free(iobuf);
    free(tmp);
    free(path);
    free(install);
    return result;
}
