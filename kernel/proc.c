#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void wakeup1(struct proc *chan);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// initialize the proc table at boot time.
//在启动时初始化进程表。
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");

      // Allocate a page for the process's kernel stack.
      // Map it high in memory, followed by an invalid
      // guard page.
      // 为进程的内核栈分配一个页面。
      // 将其映射到内存的高位，然后是一个无效的
      // 保护页。
      char *pa = kalloc();
      if(pa == 0)
        panic("kalloc");
      uint64 va = KSTACK((int) (p - proc));
      kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
      p->kstack = va;
  }
  kvminithart();
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
// 必须在禁用中断的情况下调用。
// 以防止与被转移到不同CPU的进程发生竞争。
// 进程的竞争。
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
// 返回这个CPU的cpu结构。
// 中断必须被禁用。
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
// 返回当前的struct proc *，如果没有，则返回0。
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
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
// 在进程表中寻找一个未使用的进程。
// 如果找到，初始化在内核中运行所需的状态。
// 并在p->lock的情况下返回。
// 如果没有空闲的proc，或者内存分配失败，返回0。
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

  // Allocate a trapframe page.
    //分配一个trapframe页面。
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  // 一个空的用户页表。
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  // 设置新的上下文，在forkret开始执行。
  // 返回到用户空间。
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
// 释放一个proc结构和挂在上面的数据。
// 包括用户页。
// p->lock必须被保留。
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
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
// 为一个给定的进程创建一个用户页表。
// 没有用户内存，但有trampoline页。
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  // 一个空的页表。
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  // 映射trampoline代码（用于系统调用返回）。
  // 在最高的用户虚拟地址上。
  // 只有主管使用它，在往返用户空间的路上。
  // 来往于用户空间，所以不是PTE_U。
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  // 映射TRAMPOLINE下面的陷阱框，为trampoline.S。
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
// 释放一个进程的页表，并释放
// 它所指向的物理内存。
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// 一个用户程序，调用exec("/init")。
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
// 设置第一个用户进程。
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  // 分配一个用户页，并将init的指令和数据复制到其中。
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  // 为从内核到用户的第一次 "返回 "做准备。
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
// 将用户内存增加或减少n个字节。
// 成功时返回0，失败时返回1。
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
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
// 创建一个新进程，复制父进程。
// 设置子内核堆栈，以便像fork()系统调用一样返回。
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.分配进程。
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  // 将用户内存从父代复制到子代。
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  np->parent = p;

  // 继承mask
  np->mask = p->mask;

  // copy saved user registers.
  // 复制已保存的用户寄存器。
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  // 导致fork在子程序中返回0。
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  // 在打开的文件描述符上增加引用计数。
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  np->state = RUNNABLE;

  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold p->lock.
