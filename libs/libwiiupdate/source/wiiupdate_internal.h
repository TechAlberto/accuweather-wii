/* Shared between the library's source files; not part of the public API. */
#ifndef WIIUPDATE_INTERNAL_H
#define WIIUPDATE_INTERNAL_H

#include <stddef.h>
#include "wiiupdate.h"

/* Receives body bytes as they arrive; return <0 to abort the transfer. */
typedef int (*wiiupdate_sink)(void *ctx, const unsigned char *data, size_t len);

/* HTTPS GET, following up to 5 redirects. on_length (may be NULL) is told the
 * body size (-1 if unknown) before the first sink call. Returns 0 when the
 * whole body of a 200 reply was delivered. */
int wiiupdate_http_get(const char *url, const char *accept, wiiupdate_sink sink, void (*on_length)(void *, long long),
                       void *ctx, volatile int *cancel);

/* Highest numbered non-prerelease tag in a /releases reply. */
int wiiupdate_newest_tag(const char *json, char *out, size_t out_len);

/* Download URL of the asset to install for release `tag`: an HBC .zip
 * (preferring names with "hbc"/"homebrew"/"wii"), else a .dol. */
int wiiupdate_release_asset(const char *json, const char *tag, char *out, size_t out_len);

/* Unpack an HBC zip into app_dir. Entries under ".../apps/<Name>/" land in
 * app_dir; anything outside that folder is skipped (a zip without an apps/
 * folder is unpacked as is). Every file is written to a temporary name first
 * and only renamed over the old one once all of them were written, so a
 * failure leaves the installed app untouched. progress gets bytes written so
 * far and the total. Files whose contents are unchanged are skipped; with
 * update_files (a NULL-terminated list of paths relative to app_dir), any
 * other file is only written when it does not exist yet. Returns 0 on
 * success; err gets a short reason. */
int wiiupdate_extract(const char *zip_path, const char *app_dir, const char *const *update_files,
                      void (*progress)(void *, unsigned long long, unsigned long long), void *ctx, char *err,
                      size_t err_len);

/* The "apps/<Name>" folder name inside an HBC zip, or "" if none. */
int wiiupdate_zip_app_name(const char *zip_path, char *out, size_t out_len);

#endif
