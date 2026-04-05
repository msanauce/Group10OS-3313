#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/eco.h"
#include "user/user.h"

#define MAX_CHILDREN 10
#define WORK_STEADY 0
#define WORK_CHUNKY 1

struct child_report {
  int child_id;
  int start_tick;
  int end_tick;
  int workload_type;
  struct eco_stats stats;
};

struct phase_summary {
  int mode;
  int start_tick;
  int completed;
  int total_run_ticks;
  int total_turnaround;
  int total_waiting;
  int total_context_switches;
  int high_churn_children;
  int avg_run_ticks;
  int avg_turnaround;
  int avg_waiting;
  int avg_context_switches;
};

static int pipes[MAX_CHILDREN][2];
static int child_pids[MAX_CHILDREN];
static struct child_report baseline_reports[MAX_CHILDREN];
static struct child_report eco_reports[MAX_CHILDREN];

static char *
mode_name(int mode)
{
  if(mode == ECO_QUOTA)
    return "eco_quota";
  if(mode == ECO_CONTEXTSW)
    return "eco_contextsw";
  return "eco_off";
}

static char *
churn_class(int ctxsw)
{
  if(ctxsw > ECO_CHURN_THRESHOLD)
    return "HIGH_SLICE";
  return "LOW_SLICE";
}

static char *
workload_name(int type)
{
  if(type == WORK_STEADY)
    return "steady";
  return "chunky";
}

static void
run_steady_work(int duration_ticks)
{
  int start = uptime();
  while(uptime() - start < duration_ticks){
    // continuous burn
  }
}

static void
run_chunky_work(int duration_ticks)
{
  int done = 0;

  while(done < duration_ticks){
    int chunk = 1;

    if(done + chunk > duration_ticks)
      chunk = duration_ticks - done;

    int start = uptime();
    while(uptime() - start < chunk){
      // burn CPU in small bursts
    }

    done += chunk;
  }
}

static int
pick_workload_type(int child_id)
{
  if(child_id % 2 == 0)
    return WORK_CHUNKY;
  return WORK_STEADY;
}

static int
write_all(int fd, const void *buf, int n)
{
  int total = 0;

  while(total < n){
    int written = write(fd, (char *)buf + total, n - total);
    if(written <= 0)
      return -1;
    total += written;
  }

  return 0;
}

static int
read_all(int fd, void *buf, int n)
{
  int total = 0;

  while(total < n){
    int got = read(fd, (char *)buf + total, n - total);
    if(got <= 0)
      return -1;
    total += got;
  }

  return 0;
}

static void
wait_for_tick(int start_tick)
{
  while(uptime() < start_tick){
    // keep all children aligned to the same competition window
  }
}

static void
run_child(int child_id, int workload_type, int start_tick, int duration_ticks, int write_fd)
{
  struct child_report report;

  wait_for_tick(start_tick);

  report.child_id = child_id;
  report.workload_type=workload_type;
  report.start_tick = uptime();

  if(workload_type == WORK_STEADY)
  {run_steady_work(duration_ticks);}
  else
  {run_chunky_work(duration_ticks);}

  report.end_tick = uptime();

  if(getecostats(&report.stats) < 0){
    printf("contextsw_test: child %d failed to fetch eco stats\n", child_id);
    close(write_fd);
    exit(1);
  }

  if(write_all(write_fd, &report, sizeof(report)) < 0){
    printf("contextsw_test: child %d failed to write report\n", child_id);
    close(write_fd);
    exit(1);
  }

  close(write_fd);
  exit(0);
}

static int
find_slot_by_pid(int pid, int count)
{
  int i;

  for(i = 0; i < count; i++){
    if(child_pids[i] == pid)
      return i;
  }

  return -1;
}

static void
summarize_phase(struct child_report reports[], int count, int mode,
                int start_tick, struct phase_summary *summary)
{
  int i;
  int total_run = 0;
  int total_turnaround = 0;
  int total_waiting = 0;
  int total_ctxsw = 0;
  int high_churn = 0;

  for(i = 0; i < count; i++){
    int run_ticks = reports[i].end_tick - reports[i].start_tick;
    int turnaround = reports[i].end_tick - start_tick;

    total_run += run_ticks;
    total_turnaround += turnaround;
    total_waiting += reports[i].stats.waiting_tick;
    total_ctxsw += reports[i].stats.context_switches;

    if(reports[i].stats.context_switches > ECO_CHURN_THRESHOLD)
      high_churn++;
  }

  summary->mode = mode;
  summary->start_tick = start_tick;
  summary->completed = count;
  summary->total_run_ticks = total_run;
  summary->total_turnaround = total_turnaround;
  summary->total_waiting = total_waiting;
  summary->total_context_switches = total_ctxsw;
  summary->high_churn_children = high_churn;
  summary->avg_run_ticks = total_run / count;
  summary->avg_turnaround = total_turnaround / count;
  summary->avg_waiting = total_waiting / count;
  summary->avg_context_switches = total_ctxsw / count;
}

static void
print_phase_table(char *title, struct child_report reports[], int count, int start_tick)
{
  int i;

  printf("\n%s\n", title);
  printf("child\tpid\twork\trun\tturn\twait\tctxsw\tclass\t\teff(run/ctx)\n");

  for(i = 0; i < count; i++){
    int run_ticks = reports[i].end_tick - reports[i].start_tick;
    int turnaround = reports[i].end_tick - start_tick;
    int ctxsw = reports[i].stats.context_switches;
    int efficiency = (ctxsw > 0) ? (run_ticks / ctxsw) : run_ticks;

   printf("%d\t%d\t%s\t%d\t%d\t%d\t%d\t%s\t%d\n",
       reports[i].child_id,
       reports[i].stats.pid,
       workload_name(reports[i].workload_type),
       run_ticks,
       turnaround,
       reports[i].stats.waiting_tick,
       ctxsw,
       churn_class(ctxsw),
       efficiency);
   }
}

