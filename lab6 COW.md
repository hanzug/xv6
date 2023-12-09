# lab6 COW



[TOC]



## 实验内容

###### 为什么要实现COW?

实现一个内存的写时复制机制（copy-on-write fork），也称为 COW。为什么要实现这个功能呢，主要原因是：

> **在 shell 中执行指令时，首先会 fork 一个子进程，然后在子进程中使用 exec 执行 shell 中的指令。在这个过程中，fork 需要完整的拷贝所有父进程的地址空间，但在 exec 执行时，又会完全丢弃这个地址空间，创建一个新的，因此会造成很大的浪费。**

为了优化这个特定场景（fork 时）的内存利用率，我们可以在 fork 时，并不实际分配内存（与上一个实验的懒分配很像），而是让**子进程和父进程共享相同的内存区域（页表不同，但指向的物理地址相同）**。但为了保证进程之间的隔离性，我们不能同时对这块区域进行写操作，因此，**设置共享的内存地址只有读权限。当需要向内存页中写入数据时，会触发缺页中断，此时再拷贝一个内存页**，更新进程的页表，将内容写进去，然后重新执行刚才出错的指令。

在这个过程中，**需要为每个物理内存页保存一个指向该内存页的页表数量**。当为 0 时，表示没有进程使用该内存页，可以释放了；大于 1 时，每当有进程释放该内存页时，将对应的数值减一。

需要注意的是，这里要标记写入内存是 COW 场景。否则，如果真的有一个页面只能读不能写的话，就会出现问题。这里我们使用的是 PTE 页表项保留的标记位 RSW。

另一个知识点：**在XV6中，除了 trampoline page 外，一个物理内存 page 只属于一个用户进程**。

## 任务说明

有两个场景需要处理 cow 的写入内存页场景：

- 一个是用户进程写入内存，此时会触发 page fault 中断（15号中断是写入中断，只有这个时候会触发 cow，而13号中断是读页面，不会触发 cow）；
- 另一个是直接在内核状态下写入对应的内存，此时不会触发 usertrap 函数，需要另做处理。

总结起来，实验总共有以下四个步骤。

### 第一步，创建 page 的计数数组

首先对每个物理页面创建一个计数变量，保存在一个数组中，页面的数目就是数组的长度。这里有一个知识点：不是所有的物理内存都可以被用户进程映射到的，这里有一个范围，即 KERNBASE 到 PHYSTOP。具体映射可以从 xv6 手册中看到：

