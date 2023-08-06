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
    m_instance = new processPool<T>(listenfd, process_number);
    return m_instance;
  }
  ~processPool() { delete[] m_sub_process; }
  void run();

 private:
  void setup_sig_pipe;
  void run_parent();
  void run_child();

  static const int MAX_PROCESS_NUMBER = 16;
  static const int USER_PER_PROCESS = 65536;
  static const int MAX_EVENT_NUMBER = 10000;
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

processPool<T>::processPool(int listenfd, int process_number = 8)
    : m_listenfd(listenfd),
      m_process_number(process_number),
      m_idx(-1),
      m_stop(false) {
  assert(process_number > 0 && process_number <= MAX_PROCESS_NUMBER);
  m_sub_process_info = new process(process_number);
  assert(m_sub_process_info != nullptr);

  // create process_number processes
  for (int i = 0; i < process_number; ++i) {
    socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process_info[i].m_pipe_fd);
    m_sub_process_info[i].m_pid = fork();
    if (m_sub_process_info[i].m_pid > 0) {
      close(m_sub_process_info.m_pipe_fd[1]);
      continue;
    } else {
      m_idx = i;
      close(m_sub_process_info.m_pipe_fd[0]);
      break;
    }
  }
}
