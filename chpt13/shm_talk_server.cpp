#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  sockaddr_in address;       // ip address of client
  int connfd;                // socket which handle this connection
  pid_t handler_pid;         // child process which process this connection
  int father_child_pipe[2];  // pipe between child(worker process) and
                             // father(main process)
} client_data;

int setnoblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option_with_no_blocking_io = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL);
  return old_option;
}

// wrap epoll_ctl (with EPOLL_CTL_ADD)
void addfd(int epollfd, int new_fd) {
  epoll_event event_type;
  event_type.data.fd = new_fd;
  event_type.events = EPOLLIN | EPOLLET;
  epoll_ctl(epollfd, EPOLL_CTL_ADD, new_fd, &event_type);
  setnoblocking(new_fd);
}

// unified event source
int sig_pipe_fd[2];
void sig_handler(int sig) {
  // save errno for function reentrance
  auto save_errno = errno;
  send(sig_pipe_fd[1], &sig, 1, 0);
  errno = save_errno;
}
