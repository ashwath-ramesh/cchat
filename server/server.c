// program: cchat/server/server.c
// TODO:
// [] dynamic arrays sizing
// [x] hash table for storing fd info (index in array, nicknames, etc)
// [] get nicknames when client joins
// [] ring buffer for per client send queue (backpressure handling)
// [] testing multiple clients connecting and sending messages
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

#include "uthash.h"
#include "utils.h"

#define HOSTNAME "localhost"
#define PORT "3490"
#define MAXDATASIZE 256 // max number of bytes
#define MAXFDS 6        // 1 listener, n clients

struct fdmap {
  int fd;        // key
  int idx;       // indx in array of fd's
  char nick[11]; // chat nickname
  UT_hash_handle hh;
};

/** add fd to poll array & hash map
 * @param fds poll fd array
 * @param usrs fd->user hash map
 * @param addfd fd to add
 * @param islfd true=listener, false=client
 * @param nfd fd count (incremented)
 * @return 0 ok, -1 fail */
int fdadd(struct pollfd **fds, struct fdmap **usrs, int addfd, bool islfd,
          int *nfd) {
  struct fdmap *s;
  s = malloc(sizeof(*s));
  if (!s)
    return -1;

  if (islfd == true) {
    // add server to fdmap
    s->fd = addfd;
    s->idx = 0;
    strcpy(s->nick, "srvr");
    HASH_ADD_INT(*usrs, fd, s);

    // add server fd to array
    (*fds)[0].fd = addfd;
    (*fds)[0].events = POLLIN | POLLERR;
    (*fds)[0].revents = 0;
  } else {
    // add client to fdmap
    s->fd = addfd;
    s->idx = *nfd;
    strcpy(s->nick, "guest");
    HASH_ADD_INT(*usrs, fd, s);

    // add client to fd array
    (*fds)[*nfd].fd = addfd;
    (*fds)[*nfd].events = POLLIN | POLLHUP | POLLERR;
    (*fds)[*nfd].revents = 0;
  }

  (*nfd)++;
  return 0;
}

/** remove fd from poll array & hash map (swap-with-last O(1))
 * @param fds poll fd array
 * @param usrs fd->user hash map
 * @param rmfd fd to remove
 * @param nfd fd count (decremented)
 * @return 0 ok, -1 fail/not found */
int fdrm(struct pollfd **fds, struct fdmap **usrs, int rmfd, int *nfd) {
  if ((*fds)[0].fd == rmfd) {
    // if the fd to remove is the listener fd, do nothing
    // and exit
    return -1;
  }

  // removal tasks -
  // (a) find fd in index
  // (b) remove fd using either: (i) compact-shift array O(n) (ii)
  // swap-with-last removal O(1) (c) update fdmap usr with latest index for fd's

  // compact-shift array
  // for (int i = 1; i < *fdcnt; i++) {
  //   if ((*fds)[i].fd == rmfd) {
  //     for (int j = i; j < *fdcnt - 1; j++) {
  //       (*fds)[j] = (*fds)[j + 1];
  //     }
  //     (*fdcnt)--;
  //     break;
  //   }
  // }

  // lookup index from fdmap usr
  struct fdmap *srem, *slast;
  HASH_FIND_INT(*usrs, &rmfd, srem);
  if (!srem)
    return -1;
  HASH_FIND_INT(*usrs, &(*fds)[*nfd - 1].fd, slast);
  // do swap-with-last O(1) removal
  (*fds)[srem->idx] = (*fds)[*nfd - 1];
  // update fdmap usr with new index of ex-last element
  if (srem != slast)
    slast->idx = srem->idx;

  HASH_DEL(*usrs, srem);
  free(srem);
  (*nfd)--;

  return 0;
}

/** format msg with sender fd prefix
 * @param sfd sender fd
 * @param msg raw message
 * @return malloc'd "fd: msg" string or NULL */
