#include "tun_tap.h"

/* Copyright (c) John Millikin <john@john-millikin.com> */
/* SPDX-License-Identifier: 0BSD */

#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int tuntap_connect(const char *iface_name, short flags, char *iface_name_out) {
    size_t iface_name_len;

    if (iface_name != NULL) {
        iface_name_len = strlen(iface_name);
        if (iface_name_len >= IFNAMSIZ) {
            errno = EINVAL;
            return -1;
        }
    }

    int tuntap_fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
    if (tuntap_fd == -1) {
        return -1;
    }

    ifreq setiff_request{};
    setiff_request.ifr_flags = flags;
    if (iface_name != NULL) {
        memcpy(setiff_request.ifr_name, iface_name, iface_name_len + 1);
    }

    int rc = ioctl(tuntap_fd, TUNSETIFF, &setiff_request);
    if (rc == -1) {
        int ioctl_errno = errno;
        close(tuntap_fd);
        errno = ioctl_errno;
        return -1;
    }

    if (iface_name_out != NULL) {
        memcpy(iface_name_out, setiff_request.ifr_name, IFNAMSIZ);
    }

    return tuntap_fd;
}

#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdint.h>
#include <string.h>

int netlink_connect() {
    int netlink_fd, rc;
    struct sockaddr_nl sockaddr;

    netlink_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (netlink_fd == -1) {
        return -1;
    }

    memset(&sockaddr, 0, sizeof sockaddr);
    sockaddr.nl_family = AF_NETLINK;
    rc = bind(netlink_fd, (struct sockaddr *)&sockaddr, sizeof sockaddr);
    if (rc == -1) {
        int bind_errno = errno;
        close(netlink_fd);
        errno = bind_errno;
        return -1;
    }
    return netlink_fd;
}

int netlink_set_addr_ipv4(int netlink_fd, const char *iface_name, const char *address, uint8_t network_prefix_bits) {
    struct {
        struct nlmsghdr header;
        struct ifaddrmsg content;
        char attributes_buf[64];
    } request;

    size_t attributes_buf_avail = sizeof request.attributes_buf;

    memset(&request, 0, sizeof request);
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof request.content);
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_EXCL | NLM_F_CREATE;
    request.header.nlmsg_type = RTM_NEWADDR;
    request.content.ifa_index = if_nametoindex(iface_name);
    request.content.ifa_family = AF_INET;
    request.content.ifa_prefixlen = network_prefix_bits;

    /* request.attributes[IFA_LOCAL] = address */
    rtattr *request_attr = IFA_RTA(&request.content);
    request_attr->rta_type = IFA_LOCAL;
    request_attr->rta_len = RTA_LENGTH(sizeof(struct in_addr));
    request.header.nlmsg_len += request_attr->rta_len;
    inet_pton(AF_INET, address, RTA_DATA(request_attr));

    /* request.attributes[IFA_ADDRESS] = address */
    request_attr = RTA_NEXT(request_attr, attributes_buf_avail);
    request_attr->rta_type = IFA_ADDRESS;
    request_attr->rta_len = RTA_LENGTH(sizeof(struct in_addr));
    request.header.nlmsg_len += request_attr->rta_len;
    inet_pton(AF_INET, address, RTA_DATA(request_attr));

    if (send(netlink_fd, &request, request.header.nlmsg_len, 0) == -1) {
        return -1;
    }
    return 0;
}

int netlink_link_up(int netlink_fd, const char *iface_name) {
    struct {
        struct nlmsghdr header;
        struct ifinfomsg content;
    } request;

    memset(&request, 0, sizeof request);
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof request.content);
    request.header.nlmsg_flags = NLM_F_REQUEST;
    request.header.nlmsg_type = RTM_NEWLINK;
    request.content.ifi_index = if_nametoindex(iface_name);
    request.content.ifi_flags = IFF_UP;
    request.content.ifi_change = 1;

    if (send(netlink_fd, &request, request.header.nlmsg_len, 0) == -1) {
        return -1;
    }
    return 0;
}

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>

