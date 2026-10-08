/*
 * netutil.h - thin wrappers around POSIX UDP sockets, sleeping and a monotonic clock.
 */
#ifndef NETUTIL_H
#define NETUTIL_H

#include <stddef.h>

/* UDP socket bound to host:port (SO_REUSEADDR). Returns fd or -1 (error printed). */
int net_udp_bind(const char *host, int port);

/* Unbound UDP socket for sending. Returns fd or -1. */
int net_udp_socket(void);

/* Returns 0 on success, -1 on error. */
int net_sendto(int fd, const char *host, int port, const void *buf, size_t len);

/* Waits up to timeout_ms (-1 = forever). Returns bytes received (> 0), 0 on timeout,
 * -1 on error or when interrupted by a signal. */
long net_recv(int fd, void *buf, size_t cap, int timeout_ms);

void net_close(int fd);

double now_us(void);            /* monotonic clock in microseconds */
void sleep_us(long us);

/* Installs SIGINT/SIGTERM handlers that set the flag returned by stop_requested(). */
void install_stop_handler(void);
int stop_requested(void);

#endif /* NETUTIL_H */
