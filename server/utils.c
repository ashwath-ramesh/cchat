#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "utils.h"

int resolve_server_addrinfo(char* hostname, char* port,
                            struct addrinfo** servinfo) {
  // get address info
  printf(">>>>> resolving dns...\n");

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;  // ipv4 or ipv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  return getaddrinfo(hostname, port, &hints, servinfo);
}

void print_addrinfo(const struct addrinfo* ai) {
  // print
  char ipstr[INET6_ADDRSTRLEN];
  void* addr;
  char* ipver;
  int port;
  struct sockaddr_in* ipv4;
  struct sockaddr_in6* ipv6;

  // Check the address family and cast to the appropriate type
  if (ai->ai_family == AF_INET) {
    // IPv4: cast to sockaddr_in and get sin_addr
    ipv4 = (struct sockaddr_in*)ai->ai_addr;
    addr = &(ipv4->sin_addr);
    port = ntohs(ipv4->sin_port);  // Convert to host byte order
    ipver = "IPv4";
  } else {
    // IPv6: cast to sockaddr_in6 and get sin6_addr
    ipv6 = (struct sockaddr_in6*)ai->ai_addr;
    addr = &(ipv6->sin6_addr);
    port = ntohs(ipv6->sin6_port);  // Convert to host byte order
    ipver = "IPv6";
  }

  inet_ntop(ai->ai_family, addr, ipstr, sizeof(ipstr));
  // Print IP, port, socket type, and protocol
  printf("host %s: %s:%d", ipver, ipstr, port);
  printf(" | socktype: %s",
         ai->ai_socktype == SOCK_STREAM ? "STREAM" : "DGRAM");
  printf(" | protocol: %d", ai->ai_protocol);
  if (ai->ai_canonname) {
    printf(" | canonical: %s", ai->ai_canonname);
  }
  printf("\n");
}
