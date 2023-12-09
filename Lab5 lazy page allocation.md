# Lab5 lazy page allocation



[TOC]

​	针对page table使用的一个优化. 在用户程序申请更多的内存时, 并不立刻分配物理地址, 而是在之后当它们真的需要使用这些地址并产生**page fault interrup**t(缺页中断)时再进行分配.

 

首先：

### 异常号

​	异常号是由处理器发出，发给操作系统的。异常号是一种用来表示异常原因和类型的数字，异常是指由指令执行过程中发生的错误或非法操作引起的中断，例如除零、缺页、保护错误等。当处理器检测到异常时，它会将异常号保存在scause寄存器中，然后跳转到操作系统定义的异常处理程序，根据异常号进行相应的处理。

![img](https://raw.githubusercontent.com/hanzug/images/master/v2-d41da86fc6b32090dc8c824d364b958d_r.jpg)

   **本次lab基于13（Load page fault）和 15（store page fault）来作为信号驱动。** 



![image-20231204201431661](https://raw.githubusercontent.com/hanzug/images/master/image-20231204201431661.png)

## Part1：eliminate allocation from sbrk()



**sbrk()是系统调用，内部调用内核函数 kalloc()**

sbrk函数主要用于实现free，malloc。

作为实验的第一步, 我们需要修改一下**sbrk()**函数:

 在用户申请内存空间时, 只是"虚假"地增加这个进程的大小而并不去分配物理页, 为了之后用户在真正使用这个地址时, 可以发生缺页中断来让内核介入进行实际的页分配.

```c
# kernel/sysproc.c

uint64
sys_sbrk(void)
{
  int addr;
  int n;
  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  myproc()->sz += n;  // 虚假地增加size而不物理分配
//  if(growproc(n) < 0)
//    return -1;
  return addr;
}
```

## Part2: lazy allocation

第二步, 我们要在**usertrap**函数里加入额外的处理逻辑来应对缺页中断异常(**13**号或**15**号). 注意与课程视频里教授给出的基本代码不同的是, 由于在**lab3**里我们为每个进程都引入了独立的内核页, 且内核页从**地址0**到**地址PLIC**是和进程的**pagetable**同步的, 我们在分配完一个物理页后, 需要进行2次**mappages**操作.

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

  } else if (r_scause() == 13 || r_scause() == 15) {
    // 13 is read page fault, 15 is write page fault
    printf("page fault trap: signal %d at address %p\n", r_scause(), r_stval());
    uint64 va = PGROUNDDOWN(r_stval()); // beginning of a 4KB page
    uint64 pa = (uint64) kalloc();
    if (pa == 0) {
      p->killed = 1;
    } else {
      memset((void *)pa, 0, PGSIZE);
      if (umappages(p->pagetable, va, PGSIZE, pa, PTE_W|PTE_X|PTE_R|PTE_U) != 0) {
        kfree((void *)pa);
        p->killed = 1;
      }
      if (p->killed == 0) {
        // 内核页没有User flag
        if (umappages(p->kpagetable, va, PGSIZE, pa, PTE_W | PTE_X | PTE_R) != 0) {
          kfree((void *)pa);
          uvmunmap(p->pagetable, va, 1, 0); // 把在pagetable里已经建立的间接mapping删除
          p->killed = 1;
        }
      }
    }
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  ...省略...

  usertrapret();
  return;
}
```

另外需要注意在**kernel/vm.c**的一些函数里, 去掉一些**panic**操作. 涉及的地方比较繁杂, 就不一一列举了. 跑代码的时候遇到了就想一下, 如果是由于我们对**xv6**进行的改动而合理的现象, 就可以把**panic**语句那一段直接改成**continue**即可. 在这里, 我们主要是改了**uvmunmap**里的**walk**和**remap**的**panic.**

## Part 3: lazytests and usertests

在这最后一步中, 我们需要把之前的初级版本**lazy allocation**的各项功能完善并加上边界检查等细节.

首先我们把**lazy allocation**的函数单独抽出来, 作更好的模块化处理.

```c
# kernel/vm.c
// validate and potentially allocate physical page
// for a process's virtual address
// return 0 on OK, -1 if any error
int lazyvalidate(struct proc* p, uint64 va) {
  if (va > p->sz || va < p->trapframe->sp) {
      // invalid address, 超出了栈边界或进程大小
      return -1;
  }
  if (walkaddr(p->pagetable, va) != 0) {
      return 0; // already mapped 已有物理页映射
  }
  va = PGROUNDDOWN(va); // beginning of a 4KB page
  uint64 pa = (uint64) kalloc();
  if (pa == 0) {
    return -1;
  }
  memset((void *)pa, 0, PGSIZE);
  if (umappages(p->pagetable, va, PGSIZE, pa, PTE_W|PTE_X|PTE_R|PTE_U) != 0) {
    kfree((void *)pa);
    return -1;
  }

  if (umappages(p->kpagetable, va, PGSIZE, pa, PTE_W | PTE_X | PTE_R) != 0) {
    kfree((void *)pa);
    uvmunmap(p->pagetable, va, 1, 0);
    return -1;
  }
  return 0;
}
```

然后我们修改一下usertrap使得它使用这个封装起来的lazy allocation函数:

```c
//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = devintr();

  ...省略...

  } else if (r_scause() == 13 || r_scause() == 15) {
    // 13 is read page fault, 15 is write page fault
    if (lazyvalidate(p, r_stval()) != 0) {
      p->killed = 1;
      goto killed;
    }
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
    goto killed;
  }

  ...省略...

killed:
  if (p->killed)
    exit(-1);
}
```

在做出这些修改后, 我们会在**usertests sbrkarg**上失败. 因为当我们试图从用户态的一个合理的虚拟内存地址进行写或者读操作时, 由于相对应的物理页还没有被分配, 在内核态里会发生内存缺页异常. 我们需要在内核态内做出类似处理.

由于我的lab延续了lab的copyin_new和copyinstr_new那一套, 每一个进程有独立的内核页的设定, 不能像网上大部分的解析一样直接修改**walkaddr**函数. 首先在内核态陷入缺页中断异常后, 我们也需要进行处理物理页分配.

```c
# kernel/trap.c
// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  ...省略...

  if (scause == 13 || scause == 15) {
    lazyvalidate(myproc(), r_stval());
  } else if((which_dev = devintr()) == 0) {
    printf("the faulting process is pid=%d\n", myproc()->pid);
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  ...省略...
}
```

其他, **copyout**函数原本在**walkaddr**返回**0**后会直接**return -1**, 这会造成系统调用**write**返回**-1**. 所以我们在那里也需要加入**lazy allocation**.

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
    lazyvalidate(myproc(), va0); // 检查是否需要延迟分配
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

最后, 在测试的时候我发现**copyin_new**似乎有个边界值的小bug. 稍加修改后可以通过测试.

```c
# kernel/vmcopyin.c
// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  struct proc *p = myproc();
  // 原本是
  // if (srcva >= p->sz || srcva+len >= p->sz || srcva+len < srcva)
  // 但其实是可以相等的
  if (srcva > p->sz || srcva+len > p->sz || srcva+len < srcva)
    return -1;
  memmove((void *) dst, (void *)srcva, len);
  stats.ncopyin++;   // XXX lock
  return 0;
}
```

编译后进行lab批分, 顺利通过.

```bash
vagrant@developer:/vagrant/xv6-labs/xv6-labs-2020$ make grade
```