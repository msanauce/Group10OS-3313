#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

//--------Quota Stuff-----------------------

#define WINDOW_SIZE 100

uint quota_window_start = 0;
int eco_mode = ECO_OFF;

struct eco_idle_cpu_state {
  uint total_idle_ticks;
  uint idle_intervals;
  uint longest_idle_streak;
  uint idle_start_tick;
  int is_idle;
};

static struct eco_idle_cpu_state eco_idle_cpus[NCPU];
static struct spinlock eco_idle_lock;
static int eco_idle_max_cpu_seen;




struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

static struct proc*
pick_round_robin_process(int start, int *next_index)
{
  struct proc *p;
  int offset;

  for(offset = 0; offset < NPROC; offset++){
    p = &proc[(start + offset) % NPROC];
    acquire(&p->lock);
    if(p->state == RUNNABLE &&
       (eco_mode != ECO_QUOTA || p->throttled == 0)){
      *next_index = ((int)(p - proc) + 1) % NPROC;
      return p;
    }
    release(&p->lock);
  }

  return 0;
}

static int
eco_background_pressure_threshold(void)
{
  int tracked_cpus;

  acquire(&eco_idle_lock);
  tracked_cpus = eco_idle_max_cpu_seen + 1;
  if(tracked_cpus < 1)
    tracked_cpus = 1;
  release(&eco_idle_lock);

  if(tracked_cpus <= 1)
    return 1;
  return tracked_cpus - 1;
}

static void
note_background_deferral(int proc_index)
{
  struct proc *p;

  if(proc_index < 0 || proc_index >= NPROC)
    return;

  p = &proc[proc_index];
  acquire(&p->lock);
  if(p->state == RUNNABLE && p->eco_background)
    p->background_deferrals++;
  release(&p->lock);
}

static struct proc*
pick_round_robin_process_eco(int start, int *next_index)
{
  int offset;
  int first_runnable = -1;
  int first_foreground = -1;
  int first_background = -1;
  int active_foreground = 0;
  int chosen_index;
  int threshold = eco_background_pressure_threshold();
  struct proc *p;

  for(offset = 0; offset < NPROC; offset++){
    int idx = (start + offset) % NPROC;

    p = &proc[idx];
    acquire(&p->lock);
    if((p->state == RUNNABLE || p->state == RUNNING) &&
       (eco_mode != ECO_QUOTA || p->throttled == 0)){
      if(p->eco_background == 0)
        active_foreground++;
    }

    if(p->state == RUNNABLE &&
       (eco_mode != ECO_QUOTA || p->throttled == 0)){
      if(first_runnable < 0)
        first_runnable = idx;
      if(p->eco_background){
        if(first_background < 0)
          first_background = idx;
      } else {
        if(first_foreground < 0)
          first_foreground = idx;
      }
    }
    release(&p->lock);
  }

  if(first_runnable < 0)
    return 0;

  chosen_index = first_runnable;
  if(first_background >= 0 &&
     active_foreground >= threshold){
    note_background_deferral(first_background);
    if(first_foreground >= 0)
      chosen_index = first_foreground;
    else
      return 0;
  }

  p = &proc[chosen_index];
  acquire(&p->lock);
  if(p->state == RUNNABLE &&
     (eco_mode != ECO_QUOTA || p->throttled == 0)){
    *next_index = (chosen_index + 1) % NPROC;
    return p;
  }
  release(&p->lock);

  return pick_round_robin_process(start, next_index);
}

static void
eco_idle_mark_cpu(int cpu_id)
{
  acquire(&eco_idle_lock);
  if(cpu_id > eco_idle_max_cpu_seen)
    eco_idle_max_cpu_seen = cpu_id;
  release(&eco_idle_lock);
}

