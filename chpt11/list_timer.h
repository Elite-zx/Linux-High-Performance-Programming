#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>

#define BUFFER_SIZE 64
class util_timer;
struct client_data {
  sockaddr_in address;
  int sockfd;
  char buf[BUFFER_SIZE];
  util_timer* timer;
};

/*timer*/
class util_timer {
 public:
  util_timer() : prev(NULL), next(NULL) {}

 public:
  time_t expire;                  // timeout value
  void (*cb_func)(client_data*);  // callback function
  client_data* user_data;         // note: a pointer
  util_timer* prev;               // prev timer pointer
  util_timer* next;               // next timer pointer
};

/*timer container*/
class sort_timer_lst {
 public:
  sort_timer_lst() : head(NULL), tail(NULL) {}
  ~sort_timer_lst() {
    util_timer* tmp = head;
    while (tmp) {
      head = tmp->next;
      delete tmp;
      tmp = head;
    }
  }
  void add_timer(util_timer* timer) {
    /*keep ascending*/
    if (!timer) {
      return;
    }
    if (!head) {
      head = tail = timer;
      return;
    }
    if (timer->expire < head->expire) {
      timer->next = head;
      head->prev = timer;
      head = timer;
      return;
    }
    add_timer(timer, head);
  }

  void adjust_timer(util_timer* timer) {
    if (!timer) {
      return;
    }
    /*no need to move backward*/
    util_timer* tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire)) {
      return;
    }

    if (timer == head) {
      /*is head node, reinsert*/
      head = head->next;
      head->prev = NULL;
      timer->next = NULL;
      add_timer(timer, head);
    } else {
      /*remove from linked list and reinsert*/
      timer->prev->next = timer->next;
      timer->next->prev = timer->prev;
      add_timer(timer, timer->next);
    }
  }
  void del_timer(util_timer* timer) {
    if (!timer) {
      return;
    }
    if ((timer == head) && (timer == tail)) {
      delete timer;
      head = NULL;
      tail = NULL;
      return;:
    }
    if (timer == head) {
      head = head->next;
      head->prev = NULL;
      delete timer;
      return;
    }
    if (timer == tail) {
      tail = tail->prev;
      tail->next = NULL;
      delete timer;
      return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
  }

  /*this function executed once after every fixed period*/
  /*handle timeout event*/
  void tick() {
    if (!head) {
      return;
    }
    printf("timer tick\n");
    time_t cur = time(NULL);
    util_timer* tmp = head;
    while (tmp) {
      if (cur < tmp->expire) {
        break;
      }
      tmp->cb_func(tmp->user_data);
      head = tmp->next;
      if (head) {
        head->prev = NULL;
      }
      delete tmp;
      tmp = head;
    }
  }

 private:
  void add_timer(util_timer* timer, util_timer* lst_head) {
    util_timer* prev = lst_head;
    util_timer* tmp = prev->next;
    /*find proper position to insert*/
    while (tmp) {
      if (timer->expire < tmp->expire) {
        prev->next = timer;
        timer->next = tmp;
        tmp->prev = timer;
        timer->prev = prev;
        break;
      }
      prev = tmp;
      tmp = tmp->next;
    }
    if (!tmp) {
      prev->next = timer;
      timer->prev = prev;
      timer->next = NULL;
      tail = timer;
    }
  }

 private:
  /*Doubly linked list*/
  util_timer* head;
  util_timer* tail;
};

#endif