// 将p的被抛弃的孩子传递给init。
// 调用者必须持有p->lock。
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    // this code uses pp->parent without holding pp->lock.
    // acquiring the lock first could cause a deadlock
    // if pp or a child of pp were also in exit()
    // and about to try to lock p.
    // 这段代码使用了pp->parent而没有持有pp->lock。
    // 如果pp或pp的一个子节点也在exit()中并试图锁定p, 
    // 先获取锁可能会导致死锁。
    if(pp->parent == p){
      // pp->parent can't change between the check and the acquire()
      // because only the parent changes it, and we're the parent.
      // pp->parent在检查和获取()之间不能改变。
      // 因为只有父级才会改变它，而我们是父级。
      acquire(&pp->lock);
      pp->parent = initproc;
      // we should wake up init here, but that would require
      // initproc->lock, which would be a deadlock, since we hold
      // the lock on one of init's children (pp). this is why
      // exit() always wakes init (before acquiring any locks).
      // 我们应该在这里唤醒init，但这将需要
      // initproc->lock，这将是一个死锁，因为我们持有init的一个子程序（pp）的锁。
      // 因为我们持有init的一个子程序（pp）上的锁。
      // exit()总是唤醒init（在获得任何锁之前）。
      release(&pp->lock);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
// 退出当前进程。 不返回。
// 已退出的进程仍处于僵尸状态
// 直到它的父进程调用wait()。
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  // 关闭所有打开的文件
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

  // we might re-parent a child to init. we can't be precise about
  // waking up init, since we can't acquire its lock once we've
  // acquired any other proc lock. so wake up init whether that's
  // necessary or not. init may miss this wakeup, but that seems
  // harmless.
  // 我们可能会把一个孩子重新归属于init。
  // 唤醒init，因为一旦我们获得了其他进程的锁，我们就无法获得它的锁。
  // 所以无论是否有必要，都要唤醒init。
  // init可能会错过这次唤醒，但这似乎是无害的。
  acquire(&initproc->lock);
  wakeup1(initproc);
  release(&initproc->lock);

  // grab a copy of p->parent, to ensure that we unlock the same
  // parent we locked. in case our parent gives us away to init while
  // we're waiting for the parent lock. we may then race with an
  // exiting parent, but the result will be a harmless spurious wakeup
  // to a dead or wrong process; proc structs are never re-allocated
  // as anything else.
  //抓取p->parent的一个副本，以确保我们解锁的是同一个parent。
  // 以防我们的父代在等待父代锁的时候把我们交给了init。
  // 我们可能会与一个正在退出的父节点竞争。
  // 我们可能会与一个退出的父进程进行竞赛，但结果将是一个无害的虚假唤醒
  // 但结果将是一个无害的虚假唤醒，即唤醒一个死亡或错误的进程；程序结构永远不会被重新分配。
  //像其他东西一样重新分配。
  acquire(&p->lock);
  struct proc *original_parent = p->parent;
  release(&p->lock);
  
  // we need the parent's lock in order to wake it up from wait().
  // the parent-then-child rule says we have to lock it first.
  // 我们需要父代的锁，以便从wait()中唤醒它。
  // 父-子规则说我们必须先锁定它。
  acquire(&original_parent->lock);

  acquire(&p->lock);

  // Give any children to init.
  // 给予任何孩子以init。
  reparent(p);

  // Parent might be sleeping in wait().
  // 父代可能在wait()中睡眠。
  wakeup1(original_parent);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&original_parent->lock);

  // Jump into the scheduler, never to return.
  // 跳入调度器，永远不会返回。
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
// 等待一个子进程退出并返回其pid。
// 如果这个进程没有子进程，则返回-1。
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  // hold p->lock for the whole time to avoid lost
  // wakeups from a child's exit().
  //一直保持p->lock，以避免因孩子的exit()而丢失唤醒。
  acquire(&p->lock);

  for(;;){
    // Scan through table looking for exited children.
    // 扫描表，寻找已退出的子进程
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      // this code uses np->parent without holding np->lock.
      // acquiring the lock first would cause a deadlock,
      // since np might be an ancestor, and we already hold p->lock.
      // 这段代码使用了np->parent而没有持有np->lock。
      // 先获得锁会导致死锁。
      // 因为np可能是一个祖先，而我们已经持有p->lock。
      if(np->parent == p){
        // np->parent can't change between the check and the acquire()
        // because only the parent changes it, and we're the parent.
        acquire(&np->lock);
        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&p->lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&p->lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    // 如果我们没有孩子，就没有必要等待。
    if(!havekids || p->killed){
      release(&p->lock);
      return -1;
    }
    
    // Wait for a child to exit.
    // 等待一个孩子退出。
    sleep(p, &p->lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
// 每一个CPU的进程调度器。
// 每个CPU在设置好自己后调用scheduler()。
// 调度器从不返回。 它循环运行，进行:
// - 选择一个要运行的进程。
// - 启动运行该进程的swtch。
// - 最终该进程将控制权
// 通过swtch回到调度器。
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    // 通过确保设备可以中断来避免死锁。
    intr_on();
    
    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        // 切换到选定的进程。 该进程的工作是
        // 释放它的锁，在跳回我们这里之前重新获得它。
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        // 进程现在已经完成了运行。
        // 在回来之前，它应该已经改变了它的p->state。
        c->proc = 0;

        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      intr_on();
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
// 切换到调度器。 必须只持有p->lock并且已经改变了proc->state。
// 保存和恢复 intena，因为 intena 是这个内核线程的属性，而不是这个 CPU 的属性。
// 它应该是proc->intena和proc->noff，
// 但这在少数持有锁但没有进程的地方会被破坏。
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
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
// 放弃CPU的一个调度轮次。
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
// 一个fork的第一次调度由scheduler()进行。
//将切换到forkret。
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  //仍然持有来自调度器的p->锁。
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    // 文件系统的初始化必须在一个普通进程的上下文中运行（例如，因为它调用了睡眠）。
    // 常规进程中运行（例如，因为它调用了睡眠），因此不能从main()中运行。
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
// 原子化地释放锁，并在Chan上睡觉。
// 在被唤醒时重新获得锁。
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
  // 必须获得p->锁，以便
  // 改变p->状态，然后再调用sched。
  // 一旦我们持有p->锁，我们就可以
  // 保证我们不会错过任何唤醒的机会
  // (wakeup锁住p->lock)。
  // 所以释放lk也是可以的。
  if(lk != &p->lock){  //DOC: sleeplock0
    acquire(&p->lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &p->lock){
    release(&p->lock);
    acquire(lk);
  }
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
// 唤醒所有在chan上睡觉的进程。
// 必须在没有任何p->lock的情况下调用。
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
    }
    release(&p->lock);
  }
}

// Wake up p if it is sleeping in wait(); used by exit().
// Caller must hold p->lock.
// 唤醒p，如果它在wait()中沉睡；被exit()使用。
// 调用者必须持有p->lock。
static void
wakeup1(struct proc *p)
{
  if(!holding(&p->lock))
    panic("wakeup1");
  if(p->chan == p && p->state == SLEEPING) {
    p->state = RUNNABLE;
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
// 杀死具有给定pid的进程。
// 受害者将不会退出，直到它试图返回
// 回到用户空间（见 trap.c 中的 usertrap()）。
int
kill(int pid)
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

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
// 拷贝到用户地址，或者内核地址。
// 取决于usr_dst。
// 成功时返回0，错误时返回1。
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
// 从用户地址或内核地址复制。
// 取决于usr_src。
// 成功时返回0，错误时返回1。
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
// 打印一个进程列表到控制台。 用于调试。
// 当用户在控制台输入^P时运行。
// 没有锁，以避免进一步楔入卡住的机器。
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
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

/**
 * 统计剩余未使用进程数
 * @return 剩余未使用进程数
 */
int count_unused_proc(void)
{
  struct proc *p;
  int count = 0;
  for(p = proc; p < &proc[NPROC]; p++)
  {
    if(p->state == UNUSED)
      count++;
  }

  return count;
}

/**
 * 统计当前进程还未使用的文件描述符的数量
 * @return 当前进程还未使用的文件描述符的数量
 */
int count_remaining_fd(void)
{
  struct proc *p = myproc();
  int i;
  int count = 0;

  for(i = 0; i < NOFILE; i++)
    if(!p->ofile[i])
      count++;
  
  return count;
}
