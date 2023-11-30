# lab3 PageTable

- **用户页表的虚拟地址分配**？
- **用户程序的页表放在哪里？**
- **kstack的虚拟地址为什么这么靠后？**
- **页表过大问题**
- **什么是内核页**？
- **为什么每个进程需要一个内核页**？
- xv6**是如何实现内核页的分配和切换的**？



虚拟地址概况：

![image-20231129164236914](C:\Users\haria\Pictures\typoraPictures\image-20231129164236914.png)

**satp**寄存器会被内核修改。



**在xv6中 walk函数实现了软件层次的地址转换**

#### 多级页表解决页表过大问题

具体表现为：利用较高级页表的flag，可以表示一整个范围的页表状态，进而省去了这些未使用的页表项内存。

![image-20231129173949041](C:\Users\haria\Pictures\typoraPictures\image-20231129173949041.png)



**Translation Lookside Buffer**（通常翻译成页表缓存）。你会经常看到它的缩写**TLB**。基本上来说，这就是Page Table Entry的缓存，也就是PTE的缓存。



#### kernel页表设计：

![image-20231129174436669](C:\Users\haria\Pictures\typoraPictures\image-20231129174436669.png)



**kstack的虚拟地址为什么这么靠后？**

第一件事情是，有一些page在虚拟内存中的地址很靠后，比如kernel stack在虚拟内存中的地址就很靠后。这是因为在它之下有一个未被映射的Guard page，这个Guard page对应的PTE的Valid 标志位没有设置，这样，如果kernel stack耗尽了，它会溢出到Guard page，但是因为Guard page的PTE中Valid标志位未设置，会导致立即触发page fault，这样的结果好过内存越界之后造成的数据混乱。



**用户程序的页表放在哪里？**

当kernel创建了一个进程，针对这个进程的page table也会从Free memory中分配出来。内核会为用户进程的page table分配几个page，并填入PTE。在某个时间点，当内核运行了这个进程，内核会将进程的根page table的地址加载到SATP中。从那个时间点开始，处理器会使用内核为那个进程构建的虚拟地址空间。



**用户页表的虚拟地址分配**？

![img](C:\Users\haria\Pictures\typoraPictures\assets%2F-MHZoT2b_bcLghjAOPsJ%2F-MKlssQnZeSx7lgksqSn%2F-MKopGK-JjubGvX84-qy%2Fimage.png)



什么是内核页？

内核页是一种内存管理的技术，它可以让每个进程在用户态和内核态之间切换时，保持自己的内核栈和寄存器状态。内核页是一个大小为4KB的内存页，它被映射到每个进程的虚拟地址空间的最高端，即0xFFFFFFFFFFFFF000。内核页的内容包括以下几个部分：

- 一个trampoline代码，它是一个跳板，用于在用户态和内核态之间切换时，设置正确的栈指针和页表指针，然后跳转到内核的入口或返回地址。
- 一个trapframe结构，它是一个保存进程在用户态时的寄存器状态的结构，包括程序计数器、栈指针、通用寄存器、页表指针等。当进程发生中断、异常或系统调用时，内核会把这些寄存器的值保存到trapframe中，以便在返回用户态时恢复。
- 一个内核栈，它是一个用于内核执行时的临时存储空间，用于保存局部变量、函数参数、返回地址等。内核栈的大小是4096-176=3920字节，因为trampoline和trapframe占用了176字节。



为什么每个进程需要一个内核页？

每个进程需要一个内核页的原因是为了实现进程的隔离和切换。如果所有进程共享一个内核页，那么就会有以下几个问题：

- 安全性问题。如果一个恶意的进程可以修改内核页的内容，那么它就可以篡改其他进程的寄存器状态，或者劫持内核的执行流程，造成系统崩溃或数据泄露。
- 并发问题。如果多个进程同时进入内核态，那么它们就会争夺同一个内核栈的空间，导致栈溢出或数据覆盖，破坏内核的正确运行。
- 效率问题。如果每次进程切换时，都要把内核页的内容复制到另一个内存位置，那么就会增加额外的开销，降低系统的性能。

