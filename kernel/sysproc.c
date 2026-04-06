#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "eco.h"

static int stretched_sleep_calls;
static int total_extra_sleep_ticks;


uint64
sys_kps(void)
{
  char buf[4];               // enough for "-o" or "-l" + '\0' (and a bit extra)
  if (argstr(0, buf, sizeof(buf)) < 0)
    return -1;
  return kps(buf);
}

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  int actual_ticks;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  actual_ticks = n;
  acquire(&tickslock);
  if(eco_mode == ECO_SLEEP_STRETCH){
    actual_ticks = n + 3;
    stretched_sleep_calls++;
    total_extra_sleep_ticks += 3;
  }
  ticks0 = ticks;
  while(ticks - ticks0 < actual_ticks){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//----------------------Quota Stuff--------------------------
uint64
sys_setquota(void)
{
  int quota;
  struct proc *p = myproc();

  argint(0, &quota);

  if(quota <= 0)
    return -1;

  acquire(&p->lock);
  p->cpu_quota = quota;

  if(p->cpu_used_in_window < p->cpu_quota)
    p->throttled = 0;

  release(&p->lock);

  return 0;
}

uint64
sys_getecostats(void)
{
  uint64 addr;
  struct proc *p = myproc();
  struct eco_stats stats;

  argaddr(0, &addr);

  acquire(&p->lock);
  stats.pid = p->pid;
  stats.cpu_quota = p->cpu_quota;
  stats.cpu_used_in_window = p->cpu_used_in_window;
  stats.throttled = p->throttled;
  stats.quota_violations = p->quota_violations;
  stats.eco_background = p->eco_background;
  stats.background_deferrals = p->background_deferrals;
  stats.waiting_tick = p->waiting_tick;
  stats.context_switches = p->context_switches;
  stats.cpu_ticks = p->cpu_ticks;
  stats.sleep_ticks = p->sleep_ticks;
  stats.runnable_ticks = p->runnable_ticks;
  stats.times_scheduled = p->times_scheduled;
  stats.wakeup_count = p->wakeup_count;
  stats.short_sleep_count = p->short_sleep_count;
  stats.eco_score = p->eco_score;
  release(&p->lock);

  // Sleep-stretch metrics are global for demo simplicity, so every process
  // sees the same aggregated values here.
  acquire(&tickslock);
  stats.stretched_sleep_calls = stretched_sleep_calls;
  stats.total_extra_sleep_ticks = total_extra_sleep_ticks;
  release(&tickslock);

  if(copyout(p->pagetable, addr, (char *)&stats, sizeof(stats)) < 0)
    return -1;

  return 0;
}

uint64
sys_setecomode(void)
{
  int mode;

  argint(0, &mode);
  if(mode != ECO_OFF && mode != ECO_SCHED && mode != ECO_QUOTA &&
     mode != ECO_CONTEXTSW && mode != ECO_SLEEP_STRETCH)
    return -1;

  eco_mode = mode;
  return 0;
}

uint64
sys_getecomode(void)
{
  return eco_mode;
}

uint64
sys_getecoidlestats(void)
{
  uint64 addr;
  struct proc *p = myproc();
  struct eco_idle_stats stats;

  argaddr(0, &addr);
  get_eco_idle_stats(&stats);

  if(copyout(p->pagetable, addr, (char *)&stats, sizeof(stats)) < 0)
    return -1;

  return 0;
}

uint64
sys_resetecoidle(void)
{
  reset_eco_idle_stats();
  return 0;
}

uint64
sys_setbackground(void)
{
  int enabled;
  struct proc *p = myproc();

  argint(0, &enabled);
  if(enabled != 0 && enabled != 1)
    return -1;

  acquire(&p->lock);
  p->eco_background = enabled;
  release(&p->lock);

  return 0;
}
