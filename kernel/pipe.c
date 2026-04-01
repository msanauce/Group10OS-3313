#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

#define PIPESIZE 512

struct pipe {
  struct spinlock lock;   // kept for pipeclose/free safety (read/write no longer use it)
  char data[PIPESIZE];
  uint nread;
  uint nwrite;
  int readopen;
  int writeopen;

  // ADDED: Peterson state (two participants: 0=writer, 1=reader)
  volatile int want[2];    // want[i]=1 means participant i requests the critical section
  volatile int turn;       // tie-break: if both want in, "turn" decides who waits
};

// ADDED: Peterson mutual exclusion for two participants.
// Note: Peterson provides mutual exclusion, not blocking sleep.
// If pipe is full/empty, we exit and yield() to let the other run.
static inline void
peterson_enter(struct pipe *pi, int me)
{
  int other = 1 - me;
  pi->want[me] = 1;        // request critical section
  pi->turn = other;        // if both request, allow other to win the tie
  while(pi->want[other] && pi->turn == other){
    yield();               // cooperative spin (replaces sleep)
  }
}

static inline void
peterson_exit(struct pipe *pi, int me)
{
  pi->want[me] = 0;        // release critical section
}

int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *pi;

  pi = 0;
  *f0 = *f1 = 0;
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;
  if((pi = (struct pipe*)kalloc()) == 0)
    goto bad;

  pi->readopen = 1;
  pi->writeopen = 1;
  pi->nwrite = 0;
  pi->nread = 0;

  // ADDED: initialize Peterson variables
  pi->want[0] = 0;
  pi->want[1] = 0;
  pi->turn = 0;

  initlock(&pi->lock, "pipe");

  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->pipe = pi;

  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->pipe = pi;

  return 0;

bad:
  if(pi)
    kfree((char*)pi);
  if(*f0)
    fileclose(*f0);
  if(*f1)
    fileclose(*f1);
  return -1;
}

void
pipeclose(struct pipe *pi, int writable)
{
  // Unchanged: keep lock here to avoid races with freeing pi.
  // Read/write use Peterson; close/free is still protected by the spinlock.
  acquire(&pi->lock);
  if(writable){
    pi->writeopen = 0;
    wakeup(&pi->nread);
  } else {
    pi->readopen = 0;
    wakeup(&pi->nwrite);
  }
  if(pi->readopen == 0 && pi->writeopen == 0){
    release(&pi->lock);
    kfree((char*)pi);
  } else {
    release(&pi->lock);
  }
}

int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  int i = 0;
  struct proc *pr = myproc();

  // CHANGED: removed acquire/release and sleep/wakeup in favor of Peterson + yield
  while(i < n){
    if(pi->readopen == 0 || killed(pr))
      return -1;

    // copy user byte outside critical section
    char ch;
    if(copyin(pr->pagetable, &ch, addr + i, 1) == -1)
      break;

    peterson_enter(pi, 0);                 // ADDED: writer enters CS (id 0)

    if(pi->readopen == 0){                 // re-check in CS
      peterson_exit(pi, 0);
      return -1;
    }

    if(pi->nwrite == pi->nread + PIPESIZE){ // full
      peterson_exit(pi, 0);                // ADDED: release CS before waiting
      yield();                             // ADDED: cooperative wait (no sleep)
      continue;
    }

    pi->data[pi->nwrite++ % PIPESIZE] = ch;

    peterson_exit(pi, 0);                  // ADDED: writer exits CS
    i++;
  }

  return i;
}

int
piperead(struct pipe *pi, uint64 addr, int n)
{
  int i = 0;
  struct proc *pr = myproc();

  // CHANGED: removed acquire/release and sleep/wakeup in favor of Peterson + yield
  while(i < n){
    if(killed(pr))
      return -1;

    peterson_enter(pi, 1);                 // ADDED: reader enters CS (id 1)

    if(pi->nread == pi->nwrite){           // empty
      if(pi->writeopen == 0){              // writer closed => EOF
        peterson_exit(pi, 1);
        break;
      }
      peterson_exit(pi, 1);                // ADDED: release CS before waiting
      yield();                             // ADDED: cooperative wait (no sleep)
      continue;
    }

    char ch = pi->data[pi->nread % PIPESIZE];
    pi->nread++;

    peterson_exit(pi, 1);                  // ADDED: reader exits CS

    if(copyout(pr->pagetable, addr + i, &ch, 1) == -1){
      if(i == 0)
        return -1;
      break;
    }

    i++;
  }

  return i;
}