static char *fmtmsg(int sfd, const char *msg) {
  int need = snprintf(NULL, 0, "%d: %s", sfd, msg);
  if (need < 0)
    return NULL;

  char *out = malloc(need + 1);
  if (!out)
    return NULL;

  if (snprintf(out, need + 1, "%d: %s", sfd, msg) < 0) {
    free(out);
    return NULL;
  }

  return out;
}

/** broadcast msg to all clients except sender
 * @param nfd fd count
 * @param fds poll fd array
 * @param msg message to send
 * @param sfd sender fd (skipped)
 * @return 0 ok, -1 fail */
int bcast(int nfd, struct pollfd **fds, const char *msg, int sfd) {
  char *buf = fmtmsg(sfd, msg);
  if (!buf)
    return -1;

  int mlen = strlen(buf); // message len

  // build target list once (excl. listener & sender fd)
  // Sized for all clients (sender excluded in loop: -1 if listener, -2 if
  // client). Worst case -1.
  int tgtfds[MAXFDS - 1];
  int tgtidx = 0;
  for (int i = 1; i < nfd; i++) { // start at 1 to excl lfd
    if ((*fds)[i].fd != sfd) {    // skip sender fd
      tgtfds[tgtidx] = (*fds)[i].fd;
      tgtidx++;
    }
  }

  // batch send to all targets
  for (int i = 0; i < tgtidx; i++) {
    int nsent = 0;

    while (nsent < mlen) {
      int n = send(tgtfds[i], buf + nsent, mlen - nsent, 0);

      if (n == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          // Buffer full, retry (could add a small delay or use poll)
          continue;
        }
        // Permanent error (EPIPE, ECONNRESET, etc.)
        fprintf(stderr, "bcast err | fd %d: %s\n", tgtfds[i], strerror(errno));
        break; // Give up on this client, move to next
      }
      nsent += n;
    }
  }

  free(buf);
  return 0;
}

/** extract IP string from sockaddr (v4/v6)
 * @param raddr client socket address
 * @param ipstr output buffer [INET6_ADDRSTRLEN]
 * @return 0 ok, -1 unknown family */
int ipstr(struct sockaddr_storage raddr, char ipstr[INET6_ADDRSTRLEN]) {
  void *addr = NULL;

  switch (raddr.ss_family) {
  case AF_INET:
    addr = &(((struct sockaddr_in *)&raddr)->sin_addr);
    break;
  case AF_INET6:
    addr = &(((struct sockaddr_in6 *)&raddr)->sin6_addr);
    break;
  default:
    fprintf(stderr, "ipstr: unknown address family %d\n", raddr.ss_family);
    return -1;
  }
  inet_ntop(raddr.ss_family, addr, ipstr, INET6_ADDRSTRLEN);

  return 0;
}

/** handle new client connection (accept, add to poll, broadcast join)
 * @param lsock listener socket
 * @param nfd fd count
 * @param fds poll fd array
 * @param usrs fd->user hash map
 * @return 0 ok, -1 fail/capacity */
int newcon(int lsock, int *nfd, struct pollfd **fds, struct fdmap **usrs) {
  struct sockaddr_storage caddr; // new remote client address
  socklen_t caddrlen;            // new client address len
  int cfd;                       // new client fd

  // Accept new connection on listener fd with error checking
  caddrlen = sizeof(caddr);
  cfd = accept(lsock, (struct sockaddr *)&caddr, &caddrlen);
  if (cfd == -1) {
    fprintf(stderr, "accept: %s\n", strerror(errno));
    return -1;
  }

  // set as non-blocking
  fcntl(cfd, F_SETFL, O_NONBLOCK);

  // Validate if pfds array has space. If not,
  // reject new client with a msg
  if (*nfd >= MAXFDS) {
    char msg[] = "server at capacity. please try again later.\n";
    send(cfd, msg, strlen(msg), 0);
    // printf("%s", msg);
    close(cfd);
    return -1;
  }

  // Add new client fd to the pfds array
  fdadd(fds, usrs, cfd, false, nfd);

  // broadcast new client info to chat group
  char cip[INET6_ADDRSTRLEN]; // client ip
  if (ipstr(caddr, cip) == -1) {
    fprintf(stderr, "ipstr failed for client: %d\n", cfd);
    strcpy(cip, "unknown");
  }

  char msg[256]; // Buffer to hold the message
  snprintf(msg, sizeof(msg), "new client connecting from %s\n", cip);
  printf("%s", msg);
  bcast(*nfd, fds, msg, cfd);

  return 0;
}

