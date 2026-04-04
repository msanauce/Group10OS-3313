#ifndef XV6_ECO_H
#define XV6_ECO_H

#define ECO_OFF 0
#define ECO_QUOTA 1

struct eco_stats {
  int pid;
  int cpu_quota;
  int cpu_used_in_window;
  int throttled;
  int quota_violations;
  int waiting_tick;
};

#endif
