#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int duration_ticks = 100;
  int demo_quota;
  int start;
  int quiet = 0;

  if(argc > 3){
    fprintf(2, "usage: cpuburn [-q] [ticks]\n");
    exit(1);
  }

  if(argc >= 2 && strcmp(argv[1], "-q") == 0){
    quiet = 1;
    if(argc == 3)
      duration_ticks = atoi(argv[2]);
  } else if(argc == 2){
    duration_ticks = atoi(argv[1]);
  }

  if((argc == 2 && quiet == 0) || argc == 3){
    if(duration_ticks <= 0){
      fprintf(2, "cpuburn: ticks must be > 0\n");
      exit(1);
    }
  }

  // Keep the demo responsive even if the user previously enabled quota mode.
  demo_quota = duration_ticks + 100;
  if(setquota(demo_quota) < 0){
    fprintf(2, "cpuburn: failed to raise quota\n");
    exit(1);
  }

  if(!quiet)
    printf("cpuburn: pid=%d duration=%d ticks\n", getpid(), duration_ticks);
  start = uptime();
  while(uptime() - start < duration_ticks){
    // Busy-loop to keep the CPU active for the requested interval.
  }
  if(!quiet)
    printf("cpuburn: pid=%d done\n", getpid());
  exit(0);
}
