#include <stdio.h>
#include <stdlib.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>

/* fourth parameter for system call semctl*/
union semun {
  int val;
  struct semid_ds* buf;
  unsigned short int* array;
  struct seminfo* __buf;
};

/* modify semaphore by using system call semop*/
void pv(int sem_id, int op) {
  /*second parameter in system call semop with type sembuf* Specify the type of
   * operation*/
  struct sembuf sem_b;
  sem_b.sem_num = 0;
  sem_b.sem_op = op;
  sem_b.sem_flg = SEM_UNDO;
  semop(sem_id, &sem_b, 1);
}

int main(int argc, char* argv[]) {
  /*create a semaphore set whether it exists or not
   * (with special key IPC_PRIVATE)*/
  int sem_id = semget(IPC_PRIVATE, 1, 0666);

  union semun sem_un;
  sem_un.val = 1;
  /*for using SETVAL command, set value of semaphore(semval) to semun.val*/
  semctl(sem_id, 0, SETVAL, sem_un);

  /*multi process with semaphore*/
  pid_t id = fork();
  if (id < 0) {
    return 1;
  } else if (id == 0) {
    printf("child try to get binary sem\n");
    /*sem_op <0, execute the P operation*/
    pv(sem_id, -1);
    printf("child get the sem and would release it after 5 seconds\n");
    sleep(5);
    /*sem_op > 0, execute the V operation*/
    pv(sem_id, 1);
    exit(0);
  } else {
    printf("parent try to get binary sem\n");
    pv(sem_id, -1);
    printf("parent get the sem and would release it after 5 seconds\n");
    sleep(5);
    pv(sem_id, 1);
  }

  /*avoid zombie child process*/
  waitpid(id, NULL, 0);
  /*command IPC_RMID: remove semaphore set and wake up all processes waiting for
   * these semaphore*/
  semctl(sem_id, 0, IPC_RMID, sem_un);
  return 0;
}

