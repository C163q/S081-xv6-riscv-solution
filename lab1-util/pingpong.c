#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int ping[2], pong[2];
  pipe(ping);
  pipe(pong);
  if (fork() == 0) {  // child
    char buf[1];
    close(pong[0]);
    close(ping[1]);
    read(ping[0], buf, 1);
    fprintf(1, "%d: received ping\n", getpid());
    close(ping[0]);
    write(pong[1], "\0", 1);
    close(pong[1]);
  } else {  // parent
    char buf[1];
    close(ping[0]);
    close(pong[1]);
    write(ping[1], "\0", 1);
    close(ping[1]);
    read(pong[0], buf, 1);
    fprintf(1, "%d: received pong\n", getpid());
    close(pong[0]);
  }
  exit(0);
}
