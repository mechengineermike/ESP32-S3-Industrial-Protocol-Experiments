#pragma once

#include_next <netdb.h>

#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST 1
#endif

static inline const char *gai_strerror(int err)
{
    (void)err;
    return "getaddrinfo error";
}
