#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int mode;

  if(argc != 2){
    printf("usage: setecomode mode\n");
    printf("current mode: %d\n", getecomode());
    exit(1);
  }

  mode = atoi(argv[1]);
  if(setecomode(mode) < 0){
    printf("setecomode: invalid mode %d\n", mode);
    exit(1);
  }

  printf("eco mode set to %d\n", getecomode());
  exit(0);
}
