#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/eco.h"
#include "user/user.h"

#define TEST_WINDOW_SIZE 100
#define MAX_CHILDREN 10

struct child_report {
  int child_id;
  int requested_quota;
  int start_tick;
  int end_tick;
  struct eco_stats stats;
};

struct phase_summary {
  int mode;
  int start_tick;
  int completed;
  int min_quota;
  int max_quota;
  int throttled_children;
  int total_quota_hits;
  int avg_run_ticks;
  int avg_turnaround;
  int avg_waiting;
  int min_waiting;
  int max_waiting;
};

static int pipes[MAX_CHILDREN][2];
static int child_pids[MAX_CHILDREN];
static struct child_report baseline_reports[MAX_CHILDREN];
static struct child_report eco_reports[MAX_CHILDREN];

static char *
mode_name(int mode)
{
  if(mode == ECO_QUOTA)
    return "eco_on";
  return "eco_off";
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
    // Keep children aligned to the same scheduling window.
  }
}

static void
burn_cpu_for_ticks(int duration_ticks)
{
  int start = uptime();

  while(uptime() - start < duration_ticks){
    // Busy loop to create sustained CPU demand.
  }
}

static void
run_child(int child_id, int quota, int start_tick, int duration_ticks, int write_fd)
{
  struct child_report report;

  if(setquota(quota) < 0){
    printf("eco_test: child %d failed to set quota %d\n", child_id, quota);
    close(write_fd);
    exit(1);
  }

  wait_for_tick(start_tick);

  report.child_id = child_id;
  report.requested_quota = quota;
  report.start_tick = uptime();

  burn_cpu_for_ticks(duration_ticks);

  report.end_tick = uptime();

  if(getecostats(&report.stats) < 0){
    printf("eco_test: child %d failed to fetch eco stats\n", child_id);
    close(write_fd);
    exit(1);
  }

  if(write_all(write_fd, &report, sizeof(report)) < 0){
    printf("eco_test: child %d failed to write report\n", child_id);
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
  int total_quota_hits = 0;
  int throttled_children = 0;
  int min_waiting = -1;
  int max_waiting = 0;
  int min_quota = reports[0].requested_quota;
  int max_quota = reports[0].requested_quota;

  for(i = 0; i < count; i++){
    int run_ticks = reports[i].end_tick - reports[i].start_tick;
    int turnaround = reports[i].end_tick - start_tick;
    int waiting = reports[i].stats.waiting_tick;

    total_run += run_ticks;
    total_turnaround += turnaround;
    total_waiting += waiting;
    total_quota_hits += reports[i].stats.quota_violations;

    if(reports[i].stats.quota_violations > 0)
      throttled_children++;
    if(min_waiting < 0 || waiting < min_waiting)
      min_waiting = waiting;
    if(waiting > max_waiting)
      max_waiting = waiting;
    if(reports[i].requested_quota < min_quota)
      min_quota = reports[i].requested_quota;
    if(reports[i].requested_quota > max_quota)
      max_quota = reports[i].requested_quota;
  }

  summary->mode = mode;
  summary->start_tick = start_tick;
  summary->completed = count;
  summary->min_quota = min_quota;
  summary->max_quota = max_quota;
  summary->throttled_children = throttled_children;
  summary->total_quota_hits = total_quota_hits;
  summary->avg_run_ticks = total_run / count;
  summary->avg_turnaround = total_turnaround / count;
  summary->avg_waiting = total_waiting / count;
  summary->min_waiting = min_waiting;
  summary->max_waiting = max_waiting;
}

static void
print_phase_table(char *title, struct child_report reports[], int count, int start_tick)
{
  int i;

  printf("\n%s\n", title);
  printf("child\tpid\tquota\trun\tturn\twait\tquota_hits\tlast_window\tstate\n");

  for(i = 0; i < count; i++){
    int run_ticks = reports[i].end_tick - reports[i].start_tick;
    int turnaround = reports[i].end_tick - start_tick;

    printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t\t%d\t\t%s\n",
           reports[i].child_id,
           reports[i].stats.pid,
           reports[i].requested_quota,
           run_ticks,
           turnaround,
           reports[i].stats.waiting_tick,
           reports[i].stats.quota_violations,
           reports[i].stats.cpu_used_in_window,
           reports[i].stats.throttled ? "throttled" : "clear");
  }
}

static void
print_phase_summary(struct phase_summary *summary)
{
  printf("\n%s summary\n", mode_name(summary->mode));
  printf("children completed : %d\n", summary->completed);
  printf("quota range        : %d..%d ticks per window\n",
         summary->min_quota, summary->max_quota);
  printf("throttled children : %d/%d\n",
         summary->throttled_children, summary->completed);
  printf("total quota hits   : %d\n", summary->total_quota_hits);
  printf("avg run ticks      : %d\n", summary->avg_run_ticks);
  printf("avg turnaround     : %d\n", summary->avg_turnaround);
  printf("avg waiting ticks  : %d\n", summary->avg_waiting);
  printf("wait spread        : %d\n",
         summary->max_waiting - summary->min_waiting);
  printf("window size        : %d ticks\n", TEST_WINDOW_SIZE);
}