/** handle existing client I/O (recv msg, broadcast, or handle disconnect)
 * @param sfd client socket fd
 * @param nfd fd count
 * @param pfds poll fd array
 * @param usrs fd->user hash map
 * @return 0 ok, -1 disconnect/error */
int extcon(int sfd, int *nfd, struct pollfd **pfds, struct fdmap **usrs) {
  char buf[MAXDATASIZE]; // buffer to recv data
  int n = recv(sfd, buf, MAXDATASIZE, 0);
  switch (n) {
  case 0: {
    // client disconnected early. handle!
    // handles POLLHUP || POLLERR
    char msg[256]; // Buffer to hold the message
    snprintf(msg, sizeof(msg), "client %d has left the chat!\n", sfd);
    printf("%s", msg);
    bcast(*nfd, pfds, msg, 0);
    fdrm(pfds, usrs, sfd, nfd);
    close(sfd);
    return -1;
  }
  case -1: {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // not a real error for non-blocking sockets
      return 0;
    }
    fprintf(stderr, "extcon: %s\n", strerror(errno));
    fdrm(pfds, usrs, sfd, nfd);
    close(sfd);
    return -1;
  }
  default: {
    buf[n] = '\0'; // Null-terminate the received data
    bcast(*nfd, pfds, buf, sfd);
    return 0;
  }
  }
}

/** process poll events (new connections + client I/O)
 * @param lfd listener fd
 * @param nfd fd count
 * @param pfds poll fd array
 * @param usrs fd->user hash map
 * @return 0 ok */
int proc(int lfd, int *nfd, struct pollfd **pfds, struct fdmap **usrs) {
  // >>> 1. process a new client connection
  if ((*pfds)[0].revents & POLLIN) {
    if (newcon(lfd, nfd, pfds, usrs) != 0) {
      fprintf(stderr, "newcon: %s\n", strerror(errno));
    }
  }

  // >>> 2. process existing connections
  for (int i = 1; i < *nfd; i++) {
    if ((*pfds)[i].revents & POLLIN) {
      if (extcon((*pfds)[i].fd, nfd, pfds, usrs) == -1) {
        i--; // client removed. adjust index to check this position.
      }
    }
  }

  return 0;
}

int main() {
  int rstat = 0;

  int lsock = -1;

  // setup array of fd's for poll() and add server to it
  int sz = MAXFDS; // 1 listener, 5 clients
  int cnt = 0;     // current count
  struct pollfd *fds = malloc(sizeof(*fds) * sz);
  if (!fds)
    return -1;

  struct fdmap *users = NULL;

  if (lstnfd(HOSTNAME, PORT, true, &lsock) == -1) {
    fprintf(stderr, "lstnfd: %s\n", strerror(errno));
    return -1;
  }
  fdadd(&fds, &users, lsock, true, &cnt);

  while (1) {
    if (poll(fds, cnt, -1) == -1) // fd's ready for IO
    {
      fprintf(stderr, "poll: %s\n", strerror(errno));
      exit(1);
    }

    proc(lsock, &cnt, &fds, &users);
  }

  // cleanup
  if (fds)
    free(fds);

  printf("\nClosing connection.\n");
  if (lsock)
    close(lsock);

  return rstat;
}
