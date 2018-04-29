#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#define DEBUG 0
#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))

void handle_sig(void);

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}


int
allocpid(void)
{
  int pid=nextpid;
  while(!cas(&nextpid,pid,pid+1))
    pid = nextpid;
  return pid;
}


//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;
  pushcli();
  do {
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      if(p->state == UNUSED)
        break;
    if (p == &ptable.proc[NPROC]) {
      popcli();
      return 0; // ptable is full
    }
  } 
  while(!cas(&p->state, UNUSED, EMBRYO));
  popcli();
  // return 0;

// found:
  // p->state = EMBRYO;
  // release(&ptable.lock);

  p->pid = allocpid();

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0) {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  for(int i = 0; i < SIG_NUM; i++)
    p->signalHandlers[i] = (sighandler_t) SIG_DFL;
  p->pendingSignals = 0;
  p->signalMask = 0;

  return p;
}


//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
}


// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0) {
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  //newly created procces inherits the signalMask
  np->pendingSignals = 0;
  np->signalMask = curproc->signalMask;
  for(int i = 0; i < SIG_NUM; i++)
    np->signalHandlers[i] = curproc->signalHandlers[i];



  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  //acquire(&ptable.lock);

  //np->state = RUNNABLE;
  cas(&np->state, EMBRYO, RUNNABLE);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  pushcli();//  acquire(&ptable.lock);
  cas(&curproc->state, RUNNING, NEG_ZOMBIE);

  // Parent might be sleeping in wait().
  //wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  //cas(&proc->state, RUNNING, NEG_ZOMBIE)
  //curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  pushcli();//acquire(&ptable.lock);
  for(;;){
    if (!cas(&curproc->state, RUNNING, NEG_SLEEPING)) {
      panic("scheduler: cas failed");
    }

    curproc->chan = curproc;    
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(cas(&p->state, ZOMBIE, UNUSED)) {
        // Found one.
        pid = p->pid;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->killed = 0;
        //freeproc(p);
        curproc->chan = 0;
        cas(&curproc->state, NEG_SLEEPING, RUNNING);
        // release(&ptable.lock);
        //cas(&p->state, NEG_UNUSED, UNUSED);
        popcli();
        return pid;
      }
    }
  
    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      curproc->chan = 0;

      cas(&curproc->state, NEG_SLEEPING, RUNNING);
      popcli();
      return -1;
    }


    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    //sleep(curproc, &ptable.lock);  //DOC: wait-sleep
    sched();
  }
}

void
freeproc(struct proc *p)
{
  if (!p || p->state != NEG_ZOMBIE)
    panic("freeproc not zombie");
  kfree(p->kstack);
  p->kstack = 0;
  freevm(p->pgdir);
  p->killed = 0;
  p->chan = 0;
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  //struct proc *curproc = myproc();

  c->proc = 0;
  //struct proc *curproc = myproc();

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.

    
    pushcli(); //acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(!cas(&p->state, RUNNABLE, RUNNING))
        continue;
      
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      //p->state = RUNNING;

      swtch(&(c->scheduler), (c->proc)->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      if(cas(&p->state, NEG_SLEEPING, SLEEPING)) {
        if(cas(&p->killed, 1, 0))
          p->state = RUNNABLE;
      }
      cas(&p->state, NEG_RUNNABLE, RUNNABLE);
      //if(p->state == NEG_ZOMBIE) {
       // freeproc(p);
      if(cas(&p->state, NEG_ZOMBIE, ZOMBIE))
        wakeup1(p->parent);
        /*
          if (!p || p->state != NEG_ZOMBIE)
            panic("freeproc not zombie");
          kfree(p->kstack);
          p->kstack = 0;
          freevm(p->pgdir);
          p->killed = 0;
          p->chan = 0;
       // freeproc(p);
        if(cas(&p->state, NEG_ZOMBIE, ZOMBIE))
          wakeup1(p->parent);*/
      //}
    }
    popcli(); //release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  //if(!holding(&ptable.lock))
    //panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{

  pushcli();//acquire(&ptable.lock);  //DOC: yieldlock
  struct proc *p = myproc();
  cas(&p->state, RUNNING, NEG_RUNNABLE);
  sched();
  popcli();//release(&ptable.lock);

}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.

  popcli();//release(&ptable.lock);
  if (first) {
    
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");
  pushcli();
  p->chan = chan;

  cas(&p->state, RUNNING, NEG_SLEEPING);


  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    //acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }


  sched();

  // Tidy up.
  //p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    //release(&ptable.lock);
    acquire(lk);
  }
  popcli();
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->chan == chan && (p->state == SLEEPING || p->state == NEG_SLEEPING)) {
      while(p->state == NEG_SLEEPING) {
        // busy-wait
      }
      if(cas(&p->state, SLEEPING, NEG_RUNNABLE)) {
        p->chan = 0;
        cas(&p->state, NEG_RUNNABLE, RUNNABLE);

      }
    }
  }

}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  pushcli();//acquire(&ptable.lock);
  wakeup1(chan);
  popcli();//release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid, int signum)
{
  struct proc *p;
  //cprintf("Entering kill!\n");
  //pushcli();//acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->pid == pid) {
      p->pendingSignals |= (1UL << signum);
     // p->killed = 1;
      //cas(&p->state, SLEEPING, RUNNABLE);
      return 0;
    }
  }
  //popcli();//release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [NEG_UNUSED] "neg_unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [NEG_SLEEPING]  "neg_sleep ",
  [RUNNABLE]  "runnable",
  [NEG_RUNNABLE]  "neg_runnable",
  [RUNNING]   "running",
  [ZOMBIE]    "zombie",
  [NEG_ZOMBIE]    "neg_zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

