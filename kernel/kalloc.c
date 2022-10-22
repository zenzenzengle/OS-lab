// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
// 物理内存分配器，用于用户进程。
// 内核堆栈、页表页。
// 和管道缓冲区。分配整个4096字节的页面。


#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// 释放v所指向的物理内存页。
// 所指向的物理内存页，通常情况下，它应该由
// (例外情况是在初始化分配器时。
// 初始化分配器；见上面的 kinit)。
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // 填充垃圾，以捕捉悬空的裁判。
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 分配一个4096字节的物理内存页。
// 返回一个内核可以使用的指针。
// 如果内存不能被分配，则返回0。
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// for sysinfo
/**
 * 采用遍历链表的方式计算剩余表数，再计算内存空间   
 * @return 剩余的内存空间
 */
int cal_free_mem(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  int num = 0;
  while(r)
  {
    r = r -> next;
    num++;
  }
  release(&kmem.lock);
  
  return num*PGSIZE;
}
