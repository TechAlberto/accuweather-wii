#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <stdbool.h>
struct pollsd { int socket; short events, revents; };
static inline u32 net_gethostip(void) { return 1; }
static inline int if_config(char *a, char *b, char *c, bool d, int e) { (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
static inline struct hostent *net_gethostbyname(const char *h) { return gethostbyname(h); }
static inline int net_socket(int d, int t, int p) { (void)p; return socket(d, t, 0); }
static inline int net_connect(int s, struct sockaddr *a, socklen_t l) { return connect(s, a, l); }
static inline int net_close(int s) { return close(s); }
static inline int net_send(int s, const void *b, size_t l, int f) { return (int)send(s, b, l, f); }
static inline int net_recv(int s, void *b, size_t l, int f) { return (int)recv(s, b, l, f); }
static inline int net_poll(struct pollsd *p, int n, int ms)
{
    struct pollfd pf = { p->socket, p->events, 0 };
    (void)n;
    int r = poll(&pf, 1, ms);
    p->revents = pf.revents;
    return r;
}
#ifndef IPPROTO_IP
#define IPPROTO_IP 0
#endif