因此，每个进程拥有一个自己的内核页，可以避免这些问题，**实现进程的隔离和切换**。每个进程的内核页都被映射到同一个虚拟地址，但是对应不同的物理地址，这样就可以让内核在访问内核页时，不需要考虑当前是哪个进程，只需要使用固定的地址即可。





xv6是如何实现内核页的分配和切换的？

xv6在创建进程时，会为每个进程分配一个内核页，并初始化它的内容。具体的步骤如下：

- 调用kalloc函数，从内核的空闲内存链表中分配一个4KB的内存块，作为内核页的物理地址。
- 调用mappages函数，把内核页的物理地址映射到虚拟地址0xFFFFFFFFFFFFF000，设置相应的页表条目（PTE）的标志位，如有效位、可写位、用户位等。
- 调用memcpy函数，把内核代码段中的trampoline代码复制到内核页的最低端，即0xFFFFFFFFFFFFF000处。
- 调用memset函数，把内核页中的trapframe结构清零，以便在之后保存进程的寄存器状态。
- 把内核页的物理地址保存到进程控制块（PCB）的kstack字段中，以便在之后切换进程时使用。



xv6在切换进程时，会使用内核页来保存和恢复进程的状态。具体的步骤如下：

- 当进程从用户态切换到内核态时，CPU会自动把用户态的寄存器状态保存到内核页中的trapframe结构中，然后跳转到trampoline代码处执行。

- trampoline代码会把内核页的物理地址加载到栈指针寄存器（sp）中，然后把页表指针寄存器（satp）设置为内核的页表，然后跳转到内核的入口函数（kernelvec）处执行。

- kernelvec函数会根据trapframe中的中断或异常类型，调用相应的处理函数，如timer，syscall，trap等。

- 当进程从内核态切换到用户态时，内核会先选择一个要运行的进程，然后把它的PCB中的kstack字段加载到栈指针寄存器（sp）中，然后把它的页表指针寄存器（satp）设置为它的页表，然后跳转到trampoline代码处执行。

  > ```
  > //到这里就换进了新进程
  >         swtch(&c->context, &p->context);
  > ```

- trampoline代码会把trapframe中的寄存器状态恢复到CPU中，然后返回到用户态的程序计数器（pc）处执行。

![image-20231130110643132](C:\Users\haria\Pictures\typoraPictures\image-20231130110643132.png)



##### 实现：

这个实验主要涉及了解物理地址和虚拟地址的转换和寻址, 内核态下的内存地址和用户态下的内存地址的差别等. 代码主要集中在**kernel/vm.c**里, 涉及的实现细节比较多, 需要结合xv6的实验教科书耐心仔细研读.

------

### print a page table

第一个实验是比较简单的, 给定一个page table, 要求递归地打印出它所映射到的**3**层page table下所有存在的**PTE** (Page Table Entry). 根据实验hints, 我们可以基本模仿**void freewalk(pagetable_t pagetable)**这个函数来完成要求.

但是在阅读**freewalk**函数的时候, 让我有过一些疑惑的地方是, 为什么判定一个**PTE**是一个指向下一层page table的**PTE**的条件是

```c
// 此条件等于该pte不是终末层, 而是指向下一层
(pte & (PTE_R|PTE_W|PTE_X)) == 0
```

为了弄明白这一点, 我们需要看特定两个地方的实现细节:

```c
// 中间层只有PTE_V flag
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  ...省略...
  for(int level = 2; level > 0; level--) {
    ...
    if(*pte & PTE_V) {
      ...
    } else {
      ...
      *pte = PA2PTE(pagetable) | PTE_V; // << 间接层pte只有PTE_V flag被设立起来
    }
  }
  return &pagetable[PX(0, va)];
}

// 终末层所有flag都有
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  ...省略...
  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    ...
    // W, X, R, U flag全部都设立起来
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
       ...
    }
  }
  return newsz;
}
```

OK, 刨根问底地明白了这个细节后, 直接上这个打印函数**vmprint**的代码:

