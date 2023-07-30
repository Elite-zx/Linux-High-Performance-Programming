/* Author: Elite-ZX (Morris)*/

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

const int MAX_EVENT_NUMBER = 16;
const int MAX_CLIENT_NUMBER = 16;
const int PROCESS_LIMIT = 65536;
const int BUFFER_SIZE = 1024;
const int MAX_SIGNAL_CNT = 16;

typedef struct {
  sockaddr_in address;       // IP address of client
  int connfd;                // socket which handle this connection
  pid_t handler_pid;         // child process which process this connection
  int father_child_pipe[2];  // pipe between child(worker process) and
                             // father(main process)
} client_data;

int setnoblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option_with_no_blocking_io = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_option_with_no_blocking_io);
  return old_option;
}

// wrap epoll_ctl (with EPOLL_CTL_ADD)
void epoll_addfd(int epollfd, int new_fd) {
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
  int msg = sig;
  send(sig_pipe_fd[1], (char*)&msg, sizeof(msg), 0);
  errno = save_errno;
}

void addsig(int sig, void (*handler)(int), bool restart = true) {
  struct sigaction sa;
  memset(&sa, '\0', sizeof(sa));  // initialize to zero to avoid unknown value
  sa.sa_handler = handler;
  if (restart) sa.sa_flags |= SA_RESTART;
  // set sig_set to 1 , compare to sigemptyset which set sig_set to 0
  sigfillset(&sa.sa_mask);
  int ret = sigaction(sig, &sa, NULL);
  assert(ret != -1);
}

bool stop_child_process = false;
void child_terminate_handler(int sig) { stop_child_process = true; }

int run_child(int client_idx, client_data* users, char* shm) {
  epoll_event events[MAX_EVENT_NUMBER];
  // actually only two socket : connfd msg from client and pipe msg from father
  // process
  int child_epollfd = epoll_create(5);
  // if argument in assert is equal to zero, abort will be called
  assert(child_epollfd != -1);
  int connfd = users[client_idx].connfd;
  epoll_addfd(child_epollfd, connfd);
  int pipefd_to_father = users[client_idx].father_child_pipe[1];
  epoll_addfd(child_epollfd, pipefd_to_father);
  addsig(SIGTERM, child_terminate_handler, false);

  while (!stop_child_process) {
    int event_num = epoll_wait(child_epollfd, events, MAX_EVENT_NUMBER, -1);

    for (int i = 0; i < event_num; ++i) {
      int sockfd = events[i].data.fd;
      if (sockfd == connfd) {
        // data from client;
        memset(shm + client_idx * BUFFER_SIZE, '\0', BUFFER_SIZE);
        int ret =
            recv(connfd, shm + client_idx * BUFFER_SIZE, BUFFER_SIZE - 1, 0);
        if (ret <= 0) {
          stop_child_process = true;
        } else {
          // inform father process that data is coming
          send(pipefd_to_father, (char*)&client_idx, sizeof(client_idx), 0);
        }
      } else if (sockfd == pipefd_to_father) {
        int sending_client_idx = 0;
        int ret = recv(sockfd, (char*)&sending_client_idx,
                       sizeof(sending_client_idx), 0);
        if (ret <= 0) {
          stop_child_process = true;
        } else {
          // puts("send!");
          send(connfd, shm + BUFFER_SIZE * sending_client_idx, BUFFER_SIZE, 0);
        }
      } else {
        continue;
      }
    }
  }
  // stop child process
  close(connfd);
  close(pipefd_to_father);
  close(child_epollfd);
  return 0;
}

