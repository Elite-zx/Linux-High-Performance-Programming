#include <stdio.h>
#include <unistd.h>

#include "switch-user.h"

int main() {
  uid_t uid = getuid();
  uid_t euid = geteuid();
  printf("userid is: %d \t effective userid is: %d\n", uid, euid);

  /*switch user to 1000*/
  switch_to_user(1000, 1000);
  uid = getuid();
  euid = geteuid();
  printf("userid is: %d \t effective userid is: %d\n", uid, euid);
  return 0;
}
