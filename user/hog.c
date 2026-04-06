#include "kernel/types.h"
#include "kernel/eco.h"
#include "user/user.h"

int
main(void)
{
  struct eco_stats stats;
  volatile unsigned long x = 0;

  printf("hog start\n");

  while(x < 900000000UL){
    x++;
  }

  if(getecostats(&stats) == 0){
    printf("hog stats: pid=%d cpu=%d sleep=%d run=%d wait=%d ctxsw=%d sched=%d eco=%d\n",
           stats.pid,
           stats.cpu_ticks,
           stats.sleep_ticks,
           stats.runnable_ticks,
           stats.waiting_tick,
           stats.context_switches,
           stats.times_scheduled,
           stats.eco_score);
  } else {
    printf("hog stats: unavailable\n");
  }

  printf("hog done\n");
  exit(0);
}
