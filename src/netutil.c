#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "netutil.h"

static int make_addr(const char *host, int port, struct sockaddr_in *a) {
    memset(a, 0, sizeof *a);
    a->sin_family = AF_INET;
    a->sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &a->sin_addr) != 1) {
        fprintf(stderr, "error: bad IPv4 address '%s'\n", host);
        return -1;
    }
    return 0;
}

int net_udp_socket(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) perror("socket");
    return fd;
}

int net_udp_bind(const char *host, int port) {
    struct sockaddr_in a;
    if (make_addr(host, port, &a)) return -1;
    int fd = net_udp_socket();
    if (fd < 0) return -1;
    int one = 1, big = 1 << 20;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &big, sizeof big); /* best effort, may be capped */
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        fprintf(stderr, "error: cannot bind UDP %s:%d: %s (is another instance running?)\n",
                host, port, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

int net_sendto(int fd, const char *host, int port, const void *buf, size_t len) {
    struct sockaddr_in a;
    if (make_addr(host, port, &a)) return -1;
    ssize_t n = sendto(fd, buf, len, 0, (struct sockaddr *)&a, sizeof a);
    return n == (ssize_t)len ? 0 : -1;
}

long net_recv(int fd, void *buf, size_t cap, int timeout_ms) {
    struct pollfd p;
    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    int r = poll(&p, 1, timeout_ms);
    if (r == 0) return 0;
    if (r < 0) return -1;
    ssize_t n = recvfrom(fd, buf, cap, 0, NULL, NULL);
    return n < 0 ? -1 : (long)n;
}

void net_close(int fd) {
    if (fd >= 0) close(fd);
}

double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

void sleep_us(long us) {
    struct timespec ts;
    ts.tv_sec = us / 1000000L;
    ts.tv_nsec = (us % 1000000L) * 1000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR && !stop_requested()) {}
}

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

void install_stop_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART: poll() returns EINTR so loops notice the flag */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int stop_requested(void) { return g_stop != 0; }
