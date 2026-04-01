#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    printf("Usage: ps [-o | -l]\n");
    exit(1);
  }

  if (kps(argv[1]) < 0) {
    exit(1);
  }

  exit(0);
}
