/* Tests for the UDP helpers in netutil.c (loopback, ephemeral ports - never 9000-9002). */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include "testlib.h"
#include "netutil.h"

static int port_of(int fd) {
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    getsockname(fd, (struct sockaddr *)&a, &len);
    return ntohs(a.sin_port);
}

TEST(recv_returns_datagram_timeout_and_empty) {
    int rx = net_udp_bind("127.0.0.1", 0), tx = net_udp_socket();
    uint8_t buf[64], msg[10] = {1, 2, 3};
    CHECK(rx >= 0 && tx >= 0);
    CHECK_EQ_INT(net_recv(rx, buf, sizeof buf, 10), 0);                 /* timeout */
    CHECK_EQ_INT(net_sendto(tx, "127.0.0.1", port_of(rx), msg, 10), 0);
    CHECK_EQ_INT(net_recv(rx, buf, sizeof buf, 1000), 10);
    CHECK_EQ_INT(net_sendto(tx, "127.0.0.1", port_of(rx), msg, 0), 0);
    CHECK_EQ_INT(net_recv(rx, buf, sizeof buf, 1000), NET_EMPTY);
    net_close(rx);
    net_close(tx);
}

TEST(oversized_datagram_is_not_returned_truncated) {
    int rx = net_udp_bind("127.0.0.1", 0), tx = net_udp_socket();
    uint8_t buf[32], big[100];
    memset(big, 7, sizeof big);
    CHECK_EQ_INT(net_sendto(tx, "127.0.0.1", port_of(rx), big, sizeof big), 0);
    CHECK(net_recv(rx, buf, sizeof buf, 1000) < 0);   /* used to return 32: a cut-off prefix looked valid */
    CHECK_EQ_INT(net_sendto(tx, "127.0.0.1", port_of(rx), big, sizeof buf), 0);
    CHECK_EQ_INT(net_recv(rx, buf, sizeof buf, 1000), (long)sizeof buf);   /* exactly cap is fine */
    net_close(rx);
    net_close(tx);
}

int main(void) {
    RUN(recv_returns_datagram_timeout_and_empty);
    RUN(oversized_datagram_is_not_returned_truncated);
    return TEST_REPORT();
}