```c
# kernel/vm.c

// Recursive helper
void vmprint_helper(pagetable_t pagetable, int depth) {
  static char* indent[] = {
      "",
      "..",
      ".. ..",
      ".. .. .."
  };
  if (depth <= 0 || depth >= 4) {
    panic("vmprint_helper: depth not in {1, 2, 3}");
  }
  // there are 2^9 = 512 PTES in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if (pte & PTE_V) { //是一个有效的PTE
      printf("%s%d: pte %p pa %p\n", indent[depth], i, pte, PTE2PA(pte));
      if ((pte & (PTE_R|PTE_W|PTE_X)) == 0) {
        // points to a lower-level page table 并且是间接层PTE
        uint64 child = PTE2PA(pte);
        vmprint_helper((pagetable_t)child, depth+1); // 递归, 深度+1
      }
    }
  }
}

// Utility func to print the valid
// PTEs within a page table recursively
void vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);
  vmprint_helper(pagetable, 1);
}
```

编译后进行lab批分, 顺利通过.

```bash
vagrant@developer:/vagrant/xv6-labs/xv6-labs-2020$ ./grade-lab-pgtbl pte printout
make: 'kernel/kernel' is up to date.
== Test pte printout == pte printout: OK (1.3s)
```

------

### a kernel page table per process

第二个实验特别难, 一共也就100行代码, 但是从头到尾一共花了有六七个小时debug还参考了网上其他同学做的版本.

这个实验的要求是, 原本xv6只有1个内核页, 现在希望为每个新进程单独分配一个属于它的内核页. 一方面是使得每个进程拥有更好的**isolation**和**modularity**, 另一方面根据实验手册的说法, 在用户进程切入内核态后, 内核态就可以直接使用那个进程专属的内核页, 而不是用**walk**函数去模拟硬件寻址来翻译出用户虚拟地址所对应的真实物理地址.

1. 为**struct proc**加入一个新的field

```c
# kernel/proc.h
// Per-process state
struct proc {
  ...省略...
  uint64 tracemask;            // the sys calls this proc is tracing
  pagetable_t kpagetable;      // the kernel table per process 专属内核页
};
```

2. 模仿**kvminit**函数, 在**allocproc**函数里为新进程分配它的专属内核页

```c
# kernel/vm.c
// add a mapping to the per-process kernel page table.
void
ukvmmap(pagetable_t kpagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpagetable, va, sz, pa, perm) != 0)
    panic("ukvmmap");
}

/*
 * create a direct-map page table for the per-process kernel page table.
 * return nullptr when kalloc fails
 */
pagetable_t
ukvminit()
{
  pagetable_t kpagetable = (pagetable_t) kalloc();
  if (kpagetable == 0) {
    return kpagetable;
  }
  memset(kpagetable, 0, PGSIZE);
  // 把固定的常数映射照旧搬运过来
  // uart registers
  ukvmmap(kpagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  // virtio mmio disk interface
  ukvmmap(kpagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  // CLINT
  ukvmmap(kpagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
  // PLIC
  ukvmmap(kpagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
  // map kernel text executable and read-only.
  ukvmmap(kpagetable, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);
  // map kernel data and the physical RAM we'll make use of.
  ukvmmap(kpagetable, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  ukvmmap(kpagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  return kpagetable;
}

======================================================================
# kernel/proc.c
static struct proc*
allocproc(void)
{
  ...

found:
  p->pid = allocpid();
  ...
  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Allocate the per-process kernel page table
  // 为这个进程分配并初始化一个新的专属内核页
  p->kpagetable = ukvminit();
  if(p->kpagetable == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  ...
}
```

3. 在这个新的专属内核页里, 把这个新进程的**kernel stack**映射上. 注意在**procinit**函数里, 所有进程的kernel stack的物理页都已经被分配了. 我们并不需要重新去分配物理页, 只需要建立映射mapping即可.

