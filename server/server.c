#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

#define HOSTNAME "localhost"
#define PORT "3490"
#define BACKLOG 10
#define MAXDATASIZE 256  // max number of bytes we
#define MAXFDS 6         // 1 listener, n clients

int get_listener_socket(void) {
  struct addrinfo* servinfo;
  struct addrinfo* p_ai = NULL;
  int fd = -1;
  int bs;

  // resolve server address
  int gai_status = resolve_server_addrinfo(HOSTNAME, PORT, &servinfo);
  if (gai_status != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_status));
    freeaddrinfo(servinfo);
    return -1;
  }

  // setup the socket
  // loop through results, and connect to the first we can
  p_ai = NULL;
  for (p_ai = servinfo; p_ai != NULL; p_ai = p_ai->ai_next) {
    // create the socket
    if ((fd = socket(p_ai->ai_family, p_ai->ai_socktype, p_ai->ai_protocol)) ==
        -1) {
      fprintf(stderr, "server: socket %s\n", strerror(errno));
      continue;  // try the next address
    }

    // bind to a socket
    if ((bs = bind(fd, p_ai->ai_addr, p_ai->ai_addrlen)) == -1) {
      fprintf(stderr, "server: bind %s\n", strerror(errno));
      close(fd);
      continue;  // try next address
    }
    break;
  }

  // check if any connections exist
  if (p_ai == NULL) {
    fprintf(stderr, "server: failed to connect\n");
    freeaddrinfo(servinfo);
    return -1;
  }

  printf("Connection established: ");
  print_addrinfo(p_ai);

  // set as passive listener for incoming connections
  if (listen(fd, BACKLOG) == -1) {
    fprintf(stderr, "server: listen %s\n", strerror(errno));
    freeaddrinfo(servinfo);
    return -1;
  }

  // set as non-blocking
  fcntl(fd, F_SETFL, O_NONBLOCK);

  printf("server: waiting for connections...\n");
  freeaddrinfo(servinfo);
  return fd;
}

int add_to_pfds(struct pollfd** pfds, int add_fd, bool is_fd_listener,
                int* fd_count) {
  if (is_fd_listener == true) {
    // add server fd to array
    (*pfds)[0].fd = add_fd;
    (*pfds)[0].events = POLLIN | POLLERR;
    (*pfds)[0].revents = 0;
  } else {
    (*pfds)[*fd_count].fd = add_fd;
    (*pfds)[*fd_count].events = POLLIN | POLLHUP | POLLERR;
    (*pfds)[*fd_count].revents = 0;
  }

  (*fd_count)++;
  return 0;
}

int remove_from_pfds(struct pollfd** pfds, int remove_fd, int* fd_count) {
  // do something

  if ((*pfds)[0].fd == remove_fd) {
    // do nothing ???
    return -1;
  } else {
    for (int i = 1; i < *fd_count; i++) {
      if ((*pfds)[i].fd == remove_fd) {
        for (int j = i; j < *fd_count - 1; j++) {
          (*pfds)[j] = (*pfds)[j + 1];
        }
        (*fd_count)--;
        break;
      }
    }
  }

  //
  return 0;
}

int broadcast_to_clients(int fd_count, struct pollfd** pfds, const char* msg,
                         int sender_fd) {
  char msg_w_sender[512];
  snprintf(msg_w_sender, sizeof(msg_w_sender), "from: %d msg: %s\n", sender_fd,
           msg);
  int msglen = strlen(msg_w_sender);

  for (int i = 1; i < fd_count; i++) {
    int sendto_fd = (*pfds)[i].fd;

    if (sendto_fd != sender_fd) {
      int total_sent = 0;

      while (total_sent < msglen) {
        int n =
            send(sendto_fd, msg_w_sender + total_sent, msglen - total_sent, 0);

        if (n == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Buffer full, retry (could add a small delay or use poll)
            continue;
          }
          // Permanent error (EPIPE, ECONNRESET, etc.)
          fprintf(stderr, "broadcast send to fd %d: %s\n", sendto_fd,
                  strerror(errno));
          break;  // Give up on this client, move to next
        }

        total_sent += n;
      }
    }
  }
  return 0;
}

int get_ntop_ip(struct sockaddr_storage remote_addr,
                char p_ip[INET6_ADDRSTRLEN]) {
  void* addr = NULL;

  switch (remote_addr.ss_family) {
    case AF_INET:
      addr = &(((struct sockaddr_in*)&remote_addr)->sin_addr);
      break;
    case AF_INET6:
      addr = &(((struct sockaddr_in6*)&remote_addr)->sin6_addr);
      break;
    default:
      fprintf(stderr, "get_p_ip: unknown address family %d\n",
              remote_addr.ss_family);
      return -1;
  }
  inet_ntop(remote_addr.ss_family, addr, p_ip, INET6_ADDRSTRLEN);

  return 0;
}

