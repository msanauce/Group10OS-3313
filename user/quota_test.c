#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int q = 10;

  if(argc > 1)
    q = atoi(argv[1]);

  if(setquota(q) < 0){
    printf("setquota failed\n");
    exit(1);
  }

  printf("quota_test: quota set to %d\n", q);

  while(1){
    // busy loop
  }

  exit(0);
}