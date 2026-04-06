#include "kernel/types.h"
#include "user/user.h"

static void
run_child(char *prog)
{
  char *argv[] = { prog, 0 };

  if(fork() == 0){
    exec(prog, argv);
    fprintf(2, "exec %s failed\n", prog);
    exit(1);
  }
}

int
main(void)
{
  int status;

  run_child("hog");
  // Give the CPU-bound workload a short head start so the scheduler
  // has a clearer contention pattern once burst joins.
  pause(2);
  run_child("burst");

  wait(&status);
  wait(&status);
  exit(0);
}
