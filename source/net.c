#include "net.h"

#include <gccore.h>
#include <network.h>
#include <poll.h>
#include <ogc/lwp_watchdog.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <mbedtls/platform_time.h>

#include "ca_bundle.h"

/* HTTPS/HTTP client for the Wii.
 *
 * libogc has no TLS and there is no devkitPPC port of libcurl, so this follows
 * the approach quatric's PictoChat-Client uses on the same platform: mbedTLS
 * drives the socket directly and a small HTTP/1.1 layer sits on top.  The
 * certificate chain is verified against the Mozilla roots in ca_bundle.h and
 * the host name is checked; certificate dates are not, because a Wii clock is
 * very often wrong (the same choice libwiiupdate makes).
 */

#define TIMEOUT_MS 25000
#define ERR_SEND_FAILED (-0x004E)
#define ERR_RECV_FAILED (-0x004C)
#define HOST_MAX 128
#define LOCATION_MAX 256

static int net_ready;
static int tls_ready;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_x509_crt cas;
static mbedtls_ssl_config conf;

/* mbedTLS 3.6 asks the platform for a millisecond clock
 * (MBEDTLS_PLATFORM_MS_TIME_ALT in config/mbedtls_wii.h). */
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)ticks_to_millisecs(gettime());
}

static void set_err(char *err, int errsz, const char *msg)
{
    if (err && errsz > 0) {
        strncpy(err, msg, (size_t)errsz - 1);
        err[errsz - 1] = 0;
    }
}

static void set_err_code(char *err, int errsz, const char *msg, int code)
{
    if (err && errsz > 0)
        snprintf(err, (size_t)errsz, "%s (%d)", msg, code);
    else
        set_err(err, errsz, msg);
}

/* ---------------------------------------------------------------- startup */

int net_startup(void)
{
    if (net_ready)
        return 0;

    /* The system menu usually leaves a lease in place, so this is often a
     * no-op -- but try the link first and only run DHCP when there is no
     * address yet.  if_config can take a few seconds when the console has
     * never been online, which is why the splash is already on screen. */
    if (!net_gethostip()) {
        int ret = -1;
        for (int attempt = 0; attempt < 3 && ret < 0; attempt++) {
            char addr[16] = "", mask[16] = "", gw[16] = "";
            ret = if_config(addr, mask, gw, true, 20);
        }
        if (ret < 0)
            return -1;
    }

    net_ready = 1;
    return 0;
}

int net_is_ready(void)
{
    return net_ready;
}

/* ---------------------------------------------------------------- entropy */

/* The Wii exposes no hardware RNG.  Gather jitter from the CPU timebase while
 * doing memory work: the deltas vary with bus arbitration, cache and DMA. */
static int wii_entropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    static volatile u32 scratch[256];
    for (size_t i = 0; i < len; i++) {
        u32 acc = 0;
        for (int k = 0; k < 16; k++) {
            u64 t0 = gettime();
            for (int n = 0; n < (int)(t0 & 31) + 8; n++)
                scratch[(t0 + (u32)n * 37) & 255] += (u32)t0 ^ (u32)n;
            u64 t1 = gettime();
            acc = (acc << 3 | acc >> 29) ^ (u32)(t1 - t0) ^ (u32)t1;
        }
        out[i] = (unsigned char)(acc ^ acc >> 8 ^ acc >> 16 ^ acc >> 24);
    }
    *olen = len;
    return 0;
}

static int tls_start(char *err, int errsz)
{
    if (tls_ready)
        return 0;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_x509_crt_init(&cas);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_add_source(&entropy, wii_entropy, NULL, 32,
                               MBEDTLS_ENTROPY_SOURCE_STRONG);

    char personal[64];
    snprintf(personal, sizeof(personal), "accuweather-wii %llu",
             (unsigned long long)gettime());

    int ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                    (const unsigned char *)personal,
                                    strlen(personal));
    if (ret)
        goto fail;

    ret = mbedtls_x509_crt_parse(&cas, (const unsigned char *)CA_BUNDLE_PEM,
                                 sizeof(CA_BUNDLE_PEM));
    if (ret < 0)
        goto fail;

    ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret)
        goto fail;

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cas, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    /* Only TLS 1.2 is compiled in (the Wii's heap does not enjoy 1.3). */
    mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);

    tls_ready = 1;
    return 0;

fail:
    set_err_code(err, errsz, "Could not start secure connections", ret);
    return -1;
}

/* ---------------------------------------------------------------- sockets */