static void
print_phase_summary(struct phase_summary *summary)
{
  printf("\n%s summary\n", mode_name(summary->mode));
  printf("children completed   : %d\n", summary->completed);
  printf("avg run ticks        : %d\n", summary->avg_run_ticks);
  printf("avg turnaround       : %d\n", summary->avg_turnaround);
  printf("avg waiting ticks    : %d\n", summary->avg_waiting);
  printf("total context sw     : %d\n", summary->total_context_switches);
  printf("avg context sw       : %d\n", summary->avg_context_switches);
  printf("high churn children  : %d/%d\n",
         summary->high_churn_children, summary->completed);
  printf("threshold            : %d\n", ECO_CHURN_THRESHOLD);
  printf("slice policy         : low=%d high=%d\n",
         ECO_LOW_SLICE, ECO_HIGH_SLICE);
}

static void
print_comparison(struct phase_summary *baseline, struct phase_summary *eco)
{
  printf("\ncontext-switch impact comparison\n");
  printf("mode transition      : %s -> %s\n",
         mode_name(baseline->mode), mode_name(eco->mode));
  printf("total ctxsw delta    : %d\n",
         eco->total_context_switches - baseline->total_context_switches);
  printf("avg ctxsw delta      : %d\n",
         eco->avg_context_switches - baseline->avg_context_switches);
  printf("avg turnaround diff  : %d\n",
         eco->avg_turnaround - baseline->avg_turnaround);
  printf("avg waiting diff     : %d\n",
         eco->avg_waiting - baseline->avg_waiting);
  printf("high churn delta     : %d\n",
         eco->high_churn_children - baseline->high_churn_children);
}

static void
run_phase(int mode, int nchildren, int duration_ticks,
          struct child_report reports[],
          struct phase_summary *summary)
{
  int start_tick;
  int i;

  if(setecomode(mode) < 0){
    printf("contextsw_test: failed to switch eco mode to %d\n", mode);
    exit(1);
  }

  start_tick = uptime() + 20;

  for(i = 0; i < nchildren; i++){
    int pid;
    int workload_type=pick_workload_type(i+1);

    if(pipe(pipes[i]) < 0){
      printf("contextsw_test: failed to create pipe for child %d\n", i + 1);
      exit(1);
    }

    pid = fork();
    if(pid < 0){
      printf("contextsw_test: fork failed while creating child %d\n", i + 1);
      close(pipes[i][0]);
      close(pipes[i][1]);
      exit(1);
    }

    if(pid == 0){
      close(pipes[i][0]);
      run_child(i + 1, workload_type, start_tick, duration_ticks, pipes[i][1]);
    }

    close(pipes[i][1]);
    child_pids[i] = pid;
  }

  for(i = 0; i < nchildren; i++){
    int pid = wait(0);
    int slot = find_slot_by_pid(pid, nchildren);

    if(slot < 0){
      printf("contextsw_test: received unexpected child pid %d\n", pid);
      exit(1);
    }

    if(read_all(pipes[slot][0], &reports[slot], sizeof(reports[slot])) < 0){
      printf("contextsw_test: failed to read report for pid %d\n", pid);
      close(pipes[slot][0]);
      exit(1);
    }

    close(pipes[slot][0]);
  }

  summarize_phase(reports, nchildren, mode, start_tick, summary);
}

int
main(int argc, char *argv[])
{
  int nchildren = 5;
  int duration_ticks = 200;
  int original_mode;
  struct phase_summary baseline;
  struct phase_summary eco;

  if(argc > 1)
    nchildren = atoi(argv[1]);
  if(argc > 2)
    duration_ticks = atoi(argv[2]);

  if(nchildren <= 0 || nchildren > MAX_CHILDREN || duration_ticks <= 0){
    printf("Usage: contextsw_test [children 1..%d] [duration_ticks>0]\n",
           MAX_CHILDREN);
    printf("Example: contextsw_test 5 200\n");
    printf("Example: contextsw_test 8 300\n");
    exit(1);
  }

  original_mode = getecomode();

  printf("\ncontextsw_test configuration\n");
  printf("children          : %d\n", nchildren);
  printf("duration          : %d ticks\n", duration_ticks);
  printf("original mode     : %s\n", mode_name(original_mode));
  printf("churn threshold   : %d\n", ECO_CHURN_THRESHOLD);
  printf("low/high slices   : %d / %d\n", ECO_LOW_SLICE, ECO_HIGH_SLICE);

  run_phase(ECO_OFF, nchildren, duration_ticks, baseline_reports, &baseline);
  print_phase_table("baseline phase (eco off)", baseline_reports,
                    nchildren, baseline.start_tick);
  print_phase_summary(&baseline);

  run_phase(ECO_CONTEXTSW, nchildren, duration_ticks, eco_reports, &eco);
  print_phase_table("eco phase (adaptive context-switch mode)", eco_reports,
                    nchildren, eco.start_tick);
  print_phase_summary(&eco);

  print_comparison(&baseline, &eco);

  if(setecomode(original_mode) < 0){
    printf("contextsw_test: warning, failed to restore eco mode\n");
    exit(1);
  }

  printf("\ncontextsw_test complete\n");
  exit(0);
}