uint
sigprocmask(uint mask) {
  struct proc *p = myproc();
  uint tmp = p->signalMask;
  p->signalMask = mask;
  return tmp;
}

sighandler_t 
signal(int signum, sighandler_t handler) {
  struct proc *p = myproc();
  sighandler_t tmp = p->signalHandlers[signum];
  p->signalHandlers[signum] = handler;
  return *tmp; 
}

void
sigkill(void) {
  struct proc *p = myproc();
  //cprintf("SIGKILL!\n");
  p->killed = 1;
  cas(&p->state, SLEEPING, RUNNABLE);
  //p->pendingSignals &= ~(1UL << SIGKILL);
}


void 
sigret(void) {
  struct proc *p = myproc();
  p->tf->esp += sizeof(int) + (uint)&end_sigret - (uint)&start_sigret;
  memmove(p->tf, &p->userTFbackup, sizeof(struct trapframe));
  p->tf->esp += sizeof(struct trapframe); 
  p->signalMask = p->smBackup; 
  handle_sig();
}

void
sigcont(void) {
  cprintf("$$$$$$$$\n");
  struct proc *p = myproc();

  //p->pendingSignals &= ~(1UL << SIGSTOP);
  p->pendingSignals &= ~(1UL << SIGCONT);
}

void
sigstop(void) {
  //cprintf("starting to wait\n");
  struct proc *p = myproc();
  while(!(CHECK_BIT(p->pendingSignals,SIGCONT)))
    yield();
  //p->pendingSignals &= ~(1UL << SIGSTOP);
  //cprintf("ending the wait\n");
}

void use_custom_handler(int signum, struct proc *p) {
  cprintf("using custom handler!\n");

  //p->tf->esp -= sizeof(struct trapframe); 
  //p->userTFbackup = (struct trapframe*)p->tf->esp; 
  memmove((void*)p->userTFbackup, (void*)p->tf, sizeof(struct trapframe));//backing up trap frame
  
  p->tf->esp -= (uint)&end_sigret - (uint)&start_sigret;

  memmove((void*)p->tf->esp, start_sigret, (uint)&end_sigret - (uint)&start_sigret);
  *((int*)(p->tf->esp - 4)) = signum;
  //*((int*)(p->tf->esp - 8)) = p->tf->esp;
  p->tf->esp -= 4;
  p->tf->eip = (uint)p->signalHandlers[signum]; // trapret will resume into signal handler    
  cprintf("Custom handler: %d\n",p->signalHandlers[signum]);

}

void handle_sig(void) {
  struct proc *p = myproc();
  if(p == 0) 
    return;
  if((p->tf->cs&3) != DPL_USER)
    return;  
  if(p->pendingSignals == 0 || p->signalMask == ~0) 
    return;
  //cprintf("handle sig enter\n");
  
  uint pendingSignals = p->pendingSignals;

  //cprintf("SHIT!\n");

  for(int i = 0; i < 32; i++) {
    p->smBackup = p->signalMask;
    p->signalMask = 4294967295;

    if((!(CHECK_BIT(p->smBackup, i))) && (CHECK_BIT(pendingSignals, i))) {
      if((int)p->signalHandlers[i] == SIG_DFL) {
        switch(i) {
          case SIGKILL:
            sigkill();
          break;
          case SIGSTOP:
            sigstop();
          break;
          case SIGCONT:
            sigcont();
          break;
          default:
            sigkill();
          break;
        }
      }
      else if(!((int)p->signalHandlers[i] == SIG_IGN)) {
        memmove((void*)p->userTFbackup, (void*)p->tf, sizeof(struct trapframe));//backing up trap frame
        p->tf->esp -= (uint)&end_sigret - (uint)&start_sigret;
        memmove((void*)p->tf->esp, start_sigret, (uint)&end_sigret - (uint)&start_sigret);
        *((int*)(p->tf->esp - 4)) = i;
        //*((int*)(p->tf->esp - 8)) = p->tf->esp;
        p->tf->esp -= 4;
        p->tf->eip = (uint)p->signalHandlers[i]; // trapret will resume into signal handler    
      }
    }
    p->signalMask = p->smBackup;
    p->pendingSignals &= ~(1UL << i);
  }
}



/*
    }
    if (CHECK_BIT(pendingSignals, i )){
      if (!(CHECK_BIT(p->smBackup , i)) || (i == SIGCONT)){
        if ((int)p->signalHandlers[i] == SIG_DFL)
      }
    }


*/






/*

    if(((!(CHECK_BIT(p->smBackup, i))) || (i == SIGCONT)) && 
      (CHECK_BIT(pendingSignals, i)) && ((int)p->signalHandlers[i] == SIG_DFL)) {
     // if((pendingSignals >> (i) & 1) == SIG_DFL) {
        //cprintf("handelig a sig!\n");
        switch(i) {
          case SIGKILL:
            sigkill();
          break;
          case SIGSTOP:
            sigstop();
          break;
          case SIGCONT:
            sigcont();
          break;
          default:
            sigkill();
          break;
        //}
      }
    }
    if((!CHECK_BIT(p->smBackup, i)) && (CHECK_BIT(pendingSignals, i)) && ((int)p->signalHandlers[i] != SIG_IGN)) {
      use_custom_handler(i, p);
    }
    p->signalMask = p->smBackup;
    p->pendingSignals &= ~(1UL << i);
  } 
  //cprintf("restore mask\n");
}  
*/