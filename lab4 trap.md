# lab4 trap



[TOC]



程序运行是完成用户空间和内核空间的切换。每当

- 程序执行系统调用
- 出现了类似page fault、运算时除以0的错误
- 一个设备触发了中断使得当前程序运行需要响应内核设备驱动

都会发生这样的切换 。

这里用户空间和内核空间的切换通常被称为trap



#### 回忆系统调用

再次回忆 lab2 的系统调用过程，在这里根据课程内容做一下补充：

- 首先，当用户调用系统调用的函数时，在进入函数前，会执行 user/usys.S 中相应的汇编指令，指令首先**将系统调用的函数码放到a7寄存器内，然后执行 ecall 指令进入内核态**。
- ecall 指令是 cpu 指令，该指令只做三件事情。
  - 首先将cpu的状态**由用户态（user mode）切换为内核态（supervisor mode）**；
  - 然后将**程序计数器的值保存在了SEPC寄存器**；
  - 最后**跳转到STVEC寄存器指向的指令**。

ecall 指令并没有将 page table 切换为内核页表，也没有切换栈指针，需要进一步执行一些指令才能成功转为内核态。

- 这里需要对 trampoline 进行一下说明，STVEC寄存器中存储的是 trampoline page 的起始位置。**进入内核前，首先需要在该位置处执行一些初始化的操作。例如，切换页表、切换栈指针等操作**。需要注意的是，由于用户页表和内核页表都有 trampoline 的索引，且索引的位置是一样的，因此，即使在此刻切换了页表，cpu 也可以正常地找到这个地址，继续在这个位置执行指令。
- 接下来，cpu 从 trampoline page 处开始进行取指执行。接下来需要**保存所有寄存器的值**，以便在系统调用后恢复调用前的状态。为此，**xv6将进程的所有寄存器的值放到了进程的 trapframe 结构中**。
- 在 kernel/trap.c 中，需要 **检查触发trap的原因，以确定相应的处理方式**。产生中断的原因有很多，比如**系统调用、运算时除以0、使用了一个未被映射的虚拟地址、或者是设备中断等等**。这里是因为系统调用，所以以系统调用的方式进行处理。
- 接下来开始在内核态执行系统调用函数，**在 kernel/syscall.c 中取出 a7 寄存器中的函数码，根据该函数码，调用 kernel/sysproc.c 中对应的系统调用函数**。
- 最后，在系统调用函数执行完成后，将保存在 trapframe 中的 SEPC 寄存器的值取出来，从该地址存储的指令处开始执行（保存的值为ecall指令处的PC值加上4，即为 ecall 指令的下一条指令）。随后**执行 ret 恢复进入内核态之前的状态**，转为用户态。