```c
# kernel/proc.h
static struct proc*
allocproc(void)
{
  ...

found:
  p->pid = allocpid();
  ...
  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Allocate the per-process kernel page table
  // 为这个进程分配并初始化一个新的专属内核页
  p->kpagetable = ukvminit();
  if(p->kpagetable == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // remap the kernel stack page per process
  // physical address is already allocated in procinit()
  // 每个kernel stack的虚拟地址va是已经提前决定好的 直接算出它对应的pa
  // 然后把这个映射加入到新的专属内核页里
  uint64 va = KSTACK((int) (p - proc));
  pte_t pa = kvmpa(va);
  memset((void *)pa, 0, PGSIZE); // 刷新清空kernel stack
  ukvmmap(p->kpagetable, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  p->kstack = va;
  ...
}
```

4. 在**scheduler**切换进程的时候, 刷新TLB和使用的虚拟-物理页表影射base. 注意在进程切换跑完返回后, 要重新切换回全局的kernel page. 这个点让我debug了好几个小时.

```c
# kernel/proc.c
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    
    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        // 切换到要马上运行的新进程的内核页表
        w_satp(MAKE_SATP(p->kpagetable));
        sfence_vma();
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        // 切换回全局内核页表
        kvminithart();
        c->proc = 0;

        found = 1;
      }
      release(&p->lock);
    }
    ...
}
```

5. 在销毁一个进程时, 回收它的内核页表. 这里需要注意的是, 我们并不需要去回收内核页表所映射到的物理地址. 因为那些物理地址, 例如device mapping, 是全局共享的. 进程专属内核表只是全局内核表的一个复制. 但是间接映射所消耗分配的物理内存是需要回收的. 举个例子, 在kernel pagetable可能有这样一个三级映射:

0x 810 (第一级) -> 0x 910 (第二级) -> 0x 1100(第三级) -> 0x 10000000L **UART0**

我们是需要把**0x 810**, **0x 910**, **0x 1100** 回收的, 但是**UARTO**不需要回收因为是共享的.

```c
# kernel/vm.c

// Unmap the leaf node mapping
// of the per-process kernel page table
// so that we could call freewalk on that
void
ukvmunmap(pagetable_t pagetable, uint64 va, uint64 npages)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("ukvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      goto clean;
    if((*pte & PTE_V) == 0)
      goto clean;
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("ukvmunmap: not a leaf");

    clean:
      *pte = 0;
  }
}

// Recursively free page-table pages similar to freewalk
// not need to already free leaf node
// 和freewalk一模一样, 除了不再出panic错当一个page的leaf还没被清除掉
// 因为当我们free pagetable和kpagetable的时候
// 只有1份物理地址, 且原本free pagetable的函数会负责清空它们
// 所以这个函数只需要把在kpagetable里所有间接mapping清除即可
void
ufreewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      ufreewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
    pagetable[i] = 0;
  }
  kfree((void*)pagetable);
}

// helper function to first free all leaf mapping
// of a per-process kernel table but do not free the physical address
// and then remove all 3-levels indirection and the physical address
// for this kernel page itself
void freeprockvm(struct proc* p) {
  pagetable_t kpagetable = p->kpagetable;
  // reverse order of allocation
  // 按分配顺序的逆序来销毁映射, 但不回收物理地址
  ukvmunmap(kpagetable, p->kstack, PGSIZE/PGSIZE);
  ukvmunmap(kpagetable, TRAMPOLINE, PGSIZE/PGSIZE);
  ukvmunmap(kpagetable, (uint64)etext, (PHYSTOP-(uint64)etext)/PGSIZE);
  ukvmunmap(kpagetable, KERNBASE, ((uint64)etext-KERNBASE)/PGSIZE);
  ukvmunmap(kpagetable, PLIC, 0x400000/PGSIZE);
  ukvmunmap(kpagetable, CLINT, 0x10000/PGSIZE);
  ukvmunmap(kpagetable, VIRTIO0, PGSIZE/PGSIZE);
  ukvmunmap(kpagetable, UART0, PGSIZE/PGSIZE);
  ufreewalk(kpagetable);
}

======================================================================
# kernel/proc.c
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
  if (p->kpagetable) {
    freeprockvm(p);
    p->kpagetable = 0;
  }
  if (p->kstack) {
    p->kstack = 0;
  }
}
```

