#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/eco.h"
#include "user/user.h"

static void
burn_cpu_for_ticks(int duration_ticks)
{
  int start = uptime();

  while(uptime() - start < duration_ticks){
    // Keep the process busy so the kernel has to decide whether to defer it.
  }
}

int
main(int argc, char *argv[])
{
  int duration_ticks = 60;
  int demo_quota;
  struct eco_stats stats;

  if(argc > 2){
    fprintf(2, "usage: bgburn [ticks]\n");
    exit(1);
  }

  if(argc == 2){
    duration_ticks = atoi(argv[1]);
    if(duration_ticks <= 0){
      fprintf(2, "bgburn: ticks must be > 0\n");
      exit(1);
    }
  }

  // Keep the background demo responsive even if quota mode is still enabled.
  demo_quota = duration_ticks + 100;
  if(setquota(demo_quota) < 0){
    fprintf(2, "bgburn: failed to raise quota\n");
    exit(1);
  }

  if(setbackground(1) < 0){
    fprintf(2, "bgburn: setbackground failed\n");
    exit(1);
  }

  printf("bgburn: pid=%d duration=%d ticks background=1\n",
         getpid(), duration_ticks);
  burn_cpu_for_ticks(duration_ticks);

  if(getecostats(&stats) < 0){
    fprintf(2, "bgburn: getecostats failed\n");
    exit(1);
  }

  printf("bgburn stats: pid=%d wait=%d ctxsw=%d background=%d deferrals=%d cpu=%d run=%d\n",
         stats.pid,
         stats.waiting_tick,
         stats.context_switches,
         stats.eco_background,
         stats.background_deferrals,
         stats.cpu_ticks,
         stats.runnable_ticks);
  printf("bgburn: pid=%d done\n", getpid());
  exit(0);
}