![img](https://raw.githubusercontent.com/hanzug/images/master/v2-d41da86fc6b32090dc8c824d364b958d_r.jpg)

va到pa的映射关系

由于一个页表的大小（PGSIZE）是 4096，因此数组的长度可以定义为：`(PHYSTOP - KERNBASE) / PGSIZE`

### implement copy-on-write

首先, 因为一个物理页有可能同时有多个进程的虚拟页指向它, 在**freepage**的时候需要考虑到这一点. 我们需要一个计数器, 只有当一个物理页的计数器降为**0**后才真正的释放它.

我们先以一个静态列表的形式存储计数器, 并对**kalloc**和**kfree**进行小修改.

```c
# kernel/kalloc.c

// reference count for each physical page to facilitate COW
#define PA2INDEX(pa) (((uint64)pa)/PGSIZE)

int cowcount[PHYSTOP/PGSIZE];

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    cowcount[PA2INDEX(p)] = 1; // 初始化的时候把每个物理页都加入freelist
    kfree(p);
  }
}

void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 需要加锁保证原子性
  acquire(&kmem.lock);
  int remain = --cowcount[PA2INDEX(pa)];
  release(&kmem.lock);

  if (remain > 0) {
    // 只有最后1个reference被删除时需要真正释放这个物理页
    return;
  }

  ...省略...
}

void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char *)r, 5, PGSIZE); // fill with junk
    int idx = PA2INDEX(r);
    if (cowcount[idx] != 0) {
      panic("kalloc: cowcount[idx] != 0");
    }
    cowcount[idx] = 1; // 新allocate的物理页的计数器为1
  }
  return (void*)r;
}

// helper函数
void adjustref(uint64 pa, int num) {
    if (pa >= PHYSTOP) {
        panic("addref: pa too big");
    }
    acquire(&kmem.lock);
    cowcount[PA2INDEX(pa)] += num;
    release(&kmem.lock);
}
```

如此一来, 我们就可以在**fork()**时, 不再额外分配复制物理页, 而是让子进程和父进程共享只读页面.

```c
# kernel/vm.c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    *pte &= ~PTE_W;  // 变为只读页面, 不允许写. 一旦试图写, 会触发num=15的trap
    flags = PTE_FLAGS(*pte);
    if(mappages(new, i, PGSIZE, pa, flags) != 0){
      goto err;
    }
    adjustref(pa, 1); // 增加计数器
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

然后, 当一个进程想要对于一个只读的**COW**页面进行修改时, 我们需要把这一页复制一遍赋给这个进程. 我们单独写一个模块化函数来处理这一步.

```c
# kernel/vm.c

int
cowalloc(pagetable_t pagetable, uint64 va) {
  if (va >= MAXVA) {
    printf("cowalloc: exceeds MAXVA\n");
    return -1;
  }

  pte_t* pte = walk(pagetable, va, 0); // should refer to a shared PA
  if (pte == 0) {
    panic("cowalloc: pte not exists");
  }
  if ((*pte & PTE_V) == 0 || (*pte & PTE_U) == 0) {
    panic("cowalloc: pte permission err");
  }
  uint64 pa_new = (uint64)kalloc();
  if (pa_new == 0) {
    printf("cowalloc: kalloc fails\n");
    return -1;
  }
  uint64 pa_old = PTE2PA(*pte);
  memmove((void *)pa_new, (const void *)pa_old, PGSIZE);
  kfree((void *)pa_old); // 减少COW页面的reference count
  *pte = PA2PTE(pa_new) | PTE_FLAGS(*pte) | PTE_W;
  return 0;
}
```

因为我们把共享页面的写权限给关闭了, 当任何一个进程试图去写在这个页面时, 会触发代码号为**15**的缺页中断. 我们可以处理这个异常时为进程复制一页这个共享物理页, 并允许写权限.

```c
# kernel/trap.c

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  ...省略...
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if (r_scause() == 15) {
    // 试图在一个COW只读页面上进行写操作, 为该进程额外分配复制一页
    if (cowalloc(p->pagetable, r_stval()) < 0) {
      p->killed = 1;
    }
  } else if((which_dev = devintr()) != 0){
      // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
  ...省略...
}
```

当这里为止, 我们已经可以通过很多测试了. 但实验手册也提示我们了, 还有一个小问题: **copyout**. 因为在呼叫**copyout**时, 我们处于内核态, 并不会触发**usertrap**. 我们其实是想要做出一样的操作: 当我们复制一些数据到用户态的某一页时, 如果那一页是共享COW页, 要额外进行复制操作. 如何知道一个页是共享的COW页? 根据我们以上的实现, **如果一个页的pte里的W写flag被关闭了, 那我们就可以认为它是一个共享COW页.**

```c
# kernel/vm.c
// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA) {
      printf("copyout: va exceeds MAXVA\n");
      return -1;
    }
    pte_t *pte = walk(pagetable, va0, 0);
    if (pte == 0 || (*pte & PTE_U) == 0 || (*pte & PTE_V) == 0) {
      printf("copyout: invalid pte\n");
      return -1;
    }
    if ((*pte & PTE_W) == 0) {
      // 写的目的地是COW共享页, 需要复制一份
      if (cowalloc(pagetable, va0) < 0) {
        return -1;
      }
    }
    ...省略...
  }
  return 0;
}
```

编译后进行lab批分, 顺利通过.

```bash
vagrant@developer:/vagrant/xv6-labs/xv6-labs-2020$ make grade
```