static void
eco_idle_enter(int cpu_id)
{
  uint current_ticks;

  acquire(&tickslock);
  current_ticks = ticks;
  release(&tickslock);

  acquire(&eco_idle_lock);
  if(cpu_id > eco_idle_max_cpu_seen)
    eco_idle_max_cpu_seen = cpu_id;
  if(eco_idle_cpus[cpu_id].is_idle == 0){
    eco_idle_cpus[cpu_id].is_idle = 1;
    eco_idle_cpus[cpu_id].idle_intervals++;
    eco_idle_cpus[cpu_id].idle_start_tick = current_ticks;
  }
  release(&eco_idle_lock);
}

static void
eco_idle_exit(int cpu_id)
{
  uint current_ticks;
  uint duration;

  acquire(&tickslock);
  current_ticks = ticks;
  release(&tickslock);

  acquire(&eco_idle_lock);
  if(cpu_id > eco_idle_max_cpu_seen)
    eco_idle_max_cpu_seen = cpu_id;
  if(eco_idle_cpus[cpu_id].is_idle){
    duration = current_ticks - eco_idle_cpus[cpu_id].idle_start_tick;
    eco_idle_cpus[cpu_id].total_idle_ticks += duration;
    if(duration > eco_idle_cpus[cpu_id].longest_idle_streak)
      eco_idle_cpus[cpu_id].longest_idle_streak = duration;
    eco_idle_cpus[cpu_id].is_idle = 0;
    eco_idle_cpus[cpu_id].idle_start_tick = current_ticks;
  }
  release(&eco_idle_lock);
}

void
get_eco_idle_stats(struct eco_idle_stats *stats)
{
  int i;
  uint current_ticks;
  uint total_idle_ticks = 0;
  uint idle_intervals = 0;
  uint longest_idle_streak = 0;
  int current_idle_cpus = 0;
  int tracked_cpus;

  acquire(&tickslock);
  current_ticks = ticks;
  release(&tickslock);

  acquire(&eco_idle_lock);
  tracked_cpus = eco_idle_max_cpu_seen + 1;
  if(tracked_cpus < 1)
    tracked_cpus = 1;
  for(i = 0; i < tracked_cpus; i++){
    uint cpu_idle_ticks = eco_idle_cpus[i].total_idle_ticks;
    uint cpu_longest = eco_idle_cpus[i].longest_idle_streak;

    if(eco_idle_cpus[i].is_idle){
      uint live_duration = current_ticks - eco_idle_cpus[i].idle_start_tick;
      cpu_idle_ticks += live_duration;
      if(live_duration > cpu_longest)
        cpu_longest = live_duration;
      current_idle_cpus++;
    }

    total_idle_ticks += cpu_idle_ticks;
    idle_intervals += eco_idle_cpus[i].idle_intervals;
    if(cpu_longest > longest_idle_streak)
      longest_idle_streak = cpu_longest;
  }
  release(&eco_idle_lock);

  stats->cpus_tracked = tracked_cpus;
  stats->current_idle_cpus = current_idle_cpus;
  stats->idle_intervals = idle_intervals;
  stats->total_idle_ticks = total_idle_ticks;
  stats->longest_idle_streak = longest_idle_streak;
  stats->uptime_ticks = current_ticks;
}

void
reset_eco_idle_stats(void)
{
  int i;
  uint current_ticks;

  acquire(&tickslock);
  current_ticks = ticks;
  release(&tickslock);

  acquire(&eco_idle_lock);
  for(i = 0; i < NCPU; i++){
    eco_idle_cpus[i].total_idle_ticks = 0;
    eco_idle_cpus[i].idle_intervals = 0;
    eco_idle_cpus[i].longest_idle_streak = 0;
    if(eco_idle_cpus[i].is_idle){
      eco_idle_cpus[i].idle_intervals = 1;
      eco_idle_cpus[i].idle_start_tick = current_ticks;
    } else {
      eco_idle_cpus[i].idle_start_tick = 0;
    }
  }
  release(&eco_idle_lock);
}

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.


