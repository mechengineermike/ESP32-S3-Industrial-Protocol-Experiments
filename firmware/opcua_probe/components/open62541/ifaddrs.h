#pragma once

#include <errno.h>
#include <sys/socket.h>

struct ifaddrs {
    struct ifaddrs *ifa_next;
    char *ifa_name;
    unsigned int ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    union {
        struct sockaddr *ifu_broadaddr;
        struct sockaddr *ifu_dstaddr;
    } ifa_ifu;
    void *ifa_data;
};

static inline int getifaddrs(struct ifaddrs **ifap)
{
    if (ifap) {
        *ifap = NULL;
    }
    errno = ENOSYS;
    return -1;
}

static inline void freeifaddrs(struct ifaddrs *ifa)
{
    (void)ifa;
}
