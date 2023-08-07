#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>

class process {
 public:
  process() : m_pid(-1) {}
  pid_t m_pid;
  int m_pipe_fd[2];
};

template <typename T>
class processPool {
 private:
  // singleton mode
  processPool(int listenfd, int process_number = 8);
  static processPool* m_instance;

 public:
  // inline function
  processPool<T>* get_instance(int listenfd, int process_number = 8) {
    if (!m_instance) {
      m_instance = new processPool<T>(listenfd, process_number);
    }
    return m_instance;
  }
  ~processPool() { delete[] m_sub_process_info; }
  void run();

 private:
  void setup_sig_pipe();
  void run_parent();
  void run_child();

  // in-class initializer (page 64 in c++ primer)
  static const int MAX_PROCESS_NUMBER = 16;
  static const int USER_PER_PROCESS = 65536;
  static const int MAX_EVENT_NUMBER = 10000;
  static const int MAX_SIGNAL_NUMBER = 64;
  int m_process_number;
  // mark a process in process pool
  int m_idx;
  // epoll routine in main process
  int m_epollfd;
  int m_listenfd;
  // stop processPool distributing or not
  int m_stop;
  // store process info
  process* m_sub_process_info;
};
processPool<T>::m_instance = nullptr;

// non-class function

static int setnonblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}

static void addfd(int epollfd, int fd) {
  epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLET;
  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

static void removefd(int epollfd, int fd) {
  epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
  close(fd);
}

// unified event source
static int sig_pipe_fd[2];
static void sig_handler(int sig) {
  int save_errno = errno;
  int msg = sig;
  send(sig_pipe_fd[1], (char*)&msg, 1, 0);
  errno = save_errno;
}

static void addsig(int sig, void(handler)(int), bool restart = true) {
  struct sigaction sa;
  memset(&sa, '\0', sizeof(sa));
  sa.sa_handler = handler;
  if (restart) {
    sa.sa_flags |= SA_RESTART;
  }
  sigfillset(&sa.sa_mask);
  assert(sigaction(sig, &sa, NULL) != -1);
}

// function member
template <typename T>
void processPool<T>::setup_sig_pipe() {
  m_epollfd = epoll_create(5);
  assert(m_epollfd != -1);

  int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipe_fd);
  assert(ret != -1);

  setnonblocking(sig_pipe_fd[1]);
  addfd(m_epollfd, sig_pipe_fd[0]);

  addsig(SIGCHLD, sig_handler);
  addsig(SIGTERM, sig_handler);
  addsig(SIGINT, sig_handler);
  addsig(SIGPIPE, SIG_IGN);
}

template <typename T>
processPool<T>::processPool(int listenfd, int process_number)
    : m_listenfd(listenfd),
      m_process_number(process_number),
      m_idx(-1),
      m_stop(false) {
  assert(process_number > 0 && process_number <= MAX_PROCESS_NUMBER);
  m_sub_process_info = new process[process_number];
  assert(m_sub_process_info != nullptr);

  // create process_number processes
  for (int i = 0; i < process_number; ++i) {
    socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process_info[i].m_pipe_fd);
    m_sub_process_info[i].m_pid = fork();
    if (m_sub_process_info[i].m_pid > 0) {
      close(m_sub_process_info[i].m_pipe_fd[1]);
      continue;
    } else {
      m_idx = i;
      close(m_sub_process_info[i].m_pipe_fd[0]);
      break;
    }
  }
}

template <typename T>
void processPool<T>::run() {
  m_idx == -1 ? run_parent() : run_child();
}

