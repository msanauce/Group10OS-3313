#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/eco.h"
#include "user/user.h"

static void
print_stats(struct eco_idle_stats *stats)
{
  int capacity_ticks = stats->uptime_ticks * stats->cpus_tracked;
  int busy_ticks = capacity_ticks - stats->total_idle_ticks;
  int idle_pct = 0;

  if(capacity_ticks > 0)
    idle_pct = (stats->total_idle_ticks * 100) / capacity_ticks;

  printf("eco idle stats\n");
  printf("uptime_ticks %d\n", stats->uptime_ticks);
  printf("cpus_tracked %d\n", stats->cpus_tracked);
  printf("current_idle_cpus %d\n", stats->current_idle_cpus);
  printf("idle_intervals %d\n", stats->idle_intervals);
  printf("total_idle_ticks %d\n", stats->total_idle_ticks);
  printf("longest_idle_streak %d\n", stats->longest_idle_streak);
  printf("estimated_busy_ticks %d\n", busy_ticks);
  printf("idle_capacity_percent %d\n", idle_pct);
}

int
main(int argc, char *argv[])
{
  struct eco_idle_stats stats;

  if(argc == 2 && strcmp(argv[1], "reset") == 0){
    if(resetecoidle() < 0){
      fprintf(2, "ecoidle: reset failed\n");
      exit(1);
    }
    printf("ecoidle: counters reset\n");
    exit(0);
  }

  if(argc != 1){
    fprintf(2, "usage: ecoidle [reset]\n");
    exit(1);
  }

  if(getecoidlestats(&stats) < 0){
    fprintf(2, "ecoidle: getecoidlestats failed\n");
    exit(1);
  }

  print_stats(&stats);
  exit(0);
}