static int wait_fd(int fd, int events)
{
    struct pollsd p = { .socket = fd, .events = events, .revents = 0 };
    int ret = net_poll(&p, 1, TIMEOUT_MS);
    if (ret == 0)
        return MBEDTLS_ERR_SSL_TIMEOUT;
    return ret < 0 ? ret : 0;
}

static int sock_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = (int)(intptr_t)ctx;
    if (wait_fd(fd, POLLOUT) < 0)
        return MBEDTLS_ERR_SSL_TIMEOUT;
    int ret = net_send(fd, buf, len > 16384 ? 16384 : (s32)len, 0);
    if (ret == -EAGAIN)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return ret < 0 ? ERR_SEND_FAILED : ret;
}

static int sock_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = (int)(intptr_t)ctx;
    int w = wait_fd(fd, POLLIN);
    if (w < 0)
        return MBEDTLS_ERR_SSL_TIMEOUT;
    int ret = net_recv(fd, buf, (s32)len, 0);
    if (ret == -EAGAIN)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return ret < 0 ? ERR_RECV_FAILED : ret;
}

typedef struct {
    int fd;
    int tls;
    mbedtls_ssl_context ssl;
} conn_t;

static int conn_write(conn_t *c, const void *data, size_t len)
{
    const unsigned char *p = data;
    while (len) {
        int ret = c->tls ? mbedtls_ssl_write(&c->ssl, p, len)
                         : sock_send((void *)(intptr_t)c->fd, p, len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if (ret <= 0)
            return -1;
        p += ret;
        len -= (size_t)ret;
    }
    return 0;
}

static int conn_read(conn_t *c, void *buf, size_t len)
{
    for (;;) {
        int ret = c->tls ? mbedtls_ssl_read(&c->ssl, buf, len)
                         : sock_recv((void *)(intptr_t)c->fd, buf, len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0)
            return 0;
        return ret;
    }
}

static int parse_url(const char *url, int *tls, char *host, size_t host_len,
                     int *port, const char **path)
{
    const char *p;
    if (!strncmp(url, "https://", 8)) {
        *tls = 1; *port = 443; p = url + 8;
    } else if (!strncmp(url, "http://", 7)) {
        *tls = 0; *port = 80; p = url + 7;
    } else {
        return -1;
    }

    const char *end = p + strcspn(p, ":/");
    if (end == p || (size_t)(end - p) >= host_len)
        return -1;
    memcpy(host, p, (size_t)(end - p));
    host[end - p] = 0;
    if (*end == ':') {
        *port = atoi(end + 1);
        end += strcspn(end, "/");
    }
    *path = *end ? end : "/";
    return 0;
}

static int conn_open(conn_t *c, int tls, const char *host, int port,
                     char *err, int errsz)
{
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    c->tls = tls;

    if (!net_ready) {
        set_err(err, errsz, "No network");
        return -1;
    }
    if (tls && tls_start(err, errsz) < 0)
        return -1;

    struct hostent *he = net_gethostbyname(host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        set_err(err, errsz, "DNS lookup failed");
        return -1;
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_len = sizeof(sa);
    sa.sin_port = htons((u16)port);
    memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof(sa.sin_addr));

    c->fd = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (c->fd < 0) {
        set_err_code(err, errsz, "Socket failed", c->fd);
        return -1;
    }

    int ret = net_connect(c->fd, (struct sockaddr *)&sa, sizeof(sa));
    if (ret < 0) {
        net_close(c->fd);
        c->fd = -1;
        set_err_code(err, errsz, "Connect failed", ret);
        return -1;
    }

    if (tls) {
        mbedtls_ssl_init(&c->ssl);
        ret = mbedtls_ssl_setup(&c->ssl, &conf);
        if (!ret)
            ret = mbedtls_ssl_set_hostname(&c->ssl, host);
        if (ret) {
            set_err_code(err, errsz, "Secure setup failed", ret);
            mbedtls_ssl_free(&c->ssl);
            net_close(c->fd);
            c->fd = -1;
            return -1;
        }
        mbedtls_ssl_set_bio(&c->ssl, (void *)(intptr_t)c->fd, sock_send, sock_recv, NULL);
        while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
                continue;
            if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
                set_err(err, errsz, "Server certificate not verified");
            else
                set_err_code(err, errsz, "Secure connection failed", ret);
            mbedtls_ssl_free(&c->ssl);
            net_close(c->fd);
            c->fd = -1;
            return -1;
        }
    }
    return 0;
}