![image-20231201151010280](https://raw.githubusercontent.com/hanzug/images/master/image-20231201151010280.png)



#### 一些寄存器：

- 在硬件中还有一个寄存器叫做程序计数器（Program Counter Register）。

- 表明当前mode的标志位，这个标志位表明了当前是supervisor mode还是user mode。当我们在运行Shell的时候，自然是在user mode。

- 还有一堆控制CPU工作方式的寄存器，比如SATP（Supervisor Address Translation and Protection）寄存器，它包含了指向page table的物理内存地址（详见4.3）。

- 还有一些对于今天讨论非常重要的寄存器，比如STVEC（Supervisor Trap Vector Base Address Register）寄存器，它指向了内核中处理trap的指令的起始地址。

- SEPC（Supervisor Exception Program Counter）寄存器，在trap的过程中保存程序计数器的值。

- SSRATCH（Supervisor Scratch Register）寄存器，这也是个非常重要的寄存器（详见6.5）。



#### supervisor mode 能做什么？

其中的一件事情是，**你现在可以读写控制寄存器了**。比如说，当你在supervisor mode时，你可以：读写SATP寄存器，也就是page table的指针；STVEC，也就是处理trap的内核指令地址；SEPC，保存当发生trap时的程序计数器；SSCRATCH等等。在supervisor mode你可以读写这些寄存器，而用户代码不能做这样的操作。

另一件事情supervisor mode可以做的是，**它可以使用PTE_U标志位为0的PTE**。当PTE_U标志位为1的时候，表明用户代码可以使用这个页表；如果这个标志位为0，则只有supervisor mode可以使用这个页表。我们接下来会看一下为什么这很重要。

这两点就是supervisor mode可以做的事情，除此之外就不能再干别的事情了



#### RISC-V 栈结构：

fp 指向当前栈帧的开始地址，sp 指向当前栈帧的结束地址。 （栈从高地址往低地址生长，所以 fp 虽然是帧开始地址，但是地址比 sp 高）
 栈帧中从高到低第一个 8 字节 `fp-8` 是 return address，也就是当前调用层应该返回到的地址。
 栈帧中从高到低第二个 8 字节 `fp-16` 是 previous address，指向上一层栈帧的 fp 开始地址。
 剩下的为保存的寄存器、局部变量等。一个栈帧的大小不固定，但是至少 16 字节。
 在 xv6 中，使用一个页来存储栈，如果 fp 已经到达栈页的上界，则说明已经到达栈底。

- [RISC-V的栈是向下增长的，也就是说，栈指针（sp）的值随着栈的使用而减小](https://riscv.org/wp-content/uploads/2015/01/riscv-software-stack-bootcamp-jan2015.pdf)[1](https://riscv.org/wp-content/uploads/2015/01/riscv-software-stack-bootcamp-jan2015.pdf)。
- [RISC-V的栈指针（sp）在函数调用时必须保持16字节对齐](https://riscv.org/wp-content/uploads/2015/01/riscv-software-stack-bootcamp-jan2015.pdf)[1](https://riscv.org/wp-content/uploads/2015/01/riscv-software-stack-bootcamp-jan2015.pdf)。
- [RISC-V的栈帧（stack frame）是用于存放函数参数、局部变量、返回地址等信息的内存区域](https://stackoverflow.com/questions/68645402/where-does-the-stack-pointer-start-for-risc-v-and-where-does-the-stack-pointer)[2](https://stackoverflow.com/questions/68645402/where-does-the-stack-pointer-start-for-risc-v-and-where-does-the-stack-pointer)。
- [RISC-V的栈帧（stack frame）的结构如下](https://stackoverflow.com/questions/68645402/where-does-the-stack-pointer-start-for-risc-v-and-where-does-the-stack-pointer)[2](https://stackoverflow.com/questions/68645402/where-does-the-stack-pointer-start-for-risc-v-and-where-does-the-stack-pointer)：

|           fp-8: 返回地址            |
| :---------------------------------: |
|            fp-16: 原栈帧            |
| fp-24 ~ fp-32: 被调用者保存的寄存器 |
|   fp-40 ~ sp: 局部变量和临时数据    |

fp指针存在s0寄存器中。



## part1: backtrace



思路：

通过汇编语言获取 fp 指针，然后不断打印 fp - 0x8（上一个stack frame的地址）直到回到栈底。

```c
# kernel/printf.c

void backtrace(void)
{
  printf("backtrace:\n");
  // 1. 获取当前栈顶指针
  uint64 curr_fp = r_fp();
  uint64 page_bottom = PGROUNDDOWN(curr_fp); // 栈页的底部
  while (page_bottom < curr_fp) {
    // 2. 获取上一个栈的返回地址和栈顶指针
    uint64 ret = *(pte_t *)(curr_fp - 0x8);
    uint64 prev_fp = *(pte_t *)(curr_fp - 0x10);
    // 3. 打印返回地址
    printf("%p\n", ret);
    // 4. 跳转到上一个栈的栈顶指针所指位置
    curr_fp = prev_fp;
  }
}
```





## Part2: alarm



我们的 alarm handler 函数是在用户态的程序，它会在时钟中断发生时被内核调用，执行一些用户定义的逻辑，比如打印一些信息，然后调用 sigreturn 系统调用，恢复之前程序的状态，继续执行。

sigreturn 使得 userret返回之前程序的trapframe。

### 系统调用的过程：

- 在用户态中，**当执行了一定数量的 cpu 时间中断后，我们将返回地址更改为 handler 函数，这样，在 ret 之后便开始执行 handler 函数**。在 cpu 中断时，也是进入的 trap.c 调用了相应的中断处理函数。
- 在执行好 handler 后，我们希望的是**回到用户调用 handler 前的状态**。但那时的状态已经被用来调用 handler 函数了，现在的 trapframe 中存放的是执行 sys_sigreturn 前的 trapframe，如果直接返回到用户态，则找不到之前的状态，无法实现我们的预期。
- 在 alarmtest 代码中可以看到，**每个 handler 函数最后都会调用 sigreturn 函数，用于恢复之前的状态**。由于每次使用 ecall 进入中断处理前，都会使用 trapframe 存储当时的寄存器信息，包括时钟中断。因此 trapframe 在每次中断前后都会产生变换，**如果要恢复状态，需要额外存储 handler 执行前的 trapframe（即更改返回值为 handler 前的 trapframe）**，这样，无论中间发生多少次时钟中断或是其他中断，保存的值都不会变。
- 因此，在 sigreturn 只需要使用存储的状态覆盖调用 sigreturn 时的 trapframe，就可以在 sigreturn 系统调用后恢复到调用 handler 之前的状态。再使用 ret 返回时，就可以返回到执行 handler 之前的用户代码部分。

![image-20231201181054341](https://raw.githubusercontent.com/hanzug/images/master/image-20231201181054341.png)

#### usertrapret 和 userret 分别做了哪些工作？

usertrapreturn 主要负责恢复用户态的上下文，设置trapframe的参数（例如sepc

 userret 主要负责从内核栈中取出用户态的寄存器和程序计数器，设置寄存器，返回到用户态



#### 为什么在sys_sigalarm中不需要保存trapframe？

在调用 sys_sigalarm 前，已经把调用前所有的寄存器信息保存在了 trapframe 中。然后进入内核中执行 sys_sigalarm 函数。执行的过程中，只需要做一件事：**为 ticks 等字段进行赋值**。赋值完成后，该系统调用函数就完成了，trapframe 中的寄存器的值恢复，返回到了用户态。此时的 trapframe 没有保存的必要。





### test0

可以查看 alarmtest.c 的代码，能够发现 test0 只需要进入内核，并执行至少一次即可。不需要正确返回也可以通过测试。

- 首先，写一个 sys_sigreturn 的代码，直接返回 0即可（后面再添加）：

```c
uint64
sys_sigreturn(void)
{
  return 0;
}
```

- 然后，在 kernel/proc.h 中的 proc 结构体添加字段，用于记录时间间隔，经过的时钟数和调用的函数信息：

```c
int interval;
  uint64 handler;
  int ticks;
```

- 编写 `sys_sigalarm()` 函数，给 proc 结构体赋值：

```c
uint64
sys_sigalarm(void)
{
  int interval;
  uint64 handler;
  struct proc * p;
  if(argint(0, &interval) < 0 || argaddr(1, &handler) < 0 || interval < 0) {
    return -1;
  }
  p = myproc();
  p->interval = interval;
  p->handler = handler;
  p->ticks = 0;
  return 0;
}
```

- 在进程初始化时，给初始化这些新添加的字段（kernel/proc.c 的 allocproc 函数）：

```c
p->interval = 0;
  p->handler = 0;
  p->ticks = 0;
```

- 在进程结束后释放内存（freeproc 函数）：

```c
p->interval = 0;
  p->handler = 0;
  p->ticks = 0;
```

- 最后一步，在时钟中断时，添加相应的处理代码：

```c
if(which_dev == 2) {
    if(p->interval) {
      if(p->ticks == p->interval) {
        p->ticks = 0;  // 待会儿需要删掉这一行
        p->trapframe->epc = p->handler;
      }
      p->ticks++;
    }
    yield();
  }
```

到这里，test0 就可以顺利通过了。值得注意的是，现在还不能正确返回到调用前的状态，因此test1 和 test2 还不能正常通过。

这里为啥把要调用的函数直接赋给 epc 呢，原因是函数在返回时，调用 ret 指令，使用 trapframe 内事先保存的寄存器的值进行恢复。这里我们更改 epc 寄存器的值，在返回后，就直接调用的是 handler 处的指令，即执行 handler 函数。

handler 函数是用户态的代码，使用的是用户页表的虚拟地址，因此只是在内核态进行赋值，在返回到用户态后才进行执行，并没有在内核态执行handler代码。

### 实现 test1/test2

在这里需要实现正确返回到调用前的状态。由于在 ecall 之后的 trampoline 处已经将所有寄存器保存在 trapframe 中，为此，需要添加一个字段，用于保存 trapframe 调用前的所有寄存器的值。

所以，其实只需要增加一个字段，用于保存调用 handler 之前的 trapframe 即可：

- 在 kernel/proc.h 中添加一个指向 trapframe 结构体的指针：

```c
struct trapframe *pretrapframe;
```

- 在进程初始化时，为该指针进行赋值：

```c
p->interval = 0;
  p->handler = 0;
  p->ticks = 0;
  if((p->pretrapframe = (struct trapframe *)kalloc()) == 0){
    release(&p->lock);
    return 0;
  }
```

- 进程结束后，释放该指针：

```c
p->interval = 0;
  p->handler = 0;
  p->ticks = 0;
  if(p->pretrapframe)
    kfree((void*)p->pretrapframe);
```

- 在每次时钟中断处理时，判断是否调用 handler，如果调用了，就存储当前的 trapframe，用于调用之后的恢复：

```c
if(which_dev == 2) {
    if(p->interval) {
      if(p->ticks == p->interval) {
        //p->ticks = 0;
        //memmove(p->pretrapframe, p->trapframe, sizeof(struct trapframe));
        *p->pretrapframe = *p->trapframe;
        p->trapframe->epc = p->handler;
      }// else {
        p->ticks++;
      //}
    }
    yield();
  }
```

- 最后，实现 sys_sigreturn 恢复执行 handler 之前的状态：

```c
uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  *p->trapframe = *p->pretrapframe;
  //memmove(p->trapframe, p->pretrapframe, sizeof(struct trapframe));
  p->ticks = 0;
  return 0;
}
```

到这里就都完成了。需要注意的是，如果有一个handler函数正在执行，就不能让第二个handler函数继续执行。为此，可以再次添加一个字段，用于标记是否有 handler 在执行。我第一次通过的时候就增加了一个字段，但想来想去，感觉有点多余。

其实可以直接在 sigreturn 中设置 ticks 为 0，而取消 trap.c 中的 ticks 置 0 操作。这样，即使第一个 handler 还没执行完，由于 ticks 一直是递增的，第二个 handler 始终无法执行。只有当 sigreturn 执行完成后，ticks 才置为 0，这样就可以等待下一个 handler 执行了