template <typename T>
void processPool<T>::run_child() {
  int pipe_from_parent = m_sub_process_info[m_idx].m_pipe_fd[1];
  addfd(m_epollfd, pipe_from_parent);

  epoll_event events[MAX_EVENT_NUMBER];
  T* users = new T[USER_PER_PROCESS];

  int event_cnt;
  while (!m_stop) {
    event_cnt = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
    if ((event_cnt < 0) && (errno != EINTR)) {
      printf("epoll failure\n");
      break;
    }

    for (int i = 0; i < event_cnt; ++i) {
      int sockfd = events[i].data.fd;
      uint32_t event_type = events[i].events;
      // new client from main process
      if (sockfd == pipe_from_parent && event_type == EPOLLIN) {
        // client value is pointless
        int client = 0;
        int ret = recv(pipe_from_parent, (char*)&client, sizeof(client), 0);
        if ((ret < 0 && errno == EAGAIN) || ret == 0) {
          continue;
        } else {
          // accept new connection in subprocess
          struct sockaddr_in client_addr;
          socklen_t client_addr_len;
          int connfd = accept(m_listenfd, (struct sockaddr*)&client_addr,
                              &client_addr_len);
          addfd(m_epollfd, connfd);
          // initialize connection ?
          users[connfd].init();
        }
      }
      // handle signals
      else if (sockfd == sig_pipe_fd[0] && event_type == EPOLLIN) {
        int signals[MAX_SIGNAL_NUMBER];
        int ret = recv(sig_pipe_fd[0], (char*)signals, sizeof(signals), 0);
        if ((ret < 0 && errno == EAGAIN) || ret == 0) {
          continue;
        } else {
          for (int i = 0; i < ret; i++) {
            switch (signals[i]) {
              case SIGCHLD: {
                // avoid zombie sub-process
                int stat;
                pid_t exited_child_pid;
                while ((exited_child_pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                  // do nothing
                  continue;
                }
                break;
              }
              case SIGTERM:
              case SIGINT: {
                m_stop = true;
                break;
              }
              default: {
                break;
              }
            }
          }
        }
      } else if (event_type == EPOLLIN) {
        // data from client
        users[sockfd].process();
      } else {
        continue;
      }
    }
  }
  delete[] users;
  users = nullptr;
  close(pipe_from_parent);
  close(m_listenfd);
  close(m_epollfd);
}

template <typename T>
void processPool<T>::run_parent() {
  setup_sig_pipe();

  addfd(m_epollfd, m_listenfd);
  epoll_event events[MAX_EVENT_NUMBER];

  int sub_process_cnt{0};
  while (!m_stop) {
    int event_cnt = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
    for (int i = 0; i < event_cnt; ++i) {
      int sockfd = events[i].data.fd;
      uint32_t event_type = events[i].events;
      if (sockfd == m_listenfd && event_type == EPOLLIN) {
        // new client is coming
        int round_robin{sub_process_cnt};
        while (m_sub_process_info[round_robin].m_pid == -1) {
          round_robin = (round_robin + 1) % m_process_number;
          if (round_robin == sub_process_cnt) {
            // no available process
            m_stop = true;
            break;
          }
          sub_process_cnt = (round_robin + 1) % m_process_number;
          puts("send request to child in process pool!");
          int new_client = 1;
          send(m_sub_process_info[round_robin].m_pipe_fd[0], (char*)&new_client,
               sizeof(new_client), 0);
        }
      } else if (sockfd == sig_pipe_fd[0] && event_type == EPOLLIN) {
        int signals[MAX_SIGNAL_NUMBER];
        int ret = recv(sig_pipe_fd[0], (char*)signals, sizeof(signals), 0);
        if ((ret < 0 && errno == EAGAIN) || ret == 0) {
          continue;
        } else {
          for (int i = 0; i < ret; i++) {
            switch (signals[i]) {
              case SIGCHLD: {
                // avoid zombie sub-process
                int stat;
                pid_t exited_child_pid;
                int exited_child_cnt{0};
                while ((exited_child_pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                  for (int i = 0; i < m_process_number; ++i) {
                    if (m_sub_process_info[i].m_pid == exited_child_pid) {
                      printf("child %s exit!\n", i);
                      m_sub_process_info[i].m_pid = -1;
                      close(m_sub_process_info[i].m_pipe_fd[0]);
                      ++exited_child_cnt;
                    }
                  }
                }
                if (exited_child_cnt == m_process_number) m_stop = true;
                break;
              }
              case SIGTERM:
              case SIGINT: {
                puts("terminate all the child!");
                for (int i = 0; i < m_process_number; ++i) {
                  if (m_sub_process_info[i].m_pid != -1)
                    kill(m_sub_process_info[i].m_pid, SIGTERM);
                }
                break;
              }
              default: {
                break;
              }
            }
          }
        }
      } else {
        continue;
      }
    }
  }
  close(m_epollfd);
  close(m_listenfd);
}
#endif

