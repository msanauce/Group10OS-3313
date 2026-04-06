#include "kernel/types.h"
#include "kernel/eco.h"
#include "user/user.h"

static void
cpu_burst(int ticks_to_run)
{
  int start = uptime();

  while(uptime() - start < ticks_to_run){
    // Busy-wait to simulate a short CPU burst.
  }
}

int
main(void)
{
  struct eco_stats stats;
  int i;

  for(i = 0; i < 8; i++){
    cpu_burst(1);
    pause(6);
  }

  if(getecostats(&stats) == 0){
    printf("burst stats: pid=%d cpu=%d sleep=%d run=%d wait=%d ctxsw=%d sched=%d eco=%d\n",
           stats.pid,
           stats.cpu_ticks,
           stats.sleep_ticks,
           stats.runnable_ticks,
           stats.waiting_tick,
           stats.context_switches,
           stats.times_scheduled,
           stats.eco_score);
  } else {
    printf("burst stats: unavailable\n");
  }

  printf("burst done\n");
  exit(0);
}
