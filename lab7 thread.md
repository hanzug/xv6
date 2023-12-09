# lab7 thread

### uthread: switching between threads



这里的线程相比现代操作系统中的线程而言，更接近一些语言中的“协程”（coroutine）。原因是这里的“线程”是完全用户态实现的，多个线程也只能运行在一个 CPU 上，并且没有时钟中断来强制执行调度，需要线程函数本身在合适的时候主动 yield 释放 CPU。这样实现起来的线程并不对线程函数透明，所以比起操作系统的线程而言更接近 coroutine。



在这个实验里我们需要实现一个**用户态**的线程调度, 它是需要用户函数里主动调用**yield**来和其他线程合作取得并行操作, 而非**xv6**进程调度里的"**强制调度**".



这个实验其实相当于在用户态重新实现一遍 xv6 kernel 中的 scheduler() 和 swtch() 的功能，所以大多数代码都是可以借鉴的。



#### 进程、线程和协程

> **进程是操作系统资源分配的基本单位，而线程是处理器任务调度和执行的基本单位**

在 xv6 中，一个进程只有一个线程，因此本实验中区分不大。

第一个实验的内容很像协程的概念，即**用户线程切换时，不进入内核态，而是直接在用户线程上，让用户线程自己主动出让 cpu，而不是接受时钟中断**。

在这个实验里我们需要实现一个**用户态**的线程调度, 它是需要用户函数里主动调用**yield**来和其他线程合作取得并行操作, 而非**xv6**进程调度里的"**强制调度**".





#### 借鉴线程调度

线程调度的过程大概是以下几个步骤：

- 首先是用户线程接收到了时钟中断，**强迫CPU从用户空间进程切换到内核**，同时在 trampoline 代码中，保存当前寄存器状态到 trapframe 中；
- **在 usertrap 处理中断时**，切换到了该进程对应的内核线程；
- 内核线程在内核中，先做一些操作，**然后调用 swtch 函数，保存用户进程对应的内核线程的寄存器至 context 对象**；
- **swtch 函数并不是直接从一个内核线程切换到另一个内核线程；而是先切换到当前 cpu 对应的调度器线程**，之后就在调度器线程的 context 下执行 schedulder 函数中；
- **schedulder 函数会再次调用 swtch 函数，切换到下一个内核线程中，由于该内核线程肯定也调用了 swtch 函数，所以之前的 swtch 函数会被恢复**，并返回到内核线程所对应进程的系统调用或者中断处理程序中。
- 当内核程序执行完成之后，**trapframe 中的用户寄存器会被恢复，完成线程调度**。





**操作系统级线程调度：**

![image-20231209105113574](https://raw.githubusercontent.com/hanzug/images/master/image-20231209105113574.png)



**我们实现的用户及线程调度**

![image-20231209105228063](https://raw.githubusercontent.com/hanzug/images/master/image-20231209105228063.png)

首先我们把**kernel/switch.S**里用来保存寄存器状态的汇编代码原封不动的搬到**user/uthread_switch.S**里.

```bash
# user/uthread_switch.S
	.text

	/*
         * save the old thread's registers,
         * restore the new thread's registers.
         */

	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
	ret    /* return to ra */
```

然后我们构造出**struct thread**用来保存每个用户态线程寄存器的**struct thread_context**.

```c
# user/uthread.c
// Saved registers for user-level thread switching
struct thread_context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

struct thread {
  struct thread_context     thread_context;    /* register status */
  char                      stack[STACK_SIZE]; /* the thread's stack */
  int                       state;             /* FREE, RUNNING, RUNNABLE */
};
```

我们需要一个小helper函数来初始化一个新线程的状态. 我们需要把它的第一次的返回地址**ra**设置成用户传进来的线程函数**func**的地址, 并且把这个线程的状态设置成***RUNNABLE\***, 让调度器去跑它. 需要注意的是, 在**risc-v**里, 栈是由高地址向低地址增长的, 所以这个线程的最初stack pointer sp应该在栈的顶端.

```c
/*
 * helper function to setup the routine for a newly-created thread
 */
void clear_thread(struct thread *t, void (*func)()) {
  memset((void *)&t->stack, 0, STACK_SIZE);
  memset((void *)&t->thread_context, 0, sizeof(struct thread_context));
  t->state = RUNNABLE;
  t->thread_context.sp = (uint64) ((char *)&t->stack + STACK_SIZE);  // 初始sp在栈顶
  t->thread_context.ra = (uint64) func;  // 初始跳转位置是user传进来的线程函数
}
```

然后我们把缺失的两个 **/\* \*YOUR CODE HERE \**/** 的地方填补上即可.

```c
# user/uthread.c
void 
thread_schedule(void)
{
  struct thread *t, *next_thread;

  ...省略...

  if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    /* YOUR CODE HERE
     * Invoke thread_switch to switch from t to next_thread:
     * thread_switch(??, ??);
     */
    thread_switch((uint64) t, (uint64) current_thread);
  } else
    next_thread = 0;
}

