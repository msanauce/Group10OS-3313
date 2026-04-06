#ifndef XV6_ECO_H
#define XV6_ECO_H

#include "param.h"

#define ECO_OFF 0
#define ECO_SCHED 1
#define ECO_QUOTA 2
#define ECO_CONTEXTSW 3
#define ECO_SLEEP_STRETCH 4

// switch values for adaptive slice based on churn
#define ECO_LOW_SLICE 3
#define ECO_HIGH_SLICE 6
#define ECO_CHURN_THRESHOLD 150

struct eco_stats {
  int pid;
  int cpu_quota;
  int cpu_used_in_window;
  int throttled;
  int quota_violations;
  int eco_background;
  int background_deferrals;
  int waiting_tick;
  int context_switches;
  int cpu_ticks;
  int sleep_ticks;
  int runnable_ticks;
  int times_scheduled;
  int wakeup_count;
  int short_sleep_count;
  int eco_score;
  int stretched_sleep_calls;
  int total_extra_sleep_ticks;
};

struct eco_idle_stats {
  int cpus_tracked;
  int current_idle_cpus;
  int idle_intervals;
  int total_idle_ticks;
  int longest_idle_streak;
  int uptime_ticks;
};

#endif