static void
print_comparison(struct phase_summary *baseline, struct phase_summary *eco)
{
  printf("\neco impact comparison\n");
  printf("mode transition     : %s -> %s\n",
         mode_name(baseline->mode), mode_name(eco->mode));
  printf("quota hits delta    : %d\n",
         eco->total_quota_hits - baseline->total_quota_hits);
  printf("throttled delta     : %d\n",
         eco->throttled_children - baseline->throttled_children);
  printf("avg turnaround diff : %d\n",
         eco->avg_turnaround - baseline->avg_turnaround);
  printf("avg waiting diff    : %d\n",
         eco->avg_waiting - baseline->avg_waiting);
  printf("avg run diff        : %d\n",
         eco->avg_run_ticks - baseline->avg_run_ticks);
}

static void
run_phase(int mode, int nchildren, int base_quota, int duration_ticks,
          int quota_step, struct child_report reports[],
          struct phase_summary *summary)
{
  int start_tick;
  int i;

  if(setecomode(mode) < 0){
    printf("eco_test: failed to switch eco mode to %d\n", mode);
    exit(1);
  }

  start_tick = uptime() + 20;

  for(i = 0; i < nchildren; i++){
    int child_quota = base_quota + i * quota_step;
    int pid;

    if(child_quota <= 0){
      printf("eco_test: child %d would receive invalid quota %d\n", i + 1, child_quota);
      exit(1);
    }

    if(pipe(pipes[i]) < 0){
      printf("eco_test: failed to create pipe for child %d\n", i + 1);
      exit(1);
    }

    pid = fork();
    if(pid < 0){
      printf("eco_test: fork failed while creating child %d\n", i + 1);
      close(pipes[i][0]);
      close(pipes[i][1]);
      exit(1);
    }

    if(pid == 0){
      close(pipes[i][0]);
      run_child(i + 1, child_quota, start_tick, duration_ticks, pipes[i][1]);
    }

    close(pipes[i][1]);
    child_pids[i] = pid;
  }

  for(i = 0; i < nchildren; i++){
    int pid = wait(0);
    int slot = find_slot_by_pid(pid, nchildren);

    if(slot < 0){
      printf("eco_test: received unexpected child pid %d\n", pid);
      exit(1);
    }

    if(read_all(pipes[slot][0], &reports[slot], sizeof(reports[slot])) < 0){
      printf("eco_test: failed to read report for pid %d\n", pid);
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
  int base_quota = 10;
  int duration_ticks = 200;
  int quota_step = 0;
  int original_mode;
  struct phase_summary baseline;
  struct phase_summary eco;

  if(argc > 1)
    nchildren = atoi(argv[1]);
  if(argc > 2)
    base_quota = atoi(argv[2]);
  if(argc > 3)
    duration_ticks = atoi(argv[3]);
  if(argc > 4)
    quota_step = atoi(argv[4]);

  if(nchildren <= 0 || nchildren > MAX_CHILDREN || base_quota <= 0 || duration_ticks <= 0){
    printf("Usage: eco_test [children 1..%d] [base_quota>0] [duration_ticks>0] [quota_step]\n",
           MAX_CHILDREN);
    printf("Example: eco_test 5 10 200 0\n");
    printf("Example: eco_test 5 8 200 2\n");
    exit(1);
  }

  original_mode = getecomode();

  printf("\neco_test configuration\n");
  printf("children      : %d\n", nchildren);
  printf("base quota    : %d\n", base_quota);
  printf("duration      : %d ticks\n", duration_ticks);
  printf("quota step    : %d\n", quota_step);
  printf("original mode : %s\n", mode_name(original_mode));

  run_phase(ECO_OFF, nchildren, base_quota, duration_ticks,
            quota_step, baseline_reports, &baseline);
  print_phase_table("baseline phase (eco off)", baseline_reports,
                    nchildren, baseline.start_tick);
  print_phase_summary(&baseline);

  run_phase(ECO_QUOTA, nchildren, base_quota, duration_ticks,
            quota_step, eco_reports, &eco);
  print_phase_table("eco phase (quota scheduler on)", eco_reports,
                    nchildren, eco.start_tick);
  print_phase_summary(&eco);

  print_comparison(&baseline, &eco);

  if(setecomode(original_mode) < 0){
    printf("eco_test: warning, failed to restore eco mode\n");
    exit(1);
  }

  printf("\neco_test complete\n");
  exit(0);
}
