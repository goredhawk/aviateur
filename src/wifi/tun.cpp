#include "tun.h"

#ifdef __linux__

    #include <arpa/inet.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <net/if.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <unistd.h>

    #include <cstring>
    #include <stdexcept>
    #include <viface/viface.hpp>

int connect_localhost_udp(const uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        return -1;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (int rc = connect(fd, (struct sockaddr *)&addr, sizeof addr); rc == -1) {
        int connect_errno = errno;
        close(fd);
        errno = connect_errno;
        return -1;
    }

    return fd;
}

int bind_localhost_udp(const uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        return -1;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (int rc = bind(fd, (struct sockaddr *)&addr, sizeof addr); rc == -1) {
        int bind_errno = errno;
        close(fd);
        errno = bind_errno;
        return -1;
    }

    return fd;
}

bool start_tun(const char *address, const uint8_t prefix_bits, const uint16_t send_port, const uint16_t recv_port) {
    // Whatever received from the IP address will be forwarded to localhost:send_port
    const int send_fd = connect_localhost_udp(send_port);
    if (send_fd == -1) {
        fprintf(stderr, "Failed to connect_localhost_udp(%u) failed!", send_port);
        return false;
    }

    // Whatever sent to localhost:recv_port will be forwarded to the IP address
    const int recv_fd = bind_localhost_udp(recv_port);
    if (recv_fd == -1) {
        fprintf(stderr, "bind_localhost_udp(%u) failed!", recv_port);
        return false;
    }

    // Create interface
    viface::VIface iface("viface%d");

    // Configure interface
    iface.setMAC("66:23:2d:28:c6:84"); // This is required
    iface.setIPv4(address);

    // Bring-up interface
    iface.up();

    pollfd poll_fd{};
    poll_fd.fd = recv_fd;
    poll_fd.events = POLLIN;
    char recv_buf[UINT16_MAX];

    while (true) {
        // 1) Capture [ TUN → local localport:8001 UDP ] → rtl8812

        // Ethernet/IP/TCP/Raw
        auto thernet_packet = iface.receive();
        if (!thernet_packet.empty()) {
            auto ip_packet = std::vector(thernet_packet.begin() + 14, thernet_packet.end());
            const size_t count = ip_packet.size();

            // Prepend the packet size in network byte order
            char size_bytes[2];
            size_bytes[0] = (char)((count >> 8) & 0xFF); // High byte
            size_bytes[1] = (char)(count & 0xFF);        // Low byte

            // Combine the size bytes and actual packet data
            char new_buf[2 + count];
            memcpy(new_buf, size_bytes, 2);
            memcpy(new_buf + 2, ip_packet.data(), count);

            // Forward to UDP:8001 on localhost
            send(send_fd, new_buf, 2 + count, 0);

            printf("TUN -> localhost:8001, data size %lu+2\n", count);
        }

        // 2) Capture rtl8812 → [ localport:8000 UDP → TUN ]

        // fixme: enabling polling breaks iface.receive
        // if (const int rc = poll(&poll_fd, 1, 0) == -1; rc < 0) {
        //     if (errno == EINTR || errno == EAGAIN) {
        //         continue;
        //     }
        //     throw std::runtime_error("Poll error");
        // }

        if ((poll_fd.revents & POLLIN) != 0) {
            const ssize_t count = recv(recv_fd, recv_buf, UINT16_MAX, 0);
            if (count < 0) {
                continue;
            }

            std::vector<uint8_t> v(recv_buf + 2, recv_buf + count);

            iface.send(v);

            printf("localhost:8001 -> TUN\n");
        }
    }

    return true;
}

#endif
