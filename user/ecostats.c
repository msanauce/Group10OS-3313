#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/eco.h"
#include "user/user.h"

int
main(void)
{
  struct eco_stats stats;

  if(getecostats(&stats) < 0){
    printf("ecostats: getecostats failed\n");
    exit(1);
  }

  printf("pid %d\n", stats.pid);
  printf("cpu_quota %d\n", stats.cpu_quota);
  printf("cpu_used_in_window %d\n", stats.cpu_used_in_window);
  printf("throttled %d\n", stats.throttled);
  printf("quota_violations %d\n", stats.quota_violations);
  printf("waiting_tick %d\n", stats.waiting_tick);
  printf("context_switches %d\n", stats.context_switches);
  printf("stretched_sleep_calls %d\n", stats.stretched_sleep_calls);
  printf("total_extra_sleep_ticks %d\n", stats.total_extra_sleep_ticks);

  exit(0);
}
