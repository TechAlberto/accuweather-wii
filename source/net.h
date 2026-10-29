#ifndef NET_H
#define NET_H

/* Blocking HTTP/1.1 GET over the Wii's network stack.
 *
 * Both http:// and https:// work: TLS 1.2 runs over mbedTLS, with the server
 * certificate chain verified against the Mozilla roots in source/ca_bundle.h
 * and the host name checked.  Certificate dates are deliberately not checked
 * (Wii clocks are often wrong).
 *
 * Returns the body length, or -1 on failure with err filled in. */
int net_http_get(const char *url, char *out, int cap, char *err, int errsz);

/* Bring up the link (DHCP when there is no lease yet).  Safe to call again;
 * returns 0 when the console is online. */
int net_startup(void);
int net_is_ready(void);

#endif
