#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

volatile int sink = 0;

void
burn_cpu(int rounds, int work)
{
  int i, j;

  for(i = 0; i < rounds; i++){
    for(j = 0; j < work; j++){
      sink = sink + ((i * 31) ^ j);
    }
  }
}

void
run_child(char *name, int quota)
{
  int phase;

  if(setquota(quota) < 0){
    printf("%s: setquota(%d) failed\n", name, quota);
    exit(1);
  }

  printf("%s: pid=%d quota=%d start\n", name, getpid(), quota);

  for(phase = 1; phase <= 6; phase++){
    burn_cpu(8, 12000000);
    printf("%s: pid=%d reached phase %d\n", name, getpid(), phase);
  }

  printf("%s: pid=%d done\n", name, getpid());
  exit(0);
}

int
main(void)
{
  int pid1, pid2;
  int status;

  printf("quota_test: starting demo\n");
  printf("quota_test: low quota child should lag behind high quota child\n");

  pid1 = fork();
  if(pid1 < 0){
    printf("quota_test: fork failed\n");
    exit(1);
  }
  if(pid1 == 0){
    run_child("low_quota", 2);
  }

  pid2 = fork();
  if(pid2 < 0){
    printf("quota_test: second fork failed\n");
    kill(pid1);
    wait(0);
    exit(1);
  }
  if(pid2 == 0){
    run_child("high_quota", 12);
  }

  wait(&status);
  printf("quota_test: first child exited status=%d\n", status);

  wait(&status);
  printf("quota_test: second child exited status=%d\n", status);

  printf("quota_test: finished\n");
  exit(0);
}