static void conn_close(conn_t *c)
{
    if (c->fd < 0)
        return;
    if (c->tls) {
        mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
    }
    net_close(c->fd);
    c->fd = -1;
}

/* ------------------------------------------------------------------- HTTP */

/* Decode a Transfer-Encoding: chunked body in place. */
static int dechunk(char *body, int len)
{
    int r = 0, w = 0;
    while (r < len) {
        char *nl = strstr(body + r, "\r\n");
        if (!nl)
            return -1;
        long sz = strtol(body + r, NULL, 16);
        r = (int)(nl - body) + 2;
        if (sz <= 0)
            break;
        if (r + sz > len)
            sz = len - r;
        memmove(body + w, body + r, (size_t)sz);
        w += (int)sz;
        r += (int)sz + 2;
    }
    body[w] = 0;
    return w;
}

static int header_value(const char *headers, const char *name,
                        char *out, int outsz)
{
    int nlen = (int)strlen(name);
    const char *p = headers;
    while (*p) {
        if (!strncasecmp(p, name, (size_t)nlen) && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            while (*v == ' ' || *v == '\t')
                v++;
            int i = 0;
            while (*v && *v != '\r' && *v != '\n' && i < outsz - 1)
                out[i++] = *v++;
            out[i] = 0;
            return 1;
        }
        p = strchr(p, '\n');
        if (!p)
            break;
        p++;
    }
    return 0;
}

static int http_once(const char *url, char *out, int cap,
                     char *err, int errsz, int depth)
{
    int tls, port;
    char host[HOST_MAX];
    const char *path;

    if (parse_url(url, &tls, host, sizeof(host), &port, &path) < 0) {
        set_err(err, errsz, "Invalid URL");
        return -1;
    }

    conn_t c;
    if (conn_open(&c, tls, host, port, err, errsz) < 0)
        return -1;

    char head[512];
    int n = snprintf(head, sizeof(head),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: AccuWeatherWii/1.0 (Nintendo Wii; libogc)\r\n"
                     "Accept: application/json\r\n"
                     "Accept-Encoding: identity\r\n"
                     "Connection: close\r\n\r\n",
                     path, host);

    int result = -1;
    if (conn_write(&c, head, (size_t)n) < 0) {
        set_err(err, errsz, "Send failed");
        goto done;
    }

    int len = 0;
    for (;;) {
        if (len >= cap - 1)
            break;
        int r = conn_read(&c, out + len, (size_t)(cap - 1 - len));
        if (r <= 0) {
            if (r < 0 && len == 0) {
                set_err(err, errsz,
                        r == MBEDTLS_ERR_SSL_TIMEOUT ? "Response timeout"
                                                     : "Connection lost");
                goto done;
            }
            break;
        }
        len += r;
    }
    out[len] = 0;

    char *sep = strstr(out, "\r\n\r\n");
    if (strncmp(out, "HTTP/1.", 7) || !sep) {
        set_err(err, errsz, "Malformed response");
        goto done;
    }

    int status = atoi(out + 9);
    *sep = 0; /* terminate the header block so the helpers can scan it */

    if (status == 301 || status == 302 || status == 303 ||
        status == 307 || status == 308) {
        char loc[LOCATION_MAX];
        if (depth < 3 && header_value(out, "Location", loc, sizeof(loc))) {
            conn_close(&c);
            c.fd = -1;
            return http_once(loc, out, cap, err, errsz, depth + 1);
        }
        set_err(err, errsz, "HTTP redirect");
        goto done;
    }
    if (status != 200) {
        if (err && errsz > 0)
            snprintf(err, (size_t)errsz, "HTTP %d", status);
        goto done;
    }

    char *body = sep + 4;
    int body_len = len - (int)(body - out);

    char te[64];
    if (header_value(out, "Transfer-Encoding", te, sizeof(te)) &&
        strstr(te, "chunked")) {
        body_len = dechunk(body, body_len);
        if (body_len < 0) {
            set_err(err, errsz, "Truncated response");
            goto done;
        }
    }

    memmove(out, body, (size_t)body_len);
    out[body_len] = 0;
    result = body_len;

done:
    conn_close(&c);
    return result;
}

int net_http_get(const char *url, char *out, int cap, char *err, int errsz)
{
    if (!net_ready) {
        set_err(err, errsz, "Network not initialised");
        return -1;
    }
    if (!url || !out || cap < 1024) {
        set_err(err, errsz, "Invalid arguments");
        return -1;
    }
    return http_once(url, out, cap, err, errsz, 0);
}