int process_new_connection(int listener_fd, int* fd_count,
                           struct pollfd** pfds) {
  struct sockaddr_storage remote_client_addr;
  socklen_t new_client_addr_len;
  int new_client_fd;

  // Accept new connection on listener fd with error checking
  new_client_addr_len = sizeof(remote_client_addr);
  new_client_fd = accept(listener_fd, (struct sockaddr*)&remote_client_addr,
                         &new_client_addr_len);
  if (new_client_fd == -1) {
    fprintf(stderr, "accept: %s\n", strerror(errno));
    return -1;
  }

  // set as non-blocking
  fcntl(new_client_fd, F_SETFL, O_NONBLOCK);

  // Validate if pfds array has space. If not,
  // reject new client with a msg
  if (*fd_count >= MAXFDS) {
    char msg[] = "server at capacity. please try again later.\n";
    send(new_client_fd, msg, strlen(msg), 0);
    // printf("%s", msg);
    close(new_client_fd);
    return -1;
  }

  // Add new client fd to the pfds array
  add_to_pfds(pfds, new_client_fd, false, fd_count);

  // broadcast new client info to chat group
  char client_ip[INET6_ADDRSTRLEN];
  if (get_ntop_ip(remote_client_addr, client_ip) == -1) {
    fprintf(stderr, "get_ntop_ip failed for client fd: %d\n", new_client_fd);
    strcpy(client_ip, "unknown");
  }

  char announcement[256];  // Buffer to hold the message
  snprintf(announcement, sizeof(announcement),
           "new client connecting from %s\n", client_ip);
  printf("%s", announcement);
  broadcast_to_clients(*fd_count, pfds, announcement, new_client_fd);

  return 0;
}

int process_existing_connection(int sender_fd, int* fd_count,
                                struct pollfd** pfds) {
  // char Bbuffer to recv data
  char buf[MAXDATASIZE];
  int nbytes = recv(sender_fd, buf, MAXDATASIZE, 0);
  switch (nbytes) {
    case 0: {
      // client disconnected early. handle!
      // handles POLLHUP || POLLERR
      char announcement[256];  // Buffer to hold the message
      snprintf(announcement, sizeof(announcement),
               "client %d has left the chat!\n", sender_fd);
      printf("%s", announcement);
      broadcast_to_clients(*fd_count, pfds, announcement, 0);
      remove_from_pfds(pfds, sender_fd, fd_count);
      close(sender_fd);
      return -1;
    }
    case -1: {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // not a real error for non-blocking sockets
        return 0;
      }
      fprintf(stderr, "process_existing_connection: %s\n", strerror(errno));
      remove_from_pfds(pfds, sender_fd, fd_count);
      close(sender_fd);
      return -1;
    }
    default: {
      buf[nbytes] = '\0';  // Null-terminate the received data
      broadcast_to_clients(*fd_count, pfds, buf, sender_fd);
      return 0;
    }
  }
}

int process_connections(int listener_fd, int* fd_count, struct pollfd** pfds) {
  // >>> 1. process a new client connection
  if ((*pfds)[0].revents & POLLIN) {
    if (process_new_connection(listener_fd, fd_count, pfds) != 0) {
      fprintf(stderr, "process_new_connection: %s\n", strerror(errno));
    }
  }

  // >>> 2. process existing connections
  for (int i = 1; i < *fd_count; i++) {
    if ((*pfds)[i].revents & POLLIN) {
      if (process_existing_connection((*pfds)[i].fd, fd_count, pfds) == -1) {
        i--;  // client removed. adjust index to check this position.
      }
    }
  }

  return 0;
}

int main() {
  int return_status = 0;

  int sockfd = -1;
  // setup array of fd's for poll() and add server to it
  int fd_size = MAXFDS;  // 1 listener, 5 clients
  int fd_count = 0;
  struct pollfd* pfds = malloc(sizeof(*pfds) * fd_size);

  sockfd = get_listener_socket();
  add_to_pfds(&pfds, sockfd, true, &fd_count);

  while (1) {
    int poll_count = poll(pfds, fd_count, -1);

    if (poll_count == -1) {
      fprintf(stderr, "poll: %s\n", strerror(errno));
      exit(1);
    }

    process_connections(sockfd, &fd_count, &pfds);
  }

  // cleanup:
  if (pfds) free(pfds);

  printf("\nClosing connection.\n");
  if (sockfd) close(sockfd);

  return return_status;
}
