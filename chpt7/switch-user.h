#include <stdio.h>
#include <unistd.h>

static bool switch_to_user(uid_t user_id, gid_t gp_id) {
  /*make sure target user is not root*/
  if ((user_id == 0) && (gp_id == 0)) {
    return false;
  }

  /*make sure current user is legal: root or target user*/
  gid_t gid = getgid();
  uid_t uid = getuid();
  if (((gid != 0) || (uid != 0)) && ((gid != gp_id) || (uid != user_id))) {
    return false;
  }

  if (uid != 0) {
    return true;
  }

  /*switch root to target user*/
  if ((setgid(gp_id) < 0) || (setuid(user_id) < 0)) {
    return false;
  }

  return true;
}
