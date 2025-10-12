#ifndef UTILS_H
#define UTILS_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include <netdb.h>

int resolve_server_addrinfo(char* hostname, char* port,
                            struct addrinfo** servinfo);
void print_addrinfo(const struct addrinfo* ai);

#endif  // UTILS_H
