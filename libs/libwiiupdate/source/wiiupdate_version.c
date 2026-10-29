#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include "wiiupdate_internal.h"

/* ── Versions ────────────────────────────────────────────────────────────── */

int WIIUPDATE_CompareVersions(const char *a, const char *b)
{
    while (*a && !isdigit((unsigned char)*a)) a++;
    while (*b && !isdigit((unsigned char)*b)) b++;
    while (*a || *b) {
        unsigned long x = strtoul(a, (char **)&a, 10);
        unsigned long y = strtoul(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        /* Only "." continues the numeric part; "-beta", " (wii)" etc. end it. */
        a = (*a == '.' && isdigit((unsigned char)a[1])) ? a + 1 : "";
        b = (*b == '.' && isdigit((unsigned char)b[1])) ? b + 1 : "";
    }
    return 0;
}

static int has_digit(const char *s)
{
    for (; *s; s++)
        if (isdigit((unsigned char)*s)) return 1;
    return 0;
}

/* Pull every non-prerelease "tag_name" out of a /releases reply and keep the
 * highest one. A rolling tag such as "latest" has no digits and is skipped. */
int wiiupdate_newest_tag(const char *json, char *out, size_t out_len)
{
    static const char key[] = "\"tag_name\":";
    const char *p = json;
    out[0] = 0;
    while ((p = strstr(p, key))) {
        if (p > json && p[-1] == '\\') { p++; continue; }
        p += sizeof key - 1;
        while (*p == ' ') p++;
        if (*p++ != '"') continue;
        const char *end = strchr(p, '"');
        if (!end) break;
        char tag[48];
        size_t n = (size_t)(end - p) < sizeof tag - 1 ? (size_t)(end - p) : sizeof tag - 1;
        memcpy(tag, p, n);
        tag[n] = 0;
        p = end;

        const char *next = strstr(p, key);
        const char *pre = strstr(p, "\"prerelease\":");
        int prerelease = 0;
        if (pre && (!next || pre < next)) {
            pre += 13;
            while (*pre == ' ') pre++;
            prerelease = !strncmp(pre, "true", 4);
        }
        if (prerelease || !has_digit(tag)) continue;
        if (!out[0] || WIIUPDATE_CompareVersions(tag, out) > 0) snprintf(out, out_len, "%s", tag);
    }
    return out[0] ? 0 : -1;
}

/* Find an unescaped "key": occurrence at or after p. */
static const char *find_key(const char *json, const char *p, const char *key)
{
    size_t n = strlen(key);
    while ((p = strstr(p, key))) {
        if (p > json && p[-1] == '\\') { p += n; continue; }
        return p;
    }
    return NULL;
}

static int ends_with(const char *s, size_t len, const char *suffix)
{
    size_t n = strlen(suffix);
    return len >= n && !strncasecmp(s + len - n, suffix, n);
}

static int contains(const char *s, size_t len, const char *word)
{
    size_t n = strlen(word);
    for (size_t i = 0; i + n <= len; i++)
        if (!strncasecmp(s + i, word, n)) return 1;
    return 0;
}

/* Pick the asset of release `tag` to install: an HBC zip, else a .dol. */
int wiiupdate_release_asset(const char *json, const char *tag, char *out, size_t out_len)
{
    static const char tag_key[] = "\"tag_name\":", url_key[] = "\"browser_download_url\":";
    const char *p = json, *start = NULL;
    out[0] = 0;
    while ((p = find_key(json, p, tag_key))) {
        p += sizeof tag_key - 1;
        while (*p == ' ') p++;
        if (*p != '"') continue;
        size_t n = strlen(tag);
        if (!strncmp(p + 1, tag, n) && p[1 + n] == '"') { start = p; break; }
    }
    if (!start) return -1;
    const char *end = find_key(json, start, tag_key);
    if (!end) end = start + strlen(start);

    int best = 0;
    for (p = start; (p = find_key(json, p, url_key)) && p < end;) {
        p += sizeof url_key - 1;
        while (*p == ' ') p++;
        if (*p++ != '"') continue;
        const char *q = strchr(p, '"');
        if (!q || q > end) break;
        size_t len = (size_t)(q - p);
        int score = 0;
        if (ends_with(p, len, ".zip"))
            score = 2 + (contains(p, len, "hbc") || contains(p, len, "homebrew") || contains(p, len, "wii"));
        else if (ends_with(p, len, ".dol"))
            score = 1;
        if (score > best && len < out_len) {
            memcpy(out, p, len);
            out[len] = 0;
            best = score;
        }
        p = q;
    }
    return best ? 0 : -1;
}