int
kps(char *arguments)
{
  struct proc *p;

  static char *states[] = {
    [UNUSED]    "unused",
    [USED]      "used",
    [SLEEPING]  "sleep",
    [RUNNABLE]  "runble",
    [RUNNING]   "run",
    [ZOMBIE]    "zombie"
  };

  if (arguments == 0) {
    printf("Usage: ps [-o | -l]\n");
    return -1;
  }

  if (strncmp(arguments, "-o", 2) == 0 && arguments[2] == '\0') {
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state != UNUSED) {
        printf("%s\n", p->name);
      }
      release(&p->lock);
    }
  }
  else if (strncmp(arguments, "-l", 2) == 0 && arguments[2] == '\0') {
    printf("PID\tSTATE\tNAME\n");
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state != UNUSED) {
        char *st = "unknown";
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
          st = states[p->state];
        printf("%d\t%s\t%s\n", p->pid, st, p->name);
      }
      release(&p->lock);
    }
  }
  else {
    printf("Usage: ps [-o | -l]\n");
    return -1;
  }

  return 0;
}

//eco metrics helper:
void
update_process_metrics_on_tick(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state == RUNNING)
      p->cpu_ticks++;
    else if(p->state == RUNNABLE){
      p->runnable_ticks++;
      p->waiting_tick++;
    }
    release(&p->lock);
  }
}


void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  initlock(&eco_idle_lock, "eco_idle");
  eco_idle_max_cpu_seen = 0;
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->waiting_tick = 0;

  // CPU quota defaults
  p->cpu_quota = 20;
  p->cpu_used_in_window = 0;
  p->throttled = 0;
  p->quota_violations = 0;
  p->eco_background = 0;
  p->background_deferrals = 0;

  //context switch defaults
  p->context_switches = 0;
  p->slice_ticks = 0;

  //Eco scheduling defaults
  p->cpu_ticks = 0;
  p->sleep_ticks = 0;
  p->runnable_ticks = 0;
  p->times_scheduled = 0;
  p->wakeup_count = 0;
  p->short_sleep_count = 0;
  p->sleep_start_tick = 0;
  p->eco_score = 0;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->context_switches = 0;
  p->slice_ticks = 0;
  p->waiting_tick = 0;
  p->eco_background = 0;
  p->background_deferrals = 0;

  //eco scheduler stuff
  p->cpu_ticks = 0;
  p->sleep_ticks = 0;
  p->runnable_ticks = 0;
  p->times_scheduled = 0;
  p->wakeup_count = 0;
  p->short_sleep_count = 0;
  p->sleep_start_tick = 0;
  p->eco_score = 0;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > TRAPFRAME) {
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));
  np->eco_background = p->eco_background;
  np->background_deferrals = 0;

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

//-------------------Quota Stuff---------------------------------------

int
reset_cpu_quotas(void)
{
  struct proc *p;
  int reset_count = 0;

  for(p = proc; p < &proc[NPROC]; p++) {
    int was_throttled = 0;
    int pid = 0;
    int used = 0;
    int quota = 0;
    int violations = 0;
    char name[16];

    acquire(&p->lock);
    if(p->state != UNUSED) {
      if(p->throttled){
        was_throttled = 1;
        reset_count++;
        pid = p->pid;
        used = p->cpu_used_in_window;
        quota = p->cpu_quota;
        violations = p->quota_violations;
        safestrcpy(name, p->name, sizeof(name));
      }
      p->cpu_used_in_window = 0;
      p->throttled = 0;
    }
    release(&p->lock);

    if(was_throttled){
      printf("[quota] reset pid=%d (%s): cleared throttle after %d/%d ticks, total hits=%d\n",
             pid, name, used, quota, violations);
    }
  }

  return reset_count;
}

