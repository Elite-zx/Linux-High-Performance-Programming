#include <fcntl.h>
#include <stdio.h>

int setnonblocking(int fd) {
  int old_flags = fcntl(fd, F_GETFL);
  int net_flags = old_flags | O_NONBLOCK;
  fcntl(fd, F_SETFL, net_flags);
  return old_flags;
}
