#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(2, "usage: bgexec command [args...]\n");
    exit(1);
  }

  if(setbackground(1) < 0){
    fprintf(2, "bgexec: failed to mark process as background\n");
    exit(1);
  }

  exec(argv[1], &argv[1]);
  fprintf(2, "bgexec: exec %s failed\n", argv[1]);
  exit(1);
}