int main(int argc, char* argv[]) {
  if (argc <= 2) {
    printf("usage: %s ip_address port_number\n", basename(argv[0]));
    return 1;
  }
  const char* ip = argv[1];
  int port = atoi(argv[2]);

  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &address.sin_addr);
  address.sin_port = htons(port);

  int listenfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(listenfd >= 0);

  int ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
  assert(ret != -1);

  ret = listen(listenfd, 5);
  assert(ret != -1);

  int user_cnt = 0;
  client_data* users = new client_data[MAX_CLIENT_NUMBER];
  int* sub_process = new int[PROCESS_LIMIT];
  memset(sub_process, -1, PROCESS_LIMIT);

  // epoll IO multiplexing, actually listen for 1.listenfd 2.sig_pipe_fd[0] 3.
  // pipe from child
  epoll_event events[MAX_EVENT_NUMBER];
  int epollfd = epoll_create(5);
  assert(epollfd != -1);
  epoll_addfd(epollfd, listenfd);

  // unified events source
  socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipe_fd);
  assert(ret != -1);
  setnoblocking(sig_pipe_fd[1]);
  epoll_addfd(epollfd, sig_pipe_fd[0]);

  // add signal
  addsig(SIGCHLD, sig_handler);  // sub process exit
  addsig(SIGTERM, sig_handler);  // triggle by function kill
  addsig(SIGINT, sig_handler);   // interrupt
  // write to pipe or socket which read end is close, ignore
  addsig(SIGPIPE, SIG_IGN);
  bool stop_server = false;
  bool terminate = false;

  // create shared memory by shm_open and mmap
  // a posix shared-memory object  created by shm_open
  static const char* shm_name = "/elite_zx_shm";
  int shmfd = shm_open(shm_name, O_RDWR | O_CREAT, 0666);
  assert(shmfd != -1);
  ret = ftruncate(shmfd, BUFFER_SIZE * MAX_CLIENT_NUMBER);
  assert(ret != -1);
  // a long-term shared memory
  char* shm = (char*)mmap(NULL, MAX_CLIENT_NUMBER * BUFFER_SIZE,
                          PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
  assert(shm != MAP_FAILED);
  close(shmfd);

  while (!stop_server) {
    int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
    for (int i = 0; i < number; ++i) {
      int sockfd = events[i].data.fd;
      if (sockfd == listenfd) {
        // new client connection
        struct sockaddr_in client_address;
        socklen_t client_address_len = sizeof(client_address);
        int connfd = accept(listenfd, (struct sockaddr*)&client_address,
                            &client_address_len);
        if (connfd < 0) {
          printf("errno: %d\n", errno);
          continue;
        }
        if (user_cnt >= MAX_CLIENT_NUMBER) {
          const char* info = "too many users!\n";
          printf("%s", info);
          send(connfd, info, strlen(info), 0);
          close(connfd);
          continue;
        }
        users[user_cnt].address = client_address;
        users[user_cnt].connfd = connfd;
        ret = socketpair(PF_UNIX, SOCK_STREAM, 0,
                         users[user_cnt].father_child_pipe);
        assert(ret != -1);
        // create a sub-process to handle the new connection
        pid_t pid = fork();
        if (!pid) {
          close(epollfd);
          close(listenfd);
          close(users[user_cnt].father_child_pipe[0]);
          close(sig_pipe_fd[0]);
          close(sig_pipe_fd[1]);

          run_child(user_cnt, users, shm);
          // have question here
          munmap((void*)shm, MAX_CLIENT_NUMBER * BUFFER_SIZE);
          exit(0);
        } else {
          close(connfd);
          close(users[user_cnt].father_child_pipe[1]);
          epoll_addfd(epollfd, users[user_cnt].father_child_pipe[0]);

          users[user_cnt].handler_pid = pid;
          sub_process[pid] = user_cnt;
          ++user_cnt;
        }
      } else if (sockfd == sig_pipe_fd[0]) {
        // unified events source for signal
        char signals[MAX_SIGNAL_CNT];
        ret = recv(sig_pipe_fd[0], signals, sizeof(signals), 0);
        if (ret <= 0) {
          continue;
        } else {
          for (int i = 0; i < ret; ++i) {
            switch (signals[i]) {
              case SIGCHLD: {
                int pid;
                int stat_for_child;
                while ((pid = waitpid(-1, &stat_for_child, WNOHANG)) > 0) {
                  int del_client_idx = sub_process[pid];
                  epoll_ctl(epollfd, EPOLL_CTL_DEL,
                            users[del_client_idx].father_child_pipe[0], NULL);
                  close(users[del_client_idx].father_child_pipe[0]);
                  // Fill the gap with the last client information
                  users[del_client_idx] = users[--user_cnt];
                  sub_process[users[del_client_idx].handler_pid] =
                      del_client_idx;
                  printf("client %d exit, now we have %d users\n",
                         del_client_idx, user_cnt);
                }
                if (user_cnt == 0 && terminate) {
                  stop_server = true;
                }
                break;
                case SIGTERM:
                case SIGINT: {
                  // Ctrl + C
                  puts("kill all the child now!");
                  if (user_cnt == 0) {
                    stop_server = true;
                    break;
                  }
                  for (int i = 0; i < user_cnt; ++i) {
                    kill(users[i].handler_pid, SIGTERM);
                  }
                  terminate = true;  // stop_server in case SIGCHLD
                  break;
                }
                default:
                  break;
              }
            }
          }
        }
      } else {
        int child = 0;
        ret = recv(sockfd, (char*)&child, sizeof(child), 0);
        if (ret <= 0) {
          continue;
        } else {
          puts("send data to the child pipe!");
          for (int i = 0; i < user_cnt; ++i) {
            if (i == child) continue;
            send(users[i].father_child_pipe[0], (char*)&child, sizeof(child),
                 0);
          }
        }
      }
    }
  }
  close(sig_pipe_fd[0]);
  close(sig_pipe_fd[1]);
  close(listenfd);
  close(epollfd);
  shm_unlink(shm_name);
  delete[] users;
  delete[] sub_process;
  return 0;
}
