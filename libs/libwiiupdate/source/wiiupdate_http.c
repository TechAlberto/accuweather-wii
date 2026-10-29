/* Minimal HTTPS GET client: TLS 1.2 via the bundled mbedTLS, follows
 * redirects, and streams the body to a callback so large downloads never
 * have to fit in memory. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <gccore.h>
#include <network.h>
#include <poll.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/platform_time.h>
#include "wiiupdate_internal.h"
#include "wiiupdate_certs.h"

#define TIMEOUT_MS     15000
#define MAX_REDIRECTS  5
#define URL_MAX        2048

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)ticks_to_millisecs(gettime());
}

/* The Wii has no exposed hardware RNG; gather timing jitter from the CPU
 * timebase while doing memory work, which varies with bus and cache state. */
static int wii_entropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    static volatile u32 scratch[256];
    for (size_t i = 0; i < len; i++) {
        u32 acc = 0;
        for (int k = 0; k < 16; k++) {
            u64 t0 = gettime();
            for (int n = 0; n < (int)(t0 & 31) + 8; n++) scratch[(t0 + n * 37) & 255] += (u32)t0 ^ n;
            u64 t1 = gettime();
            acc = (acc << 3 | acc >> 29) ^ (u32)(t1 - t0) ^ (u32)t1;
        }
        out[i] = (unsigned char)(acc ^ acc >> 8 ^ acc >> 16 ^ acc >> 24);
    }
    *olen = len;
    return 0;
}

/* ── Connection ──────────────────────────────────────────────────────────── */

typedef struct {
    int fd;
    volatile int *cancel;
    mbedtls_ssl_context ssl;
    unsigned char buf[4096];
    size_t pos, len;
    int eof;
} conn_t;

static struct {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt cas;
    mbedtls_ssl_config conf;
} tls;

static int wait_fd(conn_t *c, int events)
{
    /* Poll in short slices so a cancel is noticed quickly. */
    for (int waited = 0; waited < TIMEOUT_MS; waited += 250) {
        if (*c->cancel) return -1;
        struct pollsd p = { .socket = c->fd, .events = events, .revents = 0 };
        int ret = net_poll(&p, 1, 250);
        if (ret < 0) return -1;
        if (ret > 0) return 0;
    }
    return -1;
}

static int sock_send(void *ctx, const unsigned char *buf, size_t len)
{
    conn_t *c = ctx;
    if (wait_fd(c, POLLOUT) < 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    int ret = net_send(c->fd, buf, len > 16384 ? 16384 : len, 0);
    if (ret == -EAGAIN) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return ret < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : ret;
}

static int sock_recv(void *ctx, unsigned char *buf, size_t len)
{
    conn_t *c = ctx;
    if (wait_fd(c, POLLIN) < 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    int ret = net_recv(c->fd, buf, len, 0);
    if (ret == -EAGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
    return ret < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : ret;
}

static int tls_init(void)
{
    mbedtls_entropy_init(&tls.entropy);
    mbedtls_ctr_drbg_init(&tls.drbg);
    mbedtls_x509_crt_init(&tls.cas);
    mbedtls_ssl_config_init(&tls.conf);
    mbedtls_entropy_add_source(&tls.entropy, wii_entropy, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);

    char personal[64];
    snprintf(personal, sizeof personal, "libwiiupdate %llu %lld", (unsigned long long)gettime(), (long long)time(NULL));
    if (mbedtls_ctr_drbg_seed(&tls.drbg, mbedtls_entropy_func, &tls.entropy, (const unsigned char *)personal,
                              strlen(personal)))
        return -1;
    if (mbedtls_x509_crt_parse(&tls.cas, (const unsigned char *)WIIUPDATE_CA_PEM, sizeof WIIUPDATE_CA_PEM) < 0)
        return -1;
    if (mbedtls_ssl_config_defaults(&tls.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT))
        return -1;
    mbedtls_ssl_conf_authmode(&tls.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&tls.conf, &tls.cas, NULL);
    mbedtls_ssl_conf_rng(&tls.conf, mbedtls_ctr_drbg_random, &tls.drbg);
    return 0;
}

static void tls_free(void)
{
    mbedtls_ssl_config_free(&tls.conf);
    mbedtls_x509_crt_free(&tls.cas);
    mbedtls_ctr_drbg_free(&tls.drbg);
    mbedtls_entropy_free(&tls.entropy);
}

static int conn_open(conn_t *c, const char *host)
{
    c->fd = -1;
    c->pos = c->len = 0;
    c->eof = 0;
    mbedtls_ssl_init(&c->ssl);
    if (mbedtls_ssl_setup(&c->ssl, &tls.conf) || mbedtls_ssl_set_hostname(&c->ssl, host)) return -1;

    struct hostent *he = net_gethostbyname(host);
    if (*c->cancel || !he || he->h_addrtype != AF_INET || !he->h_addr_list[0]) return -1;
    c->fd = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (c->fd < 0) return -1;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(443);
    memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof sa.sin_addr);
    if (net_connect(c->fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        net_close(c->fd);
        c->fd = -1;
        return -1;
    }
    mbedtls_ssl_set_bio(&c->ssl, c, sock_send, sock_recv, NULL);

    int ret;
    while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0)
        if (*c->cancel || (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)) return -1;
    return 0;
}

static void conn_close(conn_t *c)
{
    if (c->fd >= 0) {
        mbedtls_ssl_close_notify(&c->ssl);
        net_close(c->fd);
        c->fd = -1;
    }
    mbedtls_ssl_free(&c->ssl);
}

static int conn_write(conn_t *c, const char *data, size_t len)
{
    for (size_t sent = 0; sent < len;) {
        int ret = mbedtls_ssl_write(&c->ssl, (const unsigned char *)data + sent, len - sent);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret <= 0 || *c->cancel) return -1;
        sent += ret;
    }
    return 0;
}

/* Refill the buffer. Returns bytes available, 0 at end of stream, <0 on error. */
static int conn_fill(conn_t *c)
{
    if (c->pos < c->len) return (int)(c->len - c->pos);
    if (c->eof) return 0;
    for (;;) {
        if (*c->cancel) return -1;
        int ret = mbedtls_ssl_read(&c->ssl, c->buf, sizeof c->buf);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            c->eof = 1;
            return 0;
        }
        if (ret < 0) return -1;
        c->pos = 0;
        c->len = ret;
        return ret;
    }
}