void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  // YOUR CODE HERE
  clear_thread(t, func);
}
```

加入Makefile, 编译后顺利通过**uthread**测试.

------

### using threads

这个实验非常的简单. 给出了一个单线程版本的**hashtable**, 在多线程运行的情况下有**race condition**, 需要我们在合适的地方加锁来避免竞态. 首先回答一下问题: 为什么在多线程情况下这个hashtable会有数据丢失

```bash
Why are there missing keys with 2 threads, but not with 1 thread? 
Identify a sequence of events with 2 threads that can lead to a key being missing.

For example, consider 2 threads are concurrently adding
 [4, 'd'] & [5, 'e'] pair into the same bucket respectively:

the bucket is originally [<1, 'a'>, <2, 'b'>, <3, 'c'>]
in put() function, they both iterate to the end of the linked list
and decided to insert at the back of <3, 'c'>
whoever execute the line '*p = e' will have the other side's changed overwritten and thus lost.
```

我们直接为每个**bucket**配置一个**pthread_mutex**锁来保证只有一个线程可以读这个**bucket**里的所有数据进行读和写的操作即可, 轻松便捷. 其实这里对于hashtable的最优解应该是配置**读写锁(reader-writer lock)**, 允许多个get或者1个put. 但题目也没这么要求, 就偷懒一下了.

```c
# notxv6/ph.c

pthread_mutex_t locks[NBUCKET]; // one lock per bucket

void init_locks() {
  // 在main函数一开始 呼叫一下这个函数
  for (int i = 0; i < NBUCKET; i++) {
    pthread_mutex_init(&locks[i], NULL);
  }
}

static 
void put(int key, int value)
{
  int i = key % NBUCKET;
  pthread_mutex_lock(&locks[i]); // 加bucket锁
  // is the key already present?
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    insert(key, value, &table[i], table[i]);
  }
  pthread_mutex_unlock(&locks[i]); // 放bucket锁
}

static struct entry*
get(int key)
{
  int i = key % NBUCKET;
  pthread_mutex_lock(&locks[i]);  // 加bucket锁

  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key) break;
  }

  pthread_mutex_unlock(&locks[i]); // 放bucket锁
  return e;
}
```

加入Makefile, 编译后顺利通过**ph_safe**和**ph_fast**测试.

------

### barrier

这个实验我们需要实验一个**barrier**, 属于是比较经典的多线程**synchronization**的手段和数据结构. 大致的思路是比较直白的: 每个线程都会呼叫**barrier()**函数, 将会卡在里面直到每个需要synchronize的线程都进入这个函数. 需要使用**conditional variable**来实现等待和唤醒的功能.

但在实现的时候需要注意一点是, 在所以thread离开上一轮之前, **bstate.nthread**不能被下一轮的第一个人设置为**0**从而形成竞态, 使得一些上一轮沉睡等待的thread卡死在上一轮中. 在此处我们使用C新特性**thread local**来实现**sense reversal**.

```c
# notxv6/barrier.c
static __thread int thread_flag = 0;  // local sense 每个线程单独的内存地址

struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;        // Barrier round
  int flag;  
} bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.round = 0;
  bstate.nthread = 0;
  bstate.flag = 0;
}

static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  pthread_mutex_lock(&bstate.barrier_mutex);
  thread_flag= !thread_flag;
  // wait until all previous round threads has exited the barrier
  // 等上一轮的最后一个抵达的人来翻页
  while (thread_flag == bstate.flag) {
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  int arrived = ++bstate.nthread;
  if (arrived == nthread) {
    // I am the last thread in this round
    // need to flip the round flag
    // 我是最后一个抵达的人, 我来翻页并唤醒前N-1个在沉睡的线程
    bstate.round++;
    bstate.flag = !bstate.flag;
    bstate.nthread = 0;
    pthread_cond_broadcast(&bstate.barrier_cond);
  } else {
    // wait for other threads
    // 主动沉睡 等待这一轮的结束并被唤醒
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
}
```

把测试文件加入Makefile后进行测试, 顺利通过此lab.

```bash
vagrant@developer:/vagrant/xv6-labs/xv6-labs-2020$ make grade
```