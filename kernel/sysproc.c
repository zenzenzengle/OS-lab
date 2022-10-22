#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
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
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
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

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
// 返回从开始到现在发生了多少次时钟滴答声中断
// 从start开始
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

/**
 * 系统调用trace，为进程设定系统调用的输出，
 * 格式为【PID：sys_&{name}(arg0) -> return】
 * @return 失败: -1 成功: 0
 */
uint64
sys_trace(void)
{
 int mask;

    //没有接收到mask参数，返回-1
    if(argint(0, &mask) < 0)
      return -1;
    // 给进程设置mask
    struct proc *p = myproc();
    p->mask = mask;

    return 0;
}

/**
 * 系统调用打印进程相关信息，
 * 包括：剩余内存空间，剩余可使用的进程数，剩余文件描述符
 * @return 失败: -1 成功: 0
 */
uint64 sys_info(void)
{
  uint64 addr;
  if(argaddr(0, &addr)<0)
    return -1;
  struct proc *p = myproc();
  struct sysinfo info;

  info.freemem = cal_free_mem();
  info.nproc = count_unused_proc();
  info.freefd = count_remaining_fd();

// copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
// pagetable：指向进程的页文件
// dstva：目标位置起始地址
// src：待复制内容起始地址
// len：待复制字节数
  if(copyout(p->pagetable, addr, (char *)&info, 24) < 0)
    return -1;

  return 0;
}