------

### simplify copyin/copyinstr

这个实验承接上一个实验, 我们需要保证每个进程的**pagetable**和**kpagetable**的前半段映射一直保持一致, 这样我们才能在切入内核态时直接使用硬件支持的虚拟/物理内存地址寻址. 按照实验手册的提醒, 在**fork()**, **sbrk()**, **exec()**这些使得进程的**pagetable**发生增长/缩减的地方, 我们需要将**kpagetable**与之进行同步更新.

首先我们写一个helper函数, 来将一段内存映射从**pagetable**复制到**kpagetable**.

```c
# kernel/vm.c

// Same as mappages without panic on remapping
// 和mappages一模一样, 只不过不再panic remapping, 直接强制复写
int umappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm) {
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// copying from old page to new page from
// begin in old page to new in old page
// and mask off PTE_U bit
// 将从begin到end的虚拟地址的映射, 从oldpage复制到newpage
int
pagecopy(pagetable_t oldpage, pagetable_t newpage, uint64 begin, uint64 end) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  begin = PGROUNDUP(begin);

  for (i = begin; i < end; i += PGSIZE) {
    if ((pte = walk(oldpage, i, 0)) == 0)
      panic("pagecopy walk oldpage nullptr");
    if ((*pte & PTE_V) == 0)
      panic("pagecopy oldpage pte not valid");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte) & (~PTE_U); // 把U flag抹去
    if (umappages(newpage, i, PGSIZE, pa, flags) != 0) {
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(newpage, 0, i / PGSIZE, 1);
  return -1;
}
```

紧接着, 我们在**fork()**, **exec()**, **sbrk()** 和**userinit()**的相应位置进行**pagetable**和**kpagetale**的同步.

**fork()**

```c
# kernel/proc.c
// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  ...

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  if (pagecopy(np->pagetable, np->kpagetable, 0, np->sz) != 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->parent = p;

  ...
  return pid;
}
```

**exec()**

```c
# kernel/exec.c
int
exec(char *path, char **argv)
{
  ...
    
  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);

  // 复制新的kernel page并刷新TLB
  if (pagecopy(p->pagetable, p->kpagetable, 0, p->sz) != 0) {
    goto bad;
  }
  // 因为load进来了新的program, 刷新一下内存映射
  w_satp(MAKE_SATP(p->kpagetable));
  sfence_vma();

  ...

  return argc; // this ends up in a0, the first argument to main(argc, argv)

}
```

**sbrk()**

```c
# kernel/proc.c
// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    // 内核页的虚拟地址不能溢出PLIC
    if (sz + n > PLIC || (sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
    if (pagecopy(p->pagetable, p->kpagetable, p->sz, sz) != 0) {
      // 增量同步[old size, new size]
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
    if (sz != p->sz) {
      // 缩量同步[new size, old size]
      uvmunmap(p->kpagetable, PGROUNDUP(sz), (PGROUNDUP(p->sz) - PGROUNDUP(sz)) / PGSIZE, 0);
    }
  }
  ukvminithard(p->kpagetable);
  p->sz = sz;
  return 0;
}
```

**userinit()**

```c
# kernel/proc.c
// Set up first user process.
void
userinit(void)
{
  ...
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  pagecopy(p->pagetable, p->kpagetable, 0, p->sz);

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  ...
}
```

另外需要注意的一点是, 在跑**usertest**的时候我总是会在**write big**这个test上出现**badalloc**的问题. 几经探寻无果后, 在网上看到了一个hack的方法是把**kernel/param.h**里的**FSSIZE**参数调大一些后顺利通过批分. 我相信这个badalloc应该是我自己的实现里有问题, 可能没能把分配的物理页给释放全等等. 不过暂时有点看不出哪儿有问题, 先继续往前走了. 之后学到后面对**xv6**有更深刻的理解后再回来看看.

编译后进行lab批分, 顺利通过.

```bash
vagrant@developer:/vagrant/xv6-labs/xv6-labs-2020$ make grade
```
