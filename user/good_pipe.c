#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  int p[2];

  // pipe() creates two file descriptors:
  // p[0] = read end, p[1] = write end
  if(pipe(p) < 0){
    fprintf(2, "pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    fprintf(2, "fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // child reads from the pipe
    close(p[1]);                 // child does not write

    char buf[64];
    int n;
    while((n = read(p[0], buf, sizeof(buf))) > 0){
      write(1, buf, n);          // write to stdout (fd 1)
    }

    close(p[0]);
    exit(0);
  } else {
    // parent writes to the pipe
    close(p[0]);                 // parent does not read

    char *haiku =
      "This is my Haiku\n"
      "I am writing poetry\n"
      "This is my good pipe\n";

    write(p[1], haiku, strlen(haiku));
    close(p[1]);                 // closing write end causes reader to see EOF

    wait(0);
    exit(0);
  }
}
