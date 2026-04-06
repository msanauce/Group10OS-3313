#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/eco.h"
#include "user/user.h"

#define TEST_WINDOW_SIZE 100
#define MAX_CHILDREN 10

struct child_report {
  int child_id;
  int requested_quota;
  int configured_duration;
  int start_tick;
  int end_tick;
  struct eco_stats stats;
};

static int pipes[MAX_CHILDREN][2];
static int child_pids[MAX_CHILDREN];
static struct child_report reports[MAX_CHILDREN];

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
    // Busy wait so all children compete in the same quota window.
  }
}

static void
burn_cpu_for_ticks(int duration_ticks)
{
  int start = uptime();

  while(uptime() - start < duration_ticks){
    // Busy loop to consume CPU time.
  }
}

static void
run_child(int child_id, int quota, int start_tick, int duration_ticks, int write_fd)
{
  struct child_report report;

  if(setquota(quota) < 0){
    printf("quota_test: child %d failed to set quota %d\n", child_id, quota);
    close(write_fd);
    exit(1);
  }

  wait_for_tick(start_tick);

  report.child_id = child_id;
  report.requested_quota = quota;
  report.configured_duration = duration_ticks;
  report.start_tick = uptime();

  burn_cpu_for_ticks(duration_ticks);

  report.end_tick = uptime();

  if(getecostats(&report.stats) < 0){
    printf("quota_test: child %d failed to fetch eco stats\n", child_id);
    close(write_fd);
    exit(1);
  }

  if(write_all(write_fd, &report, sizeof(report)) < 0){
    printf("quota_test: child %d failed to write report\n", child_id);
    close(write_fd);
    exit(1);
  }

  close(write_fd);
  exit(0);
}

static int
find_slot_by_pid(int pid, int pids[], int count)
{
  int i;

  for(i = 0; i < count; i++){
    if(pids[i] == pid)
      return i;
  }

  return -1;
}

static void
print_report_table(struct child_report reports[], int count, int configured_start)
{
  int i;

  printf("\nquota_test results\n");
  printf("child\tpid\tquota\trun\tturn\twait\tquota_hits\tlast_window\tstate\n");

  for(i = 0; i < count; i++){
    int run_ticks = reports[i].end_tick - reports[i].start_tick;
    int turnaround = reports[i].end_tick - configured_start;

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
print_eco_metrics(struct child_report reports[], int count)
{
  int i;

  printf("\ndetailed eco metrics\n");

  for(i = 0; i < count; i++){
    struct eco_stats *stats = &reports[i].stats;

    printf("child %d pid %d\n", reports[i].child_id, stats->pid);
    printf("  quota=%d used=%d throttled=%d hits=%d\n",
           stats->cpu_quota, stats->cpu_used_in_window,
           stats->throttled, stats->quota_violations);
    printf("  wait=%d ctxsw=%d cpu=%d sleep=%d runnable=%d\n",
           stats->waiting_tick, stats->context_switches,
           stats->cpu_ticks, stats->sleep_ticks,
           stats->runnable_ticks);
    printf("  scheduled=%d wakeups=%d short_sleeps=%d score=%d\n",
           stats->times_scheduled, stats->wakeup_count,
           stats->short_sleep_count, stats->eco_score);
    printf("  stretch_calls=%d extra_sleep=%d\n",
           stats->stretched_sleep_calls, stats->total_extra_sleep_ticks);
  }
}

static void
print_summary(struct child_report reports[], int count, int configured_start)
{
  int i;
  int throttled_children = 0;
  int total_wait = 0;
  int total_run = 0;
  int total_turnaround = 0;
  int total_quota_hits = 0;
  int min_wait = -1;
  int max_wait = 0;
  int min_quota = reports[0].requested_quota;
  int max_quota = reports[0].requested_quota;

  for(i = 0; i < count; i++){
    int run_ticks = reports[i].end_tick - reports[i].start_tick;
    int turnaround = reports[i].end_tick - configured_start;
    int wait_ticks = reports[i].stats.waiting_tick;

    total_run += run_ticks;
    total_turnaround += turnaround;
    total_wait += wait_ticks;
    total_quota_hits += reports[i].stats.quota_violations;

    if(reports[i].stats.quota_violations > 0)
      throttled_children++;
    if(min_wait < 0 || wait_ticks < min_wait)
      min_wait = wait_ticks;
    if(wait_ticks > max_wait)
      max_wait = wait_ticks;
    if(reports[i].requested_quota < min_quota)
      min_quota = reports[i].requested_quota;
    if(reports[i].requested_quota > max_quota)
      max_quota = reports[i].requested_quota;
  }

  printf("\neco summary\n");
  printf("children completed : %d\n", count);
  printf("quota range        : %d..%d ticks per window\n", min_quota, max_quota);
  printf("throttled children : %d/%d\n", throttled_children, count);
  printf("total quota hits   : %d\n", total_quota_hits);
  printf("avg run ticks      : %d\n", total_run / count);
  printf("avg turnaround     : %d\n", total_turnaround / count);
  printf("avg waiting ticks  : %d\n", total_wait / count);
  printf("wait spread        : %d\n", max_wait - min_wait);
  printf("window size        : %d ticks\n", TEST_WINDOW_SIZE);
}

int
main(int argc, char *argv[])
{
  int nchildren = 5;
  int base_quota = 10;
  int duration_ticks = 200;
  int quota_step = 0;
  int start_tick;
  int i;

  if(argc > 1)
    nchildren = atoi(argv[1]);
  if(argc > 2)
    base_quota = atoi(argv[2]);
  if(argc > 3)
    duration_ticks = atoi(argv[3]);
  if(argc > 4)
    quota_step = atoi(argv[4]);

  if(nchildren <= 0 || nchildren > MAX_CHILDREN || base_quota <= 0 || duration_ticks <= 0){
    printf("Usage: quota_test [children 1..%d] [base_quota>0] [duration_ticks>0] [quota_step]\n",
           MAX_CHILDREN);
    printf("Example: quota_test 5 10 200 0\n");
    printf("Example: quota_test 5 8 200 2\n");
    exit(1);
  }

  start_tick = uptime() + 20;

  printf("\nquota_test configuration\n");
  printf("children    : %d\n", nchildren);
  printf("base quota  : %d\n", base_quota);
  printf("duration    : %d ticks\n", duration_ticks);
  printf("quota step  : %d\n", quota_step);
  printf("start tick  : %d\n", start_tick);

  for(i = 0; i < nchildren; i++){
    int child_quota = base_quota + i * quota_step;
    int pid;

    if(child_quota <= 0){
      printf("quota_test: child %d would receive invalid quota %d\n", i + 1, child_quota);
      exit(1);
    }

    if(pipe(pipes[i]) < 0){
      printf("quota_test: failed to create pipe for child %d\n", i + 1);
      exit(1);
    }

    pid = fork();
    if(pid < 0){
      printf("quota_test: fork failed while creating child %d\n", i + 1);
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
    int slot = find_slot_by_pid(pid, child_pids, nchildren);

    if(slot < 0){
      printf("quota_test: received unexpected child pid %d\n", pid);
      exit(1);
    }

    if(read_all(pipes[slot][0], &reports[slot], sizeof(reports[slot])) < 0){
      printf("quota_test: failed to read report for pid %d\n", pid);
      close(pipes[slot][0]);
      exit(1);
    }

    close(pipes[slot][0]);
  }

  print_report_table(reports, nchildren, start_tick);
  print_eco_metrics(reports, nchildren);
  print_summary(reports, nchildren, start_tick);

  printf("\nquota_test complete\n");
  exit(0);
}
