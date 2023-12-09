# lab2 syscall





**实验二主要涉及对系统函数调用过程的理解以及尝试自己手动添加系统调用**。

- 用户呼叫我们提供的系统调用接口 **int trace(int)**
- 这个接口的实现由**perl**脚本生成的汇编语言实现, 将**SYS_trace**的代号放入**a7**寄存器, 由**ecall**硬件支持由用户态转入内核态
- 控制转到系统调用的通用入口 **void syscall(void)** 手上. 它由**a7**寄存器读出需要被调用的系统调用是第几个, 从*uint64 (\*syscalls[])(void)*这个函数指针数组跳转到那个具体的系统调用函数实现上. 将返回值放在**a0**寄存器里
- 我们从第二步的**ecall**里退出来了, 汇编指令**ret**使得用户侧系统调用接口返回

![image-20231128195257277](https://raw.githubusercontent.com/hanzug/images/master/image-20231128195257277.png)





**对于ecall**

​	你可以在[RISC-V的官方文档](https://riscv.org/wp-content/uploads/2019/12/riscv-spec-20191213.pdf)[1](https://riscv.org/wp-content/uploads/2019/12/riscv-spec-20191213.pdf)的第24页看到ecall指令的编码格式。

​	ecall指令的功能是从用户模式（U-mode）切换到机器模式（M-mode），并执行内核中的异常处理程序（exception handler）。

​	这个异常处理程序会根据用户进程的a7寄存器中的系统调用号，调用相应的系统调用函数。这些系统调用函数就是在syscall.c文件中定义的。



1. 首先我们在**user/user.h**里提供一个用户的接口. 

```c
# user/user.h
...省略...
int sleep(int);
int uptime(void);
int trace(int);  // 新系统调用trace的函数原型签名
```

2. 接着在**user/usys.pl**里为这个函数签名加入一个entry.

```c
# user/usys.pl
...省略...
entry("sleep");
entry("uptime");
entry("trace");
```

其实在这一步的时候, 我困惑了好一会儿. 因为我看遍全局后发现, 我从来没有去实现过我在**user/user.h**里声明的这个新的系统调用**trace**. 但如果我们把这个*entry*宏展开后, 就可以明白其实它为我们直接以汇编语言的形式实现了新的系统调用stub.

```ca65
entry的具体宏展开是:
sub entry {
    my $name = shift;
    print ".global $name\n";
    print "${name}:\n";
    print " li a7, SYS_${name}\n";
    print " ecall\n";
    print " ret\n";
}

# 经过compiler后, entry("trace")为我们在usys.S里生成了如下的汇编代码

# usys.S
.global trace
trace:
 li a7, SYS_trace
 ecall
 ret


# 我们看到, 上述的重点就是, 把sys_trace的代号放入a7这个寄存器里, 
# 然后用riscv提供的ecall指令从, 用户态切入到内核态! 重点！
```

3. 为**struct proc**加上记忆需要**trace**的bit mask

```c
# kernel/proc.h 加一个field
// Per-process state
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  struct proc *parent;         // Parent process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  uint64 tracemask;            // the sys calls this proc is tracing  << 新的field加在这里
};

# kernel/proc.c 修改2个函数

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  ...省略...
  // Zero initializes the tracemask for a new process << 新的进程默认不追踪sys calls
  p->tracemask = 0;
  return p;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // inherit parent's trace mask << fork出的新进程继承父进程的bit mask
  np->tracemask = p->tracemask;
  ...省略...
}
```

4. 实现内核态下的**sys_trace**函数, 用**argint**函数从寄存器拿参数, 用**myproc**获得当前进程的一个指针

```c
# kernel/sysproc.c

// click the sys call number in p->tracemask
// so as to tracing its calling afterwards
uint64 
sys_trace(void) {
  int trace_sys_mask;
  if (argint(0, &trace_sys_mask) < 0)
    return -1;
  myproc()->tracemask |= trace_sys_mask;
  return 0;
}
```

5. 为这个sys_trace提供一个代号和函数指针的mapping. 并且顺手加上一个从代号到名字的mapping, 便于print

```c
# kernel/sys_call.h
...
#define SYS_mkdir  20
#define SYS_close  21
#define SYS_trace  22

# kernel/sys_call.c

extern uint64 sys_trace(void);

static uint64 (*syscalls[])(void) = {
 ...省略...
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_trace]   sys_trace,
};

static char *sysnames[] = {
    "",
    ...省略...
    "mkdir",
    "close",
    "trace",
};
```

6. 最后我们修改通用入口syscall函数, 让它print出需要trace的系统调用

```c
# kernel/syscall.c
...省略...
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7; // 系统调用代号存在a7寄存器内
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num](); // 返回值存在a0寄存器内
    if (p->tracemask & (1 << num)) { // << 判断是否需要trace这个系统调用
      // this process traces this sys call num
      printf("%d: syscall %s -> %d\n", p->pid, sysnames[num], p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

OK. 让我们来从头到尾捋一遍思路. 用户调用一次新系统调用**trace**到底发生了哪些事:

- 用户呼叫我们提供的系统调用接口 **int trace(int)**
- 这个接口的实现由**perl**脚本生成的汇编语言实现, 将**SYS_trace**的代号放入**a7**寄存器, 由**ecall**硬件支持由用户态转入内核态
- 控制转到系统调用的通用入口 **void syscall(void)** 手上. 它由**a7**寄存器读出需要被调用的系统调用是第几个, 从*uint64 (\*syscalls[])(void)*这个函数指针数组跳转到那个具体的系统调用函数实现上. 将返回值放在**a0**寄存器里
- 我们从第二步的**ecall**里退出来了, **汇编指令ret使得用户侧系统调用接口返回**

把文件目标加入Makefile后进行lab批分, 顺利通过.



------

### sysinfo

这个新增的系统调用的步骤和上面**trace**基本一致, 一些基础的CRUD代码我们就不再展示, 比如加一个用户侧的接口, 加一个**sysinfo**系统调用的代号和函数指针等. 我们来看3个比较重要的步骤:

1. 获得空余内存的字节数

根据实验指引, 我们来看**kernel/kalloc.c**内核文件. 我们可以观察到的是, 每个物理内存页的单位是**PGSIZE=4096**字节, 以一个单链表的形式管理空闲内存页. 整个xv6的内存地址空间是由**void kinit()**这个函数来定义的. 既然我们知道**kmem->freelist**指向第一个空余内存页, 然后去计算整个系统的空余内存只需要遍历一次这个单链表即可. 由上, 我们可得如下代码:

```c
# kernel/kalloc.c
// Return the number of bytes of free memory
// should be multiple of PGSIZE
uint64
kfreemem(void) {
  struct run *r;
  uint64 free = 0;
  acquire(&kmem.lock); // 上锁, 防止数据竞态
  r = kmem.freelist;
  while (r) {
    free += PGSIZE; // 每一页固定4096字节
    r = r->next; // 遍历单链表
  }
  release(&kmem.lock);
  return free;
}
```

2. 获得分配出去的进程数量

根据实验指引, 我们来看**kernel/proc.c**内核文件. 在这个文件的一开头, 我们看到xv6静态分配了最多64个进程的信息存储空间在**struct proc proc[NPROC]**. 同时在**void procinit(void)里**每一个存储空间都被初始化了, 不会有空悬指针访问的危险. 所以我们遍历这个存储空间列表即可知道分配出去了多少进程. 代码如下:

```c
# kernel/proc.c
// Count how many processes are not in the state of UNUSED
uint64
count_free_proc(void) {
  struct proc *p;
  uint64 count = 0;
  for(p = proc; p < &proc[NPROC]; p++) {
    // 此处不一定需要加锁, 因为该函数是只读不写
    // 但proc.c里其他类似的遍历时都加了锁, 那我们也加上
    acquire(&p->lock);
    if(p->state != UNUSED) {
      count += 1;
    }
    release(&p->lock);
  }
  return count;
}
```

3. 将数据拷贝到用户态提供的buffer里

**xv6**的用户态和内核态的数据并不能直接交互. 粗略地看了一眼拷贝进和出的具体实现函数, 似乎是和虚拟内存地址的寻址有关系, 这个之后我们应该会在lab3里继续探究. 我们这里需要使用**copyou**t函数来将内核态的数据拷贝到用户态地址上. 来看一下**copyou**t的函数签名:

```c
// 从内核态拷贝到用户态
// 拷贝len字节数的数据, 从src指向的内核地址开始, 到由pagetable下的dstv用户地址
// 成功则返回 0, 失败返回 -1
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
```

好, 万事俱备, 只差把上述功能粘在一起, 写出我们的**sysinfo**系统调用内核函数:

```c
# kernel/sysproc.c
// collect system info
uint64
sys_sysinfo(void) {
  struct proc *my_proc = myproc();
  uint64 p;
  if(argaddr(0, &p) < 0) // 获取用户提供的buffer地址
    return -1;
  // construct in kernel first 在内核态先构造出这个sysinfo struct
  struct sysinfo s;
  s.freemem = kfreemem();
  s.nproc = count_free_proc();
  // copy to user space // 把这个struct复制到用户态地址里去
  if(copyout(my_proc->pagetable, p, (char *)&s, sizeof(s)) < 0)
    return -1;
  return 0;
}
```

把文件目标加入Makefile后进行lab批分, 顺利通过.

```c
vagrant@developer:/vagrant/xv6-labs/xv6-labs-2020$ ./grade-lab-syscall sysinfo
make: 'kernel/kernel' is up to date.
== Test sysinfotest == sysinfotest: OK (2.4s) 
```
