// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

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

//为每个cpu维护一个空闲链表
struct kmem {
  struct spinlock lock;
  struct run *freelist;
};
struct kmem kmems[NCPU];

// void
// kinit()
// {
//   initlock(&kmem.lock, "kmem");
//   freerange(end, (void*)PHYSTOP);
// }

void 
kinit()
{
  //push_off为禁用中断，pop_off为启用中断
  push_off();
  int id = cpuid();
  //给每个freelist都初始化一个锁并编号
  char lock_name[6] = {'k', 'm', 'e', 'm', (char)(id+48), '\0'};
  initlock(&(kmems[id].lock), lock_name);
  freerange(end, (void*)PHYSTOP);
  pop_off();
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
// void
// kfree(void *pa)
// {
//   struct run *r;

//   if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
//     panic("kfree");

//   // Fill with junk to catch dangling refs.
//   memset(pa, 1, PGSIZE);

//   r = (struct run*)pa;

//   acquire(&kmem.lock);
//   r->next = kmem.freelist;
//   kmem.freelist = r;
//   release(&kmem.lock);
// }

void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
 
  push_off();
  //获取当前cpu的id，对其空闲链表加锁
  int id = cpuid();
  acquire(&(kmems[id].lock));
  r->next = kmems[id].freelist;
  //把页加入本cpu的freelist
  kmems[id].freelist = r;
  //释放锁
  release(&(kmems[id].lock));
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// void *
// kalloc(void)
// {
//   struct run *r;

//   acquire(&kmem.lock);
//   r = kmem.freelist;
//   if(r)
//     kmem.freelist = r->next;
//   release(&kmem.lock);

//   if(r)
//     memset((char*)r, 5, PGSIZE); // fill with junk
//   return (void*)r;
// }

void * 
kalloc(void)
{
  struct run *r;
  push_off();
  int id = cpuid();
  //对当前freelist加锁
  acquire(&(kmems[id].lock));
  r = kmems[id].freelist;

  //判断自己是否有页
  if(r)
  {
    //如果还有空，就释放锁并返回空闲块
    kmems[id].freelist = r->next;
    release(&(kmems[id].lock));
  }
  // 如果自己没有，去其他CPU窃取
  else
  {
    //释放锁
    release(&(kmems[id].lock));
    for(int i = 0; i < NCPU; i++)
    {
      if(i == id)
        continue;
      //循环取出每个cpu的freelist
      acquire(&(kmems[i].lock));
      r = kmems[i].freelist;
      //如果非空，就释放锁并返回空闲块
      if(r)
      {
        kmems[i].freelist = r->next;
        release(&kmems[i].lock);
        break;
      }
      //释放当前cpu的锁，检查下一个
      release(&(kmems[i].lock));
    }
  }
  pop_off();
  // 将页面初始化
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