int run_proxy(int tuntap_fd, int send_fd, int recv_fd) {
    pollfd poll_fds[2];
    char recv_buf[UINT16_MAX];

    poll_fds[0].fd = tuntap_fd;
    poll_fds[0].events = POLLIN;
    poll_fds[1].fd = recv_fd;
    poll_fds[1].events = POLLIN;

    while (true) {
        if (poll(poll_fds, 2, -1) == -1) {
            return -1;
        }

        // 1) Thread to capture [ TUN → local UDP:8001 ] → rtl8812
        if ((poll_fds[0].revents & POLLIN) != 0) {
            ssize_t count = read(tuntap_fd, recv_buf, UINT16_MAX);
            if (count < 0) {
                return -1;
            }
            send(send_fd, recv_buf, count, 0);
        }

        // 2) Thread to capture rtl8812 → [ local UDP:8000 → TUN ]
        if ((poll_fds[1].revents & POLLIN) != 0) {
            ssize_t count = recv(recv_fd, recv_buf, UINT16_MAX, 0);
            if (count < 0) {
                return -1;
            }
            if (write(tuntap_fd, recv_buf, count) == -1) {
                return -1;
            }
        }
    }

    return 0;
}

int bind_localhost_udp(uint16_t port) {
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

int connect_localhost_udp(uint16_t port) {
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

// 使用const避免修改输入，增加线程安全性
int split_address(const char *address_str, uint8_t *network_prefix_bits) {
    char ip_part[INET_ADDRSTRLEN];
    const char *prefix_str = strchr(address_str, '/');

    // 复制IP部分到临时缓冲区
    size_t ip_len = prefix_str ? (size_t)(prefix_str - address_str) : strlen(address_str);
    if (ip_len >= sizeof(ip_part)) return -1;
    strncpy(ip_part, address_str, ip_len);
    ip_part[ip_len] = '\0';

    // 验证IP
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_part, &addr) != 1) return -1;

    // 处理前缀
    *network_prefix_bits = 32;
    if (prefix_str) {
        char *end;
        long prefix = strtol(prefix_str + 1, &end, 10);
        if (*end != '\0' || prefix < 0 || prefix > 32) return -1;
        *network_prefix_bits = (uint8_t)prefix;
    }
    return 0;
}

int parse_port(char *port_str, uint16_t *port) {
    char *extra;
    long raw = strtol(port_str, &extra, 10);

    if (raw < 0 || raw > UINT16_MAX) {
        return -1;
    }
    if (*extra != 0) {
        return -1;
    }
    *port = raw;
    return 0;
}

bool start_tun(char *address, uint16_t send_port, uint16_t recv_port) {
    char iface_name[IFNAMSIZ];
    uint8_t prefix_bits;

    if (split_address(address, &prefix_bits) == -1) {
        fprintf(stderr, "Invalid address \"%s\"\n", address);
        return false;
    }

    // Whatever received from address will be forwarded to localhost:send_port
    int send_fd = bind_localhost_udp(send_port);
    if (send_fd == -1) {
        fprintf(stderr, "bind_localhost_udp(%u): ", send_port);
        return false;
    }

    // Whatever sent to localhost:recv_port will be forwarded to address
    int recv_fd = connect_localhost_udp(recv_port);
    if (recv_fd == -1) {
        fprintf(stderr, "connect_localhost_udp(%u): ", recv_port);
        return false;
    }

    int tuntap_fd = tuntap_connect(NULL, IFF_TUN | IFF_NO_PI, iface_name);
    if (tuntap_fd == -1) {
        fprintf(stderr, "tuntap_connect");
        return false;
    }

    int netlink_fd = netlink_connect();
    if (netlink_fd == -1) {
        fprintf(stderr, "netlink_connect");
        return false;
    }

    int rc = netlink_set_addr_ipv4(netlink_fd, iface_name, address, prefix_bits);
    if (rc == -1) {
        fprintf(stderr, "netlink_set_addr_ipv4");
        return false;
    }
    rc = netlink_link_up(netlink_fd, iface_name);
    if (rc == -1) {
        fprintf(stderr, "netlink_link_up");
        return false;
    }
    close(netlink_fd);

    if (run_proxy(tuntap_fd, send_fd, recv_fd) == -1) {
        perror("run_proxy");
        return false;
    }

    return true;
}
