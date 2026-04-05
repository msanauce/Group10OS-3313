#ifndef XV6_ECO_H
#define XV6_ECO_H

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
  int waiting_tick;
  int context_switches;
  int stretched_sleep_calls;
  int total_extra_sleep_ticks;
};

#endif