void
check_and_reset_quota_window(void)
{
  uint current_ticks;
  uint previous_start;
  int reset_count;

  acquire(&tickslock);
  current_ticks = ticks;
  if(current_ticks - quota_window_start < WINDOW_SIZE){
    release(&tickslock);
    return;
  }

  previous_start = quota_window_start;
  quota_window_start = current_ticks;
  release(&tickslock);

  reset_count = reset_cpu_quotas();
  if(reset_count > 0){
    printf("[quota] window advanced: [%d, %d) -> [%d, %d), reset %d throttled proc(s)\n",
           previous_start, previous_start + WINDOW_SIZE,
           current_ticks, current_ticks + WINDOW_SIZE,
           reset_count);
  }
}

//calculating the eco score
static int
compute_eco_score(struct proc *p)
{
  int score = 1000;

  // A simple eco score: penalize sustained CPU use,
  // but keep some aging so waiting/sleeping tasks still make progress.
  score -= (int)(5 * p->cpu_ticks);
  score += (int)p->waiting_tick;
  score += (int)(2 * p->sleep_ticks);

  if(score < 0)
    score = 0;

  return score;
}


//pick eco mode scheduler:
static struct proc*
pick_eco_process(void)
{
  struct proc *p;
  struct proc *best = 0;
  int best_score = -1;
  uint best_times_scheduled = 0;

  for(p = proc; p < &proc[NPROC]; p++){
    int score;

    acquire(&p->lock);

    if(p->state == RUNNABLE &&
       (eco_mode != ECO_QUOTA || p->throttled == 0)){
      score = compute_eco_score(p);
      p->eco_score = score;

      if(best == 0 ||
         score > best_score ||
         (score == best_score &&
          p->times_scheduled < best_times_scheduled)){
        best = p;
        best_score = score;
        best_times_scheduled = p->times_scheduled;
      }
    }

    release(&p->lock);
  }

  if(best == 0)
    return 0;

  acquire(&best->lock);
  if(best->state == RUNNABLE &&
     (eco_mode != ECO_QUOTA || best->throttled == 0)){
    best->eco_score = compute_eco_score(best);
    return best;   // lock is held
  }
  release(&best->lock);
  return 0;
}

int
eco_background_should_yield(struct proc *current)
{
  (void)current;
  return 0;
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *chosen;
  struct cpu *c = mycpu();
  static int last_index = 0;
  int start;

  eco_idle_mark_cpu(cpuid());
  c->proc = 0;
  for(;;){
    intr_on();
    intr_off();

    if(eco_mode == ECO_QUOTA){
      check_and_reset_quota_window();
    }

    chosen = 0;

    if(eco_mode == ECO_SCHED){
      chosen = pick_eco_process();
    } else {
      // Baseline path: rotate the scan start so RUNNABLE processes
      // take turns instead of always favoring the earliest slot.
      start = last_index;
      if(eco_mode == ECO_OFF)
        chosen = pick_round_robin_process_eco(start, &last_index);
      else
        chosen = pick_round_robin_process(start, &last_index);
    }

    if(chosen == 0){
      eco_idle_enter(cpuid());
      asm volatile("wfi");
      continue;
    }

    eco_idle_exit(cpuid());
    chosen->times_scheduled++;
    

    if(chosen->state == RUNNABLE){
      chosen->state = RUNNING;
      chosen->context_switches++;//when scheduler gives cpu the process, its a context switch
      chosen->slice_ticks = 0;
      c->proc = chosen;
      swtch(&c->context, &chosen->context);
      c->proc = 0;
    }

    release(&chosen->lock);
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few paces where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->slice_ticks = 0;//reset slice when process voluntarily gives up cpu
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  p->slice_ticks = 0;

  //ECO METRICS UPDATE
  acquire(&tickslock);
  p->sleep_start_tick = ticks;
  release(&tickslock);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
        // wakeup(&ticks) is called while tickslock is already held
        // from clockintr(), so don't re-acquire it here.
        uint duration = ticks - p->sleep_start_tick;

        p->sleep_ticks += duration;
        p->wakeup_count++;
        if(duration <= 2)
          p->short_sleep_count++;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
