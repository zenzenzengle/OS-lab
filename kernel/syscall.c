#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
// 从当前进程中获取addr处的uint64。
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz)
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
// 从当前进程中获取addr处以nul结尾的字符串。
// 返回字符串的长度，不包括nul，或者-1表示错误。
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  int err = copyinstr(p->pagetable, buf, addr, max);
  if(err < 0)
    return err;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
// 获取第n个32位系统调用参数。
int
argint(int n, int *ip)
{
  *ip = argraw(n);
  return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
// 检索一个作为指针的参数。
// 不检查合法性，因为
// copyin/copyout会做这个。
int
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
// 获取第n个字大小的系统调用参数，作为一个空尾的字符串。
// 复制到buf中，最多可复制到最大。
// 如果OK（包括nul）则返回字符串长度，如果错误则返回-1。
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  if(argaddr(n, &addr) < 0)
    return -1;
  return fetchstr(addr, buf, max);
}

/**
 * 定义了一大段看起来就和系统调用有关的部分
 * 以下为用extern 进行标识的函数接口
 * 实际上声明了这些函数，这些函数的实现不必在这个文件中，
 * 而是分布在各个相关的代码文件中
 * （一般放在sys开头的文件中，包括sysproc.c与sysfile.c），
 * 我们在这些代码文件中实现好对应的函数，
 * 最后就可以编译出对应名字的汇编代码函数， 
 * extern 就会找到对应的函数实现了。*/

extern uint64 sys_chdir(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_exit(void);
extern uint64 sys_fork(void);
extern uint64 sys_fstat(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_mknod(void);
extern uint64 sys_open(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_unlink(void);
extern uint64 sys_wait(void);
extern uint64 sys_write(void);
extern uint64 sys_uptime(void);
extern uint64 sys_trace(void);
extern uint64 sys_info(void);

/**以下为以syscall为名的数组。
 * 将这些函数的指针都放在统一的数组里，
 * 并且数组下标就是系统调用号，
 * 这样我们在分辨不同系统调用的时候就可很方便地用数组来进行操作了。
 * kernel/syscall.c中的syscall()函数就
 * 根据这一方法实现了系统调用的分发
 * （通过不同系统调用号调用不同系统调用函数），
 * 请仔细阅读并尝试理解。
*/
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_trace]   sys_trace,
[SYS_sysinfo] sys_info
};

/**
 * 将两者合起来使用，
 * 可以使得 系统调用的实现 和 系统调用的分发 彼此分离，
 * 这对函数编写者非常友好，但是会让初学者有些迷惑，这是需要注意的。
*/

char* syscall_name[SYS_CALL_AMOUNT] = {
  "sys_fork",
  "sys_exit",
  "sys_wait",
  "sys_pipe",
  "sys_read",
  "sys_kill",
  "sys_exec",
  "sys_fstat",
  "sys_chdir",
  "sys_dup",
  "sys_getpid",
  "sys_sbrk",
  "sys_sleep",
  "sys_uptime",
  "sys_open",
  "sys_write",
  "sys_mknod",
  "sys_unlink",
  "sys_link",
  "sys_mkdir",
  "sys_close",
  "sys_trace",
  "sys_info"
};


void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    int arg;
    argint(0, &arg);
    p->trapframe->a0 = syscalls[num]();
    // for trace
    int mask = p->mask;
    // 判断mask的系统调用对应位是否为1
    if((mask>>num) & 1)
    {
      printf("%d: %s(%d) -> %d\n", p->pid, syscall_name[num-1], arg, p->trapframe->a0); 
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