/* Read one line without its CR LF; overlong lines are truncated. */
static int conn_line(conn_t *c, char *out, size_t max)
{
    size_t n = 0;
    for (;;) {
        int avail = conn_fill(c);
        if (avail <= 0) return -1;
        char ch = (char)c->buf[c->pos++];
        if (ch == '\n') break;
        if (n + 1 < max) out[n++] = ch;
    }
    if (n && out[n - 1] == '\r') n--;
    out[n] = 0;
    return (int)n;
}

/* Pass exactly len bytes (or until end of stream when len < 0) to the sink. */
static int conn_body(conn_t *c, long long len, wiiupdate_sink sink, void *ctx)
{
    while (len != 0) {
        int avail = conn_fill(c);
        if (avail < 0) return -1;
        if (avail == 0) return len < 0 ? 0 : -1;
        size_t n = (size_t)avail;
        if (len > 0 && (long long)n > len) n = (size_t)len;
        if (sink(ctx, c->buf + c->pos, n) < 0) return -1;
        c->pos += n;
        if (len > 0) len -= n;
    }
    return 0;
}

/* ── GET ─────────────────────────────────────────────────────────────────── */

static int split_url(const char *url, char *host, size_t host_len, const char **path)
{
    if (strncasecmp(url, "https://", 8)) return -1;
    url += 8;
    size_t n = strcspn(url, "/?#");
    if (n == 0 || n >= host_len) return -1;
    memcpy(host, url, n);
    host[n] = 0;
    char *port = strchr(host, ':');
    if (port) {
        if (strcmp(port, ":443")) return -1;
        *port = 0;
    }
    *path = url[n] == '/' ? url + n : "/";
    return 0;
}

int wiiupdate_http_get(const char *start_url, const char *accept, wiiupdate_sink sink, void (*on_length)(void *, long long),
                       void *ctx, volatile int *cancel)
{
    char *url = malloc(URL_MAX), *line = malloc(URL_MAX), *location = malloc(URL_MAX);
    conn_t *c = malloc(sizeof *c);
    int result = -1, tls_ready = 0;
    if (!url || !line || !location || !c) goto out;
    tls_ready = 1;
    if (tls_init() < 0) goto out;
    snprintf(url, URL_MAX, "%s", start_url);
    c->cancel = cancel;

    for (int hop = 0; hop <= MAX_REDIRECTS && !*cancel; hop++) {
        char host[128];
        const char *path;
        if (split_url(url, host, sizeof host, &path) < 0) break;
        if (conn_open(c, host) < 0) {
            conn_close(c);
            break;
        }

        int req = snprintf(line, URL_MAX,
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "User-Agent: libwiiupdate/1.1\r\n"
                           "Accept: %s\r\n"
                           "Accept-Encoding: identity\r\n"
                           "Connection: close\r\n\r\n",
                           path, host, accept ? accept : "*/*");
        int status = 0, chunked = 0;
        long long length = -1;
        location[0] = 0;
        if (req >= URL_MAX || conn_write(c, line, req) < 0 || conn_line(c, line, URL_MAX) < 0 ||
            strncmp(line, "HTTP/1.", 7) || (status = atoi(line + 9)) < 100) {
            conn_close(c);
            break;
        }
        int bad = 0;
        for (;;) {
            int n = conn_line(c, line, URL_MAX);
            if (n < 0) { bad = 1; break; }
            if (n == 0) break;
            if (!strncasecmp(line, "content-length:", 15)) length = strtoll(line + 15, NULL, 10);
            else if (!strncasecmp(line, "transfer-encoding:", 18) && strcasestr(line + 18, "chunked")) chunked = 1;
            else if (!strncasecmp(line, "location:", 9)) {
                const char *v = line + 9;
                while (*v == ' ') v++;
                snprintf(location, URL_MAX, "%s", v);
            }
        }
        if (bad) {
            conn_close(c);
            break;
        }

        if (status >= 300 && status < 400 && location[0]) {
            conn_close(c);
            if (location[0] == '/') { /* same host, absolute path */
                snprintf(line, URL_MAX, "https://%s%s", host, location);
                snprintf(url, URL_MAX, "%s", line);
            } else {
                snprintf(url, URL_MAX, "%s", location);
            }
            continue;
        }
        if (status != 200) {
            conn_close(c);
            break;
        }

        if (chunked) {
            if (on_length) on_length(ctx, -1);
            result = 0;
            for (;;) {
                if (conn_line(c, line, URL_MAX) < 0) { result = -1; break; }
                long long size = strtoll(line, NULL, 16);
                if (size <= 0) break;
                if (conn_body(c, size, sink, ctx) < 0 || conn_line(c, line, URL_MAX) < 0) { result = -1; break; }
            }
        } else {
            if (on_length) on_length(ctx, length);
            result = conn_body(c, length, sink, ctx);
        }
        conn_close(c);
        break;
    }

out:
    if (tls_ready) tls_free();
    free(c);
    free(location);
    free(line);
    free(url);
    return *cancel ? -1 : result;
}
