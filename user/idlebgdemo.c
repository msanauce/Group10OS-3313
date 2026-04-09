#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/eco.h"
#include "user/user.h"

#define ROLE_FG1 1
#define ROLE_FG2 2
#define ROLE_BG 3

struct worker_report {
  int role;
  int start_tick;
  int end_tick;
  struct eco_stats stats;
};

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
burn_cpu_for_ticks(int duration_ticks)
{
  int start = uptime();

  while(uptime() - start < duration_ticks){
    // Keep the process runnable for the whole window.
  }
}

static void
print_window_stats(char *label, int window_start)
{
  struct eco_idle_stats stats;
  int elapsed_ticks;
  int capacity_ticks;
  int busy_ticks;
  int idle_pct;

  if(getecoidlestats(&stats) < 0){
    printf("idlebgdemo: getecoidlestats failed during %s\n", label);
    return;
  }

  elapsed_ticks = uptime() - window_start;
  capacity_ticks = elapsed_ticks * stats.cpus_tracked;
  busy_ticks = capacity_ticks - stats.total_idle_ticks;
  idle_pct = 0;
  if(capacity_ticks > 0)
    idle_pct = (stats.total_idle_ticks * 100) / capacity_ticks;

  printf("\n%s\n", label);
  printf("window_ticks          %d\n", elapsed_ticks);
  printf("cpus_tracked          %d\n", stats.cpus_tracked);
  printf("current_idle_cpus     %d\n", stats.current_idle_cpus);
  printf("idle_intervals        %d\n", stats.idle_intervals);
  printf("total_idle_ticks      %d\n", stats.total_idle_ticks);
  printf("estimated_busy_ticks  %d\n", busy_ticks);
  printf("idle_capacity_percent %d\n", idle_pct);
  printf("longest_idle_streak   %d\n", stats.longest_idle_streak);
}

static char *
role_name(int role)
{
  if(role == ROLE_FG1)
    return "foreground-1";
  if(role == ROLE_FG2)
    return "foreground-2";
  return "background";
}

static void
run_worker(int role, int start_tick, int duration_ticks, int write_fd)
{
  struct worker_report report;

  if(setquota(duration_ticks + 100) < 0){
    printf("idlebgdemo: failed to raise quota for %s\n", role_name(role));
    close(write_fd);
    exit(1);
  }

  if(role == ROLE_BG && setbackground(1) < 0){
    printf("idlebgdemo: failed to mark background worker\n");
    close(write_fd);
    exit(1);
  }

  while(uptime() < start_tick){
    // Align all workers to the same contention window.
  }

  report.role = role;
  report.start_tick = uptime();
  burn_cpu_for_ticks(duration_ticks);
  report.end_tick = uptime();

  if(getecostats(&report.stats) < 0){
    printf("idlebgdemo: getecostats failed for %s\n", role_name(role));
    close(write_fd);
    exit(1);
  }

  if(write_all(write_fd, &report, sizeof(report)) < 0){
    printf("idlebgdemo: failed to write report for %s\n", role_name(role));
    close(write_fd);
    exit(1);
  }

  close(write_fd);
  exit(0);
}

static void
print_report(struct worker_report *report)
{
  printf("%s\n", role_name(report->role));
  printf("pid                  %d\n", report->stats.pid);
  printf("run_ticks            %d\n", report->end_tick - report->start_tick);
  printf("waiting_tick         %d\n", report->stats.waiting_tick);
  printf("context_switches     %d\n", report->stats.context_switches);
  printf("cpu_ticks            %d\n", report->stats.cpu_ticks);
  printf("runnable_ticks       %d\n", report->stats.runnable_ticks);
  printf("eco_background       %d\n", report->stats.eco_background);
  printf("background_deferrals %d\n", report->stats.background_deferrals);
}

int
main(void)
{
  int i;
  int status;
  int original_mode = getecomode();
  int pipes[3][2];
  int start_tick;
  int window_start;
  int pids[3];
  int roles[3] = { ROLE_FG1, ROLE_FG2, ROLE_BG };
  struct worker_report reports[3];

  if(setecomode(ECO_OFF) < 0){
    printf("idlebgdemo: failed to switch to eco off mode\n");
    exit(1);
  }

  if(resetecoidle() < 0){
    printf("idlebgdemo: failed to reset idle stats\n");
    if(original_mode != ECO_OFF)
      setecomode(original_mode);
    exit(1);
  }

  window_start = uptime();
  pause(10);
  print_window_stats("baseline idle window", window_start);

  if(resetecoidle() < 0){
    printf("idlebgdemo: failed to reset busy window\n");
    if(original_mode != ECO_OFF)
      setecomode(original_mode);
    exit(1);
  }

  printf("\nlaunching workload\n");
  printf("two foreground CPU burners compete with one background burner\n");

  start_tick = uptime() + 5;
  window_start = uptime();

  for(i = 0; i < 3; i++){
    if(pipe(pipes[i]) < 0){
      printf("idlebgdemo: pipe failed\n");
      if(original_mode != ECO_OFF)
        setecomode(original_mode);
      exit(1);
    }

    pids[i] = fork();
    if(pids[i] < 0){
      printf("idlebgdemo: fork failed\n");
      close(pipes[i][0]);
      close(pipes[i][1]);
      if(original_mode != ECO_OFF)
        setecomode(original_mode);
      exit(1);
    }

    if(pids[i] == 0){
      close(pipes[i][0]);
      run_worker(roles[i], start_tick, 20, pipes[i][1]);
    }

    close(pipes[i][1]);
  }

  for(i = 0; i < 3; i++){
    if(wait(&status) < 0){
      printf("idlebgdemo: wait failed\n");
      close(pipes[i][0]);
      break;
    }

    if(read_all(pipes[i][0], &reports[i], sizeof(reports[i])) < 0){
      printf("idlebgdemo: failed to read worker report\n");
      close(pipes[i][0]);
      break;
    }

    close(pipes[i][0]);
  }

  print_window_stats("workload window", window_start);

  printf("\nworker summary\n");
  for(i = 0; i < 3; i++){
    print_report(&reports[i]);
  }

  if(original_mode != ECO_OFF && setecomode(original_mode) < 0){
    printf("idlebgdemo: warning, failed to restore original mode %d\n",
           original_mode);
    exit(1);
  }

  printf("\nidlebgdemo complete\n");
  exit(0);
}
