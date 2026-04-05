#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/eco.h"
#include "user/user.h"

int
main(void)
{
  int i;

  printf("sleepdemo: mode=%d\n", getecomode());
  for(i = 0; i < 5; i++){
    printf("sleepdemo: iteration %d before pause(5)\n", i + 1);
    pause(5);
    printf("sleepdemo: iteration %d after wake\n", i + 1);
  }

  exit(0);
}
