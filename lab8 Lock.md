# lab8: Lock



## 目的

对于xv6多核运行下更好的**parallelism**, 用更精细的**locks**设计来使得**os**运行速度更快, 减少对于同一个锁的竞争. 

### memory allocator

在xv6中，空闲内存是由一个全局共享freelist存储的，在多核的情况下，由于每一个核都有各自的线程，所以在分配内存的时候会造成竞态，为了更好的并行，我们为每一个核分配一个freelist。



## 背景知识



**xv6如何实现buffer cache？**

xv6操作系统中的Buffer Cache实现主要涉及以下几个步骤：

1. **初始化**：在`binit`函数中，使用静态数组`buf`中的`NBUF`个缓存块初始化双向链表（**本次lab改为hash实现**）。所有对Buffer Cache的访问都通过`bcache.head`引用链表，而不是`buf`数组
2. **获取缓存块**：`bread`函数调用`bget`为给定的磁盘块获取缓存块。如果该块未被缓存，需要从磁盘进行读取，`bread`会在返回缓存块之前调用`virtio_disk_rw`来执行此操作
3. **查找缓存块**：`bget`函数扫描缓存块链表，**查找具有给定设备号和块号的缓存块**。如果存在这样的缓存块，`bget`将获取缓存块的睡眠锁。然后`bget`返回锁定的缓存块
4. **创建新的缓存块**：如果给定的磁盘块还没有缓存，`bget`必须创建一个，这可能会重用包含其他磁盘块内容的缓存块。它再次扫描缓存块列表，查找未在使用中的缓存块：任何这样的缓存块都可以使用。`bget`编辑缓存块元数据以记录新设备和块号，并获取其睡眠锁
5. **写入缓存块**：如果调用者修改了缓存块，则必须在释放缓存块之前调用`bwrite`将更改的数据写入磁盘。`bwrite`函数调用`virtio_disk_rw`来将缓存块写入磁盘中
6. **释放缓存块**：内核线程必须通过调用`brelse`释放`bread`读的块



**为什么xv6在buffer cache的 bget中引入睡眠锁?**

**睡眠锁**

```c
void
acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  while (lk->locked) {
    sleep(lk, &lk->lk);
  }
  lk->locked = 1;
  lk->pid = myproc()->pid;
  release(&lk->lk);
}
```

在xv6操作系统中，`bget`函数中引入睡眠锁的主要原因是为了保证在缓冲区被使用的时候，其他的进程不能修改它的内容，避免数据的不一致性

当一个进程试图获取一个已经被占用的睡眠锁，它会被挂起，直到锁被释放。这样可以减少对CPU的浪费，也可以避免死锁的可能性

在`bget`函数中，睡眠锁被用于保护缓冲区块的访问。当一个进程需要访问一个缓冲区块时，它首先需要获取这个缓冲区块的睡眠锁。如果这个锁已经被其他进程持有，那么当前进程就会进入睡眠状态，直到锁被释放。这样做的好处是，当一个线程在等待获取锁的时候，它不会浪费CPU资源，而是通过进入睡眠状态将CPU让给其他线程使用



**时间戳可以轻松的实现LRU算法**



------------





## memory allocator



**我们把kmem改成数组，对应cpu核心数量。**

```c
# kernel/kalloc.c

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    char name[9] = {0};
    snprintf(name, 8, "kmem-%d", i);
    initlock(&kmem[i].lock, name);
  }
  freerange(end, (void*)PHYSTOP);
}
```

在**kalloc**和**kfree**的时候要注意暂时先关闭interrupt中断, 免得在读取到当前**cpu id**是**1**后, 这一段代码被放到**cpu 2**上去跑, 造成内存不均衡了.

```c
# kernel/kalloc.c
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  push_off();
  int cpu = cpuid();
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;
  // --- critical session ---
  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
  // --- end of critical session ---
  pop_off();
}
```

我们写一个helper函数, 以**round-robin**的形式去偷取**邻居cpu**的空余内存页, 一次只偷取一页. 其实我可以想到的是, 如果想达成一个更高效的运行, 即减少偷取的次数, 最好是一次偷取比如邻居一半的空余内存页. 这需要我们维持额外一个**int**结构来记住每个freelist现在有多长. 不过对于这个**lab**实验来说, 其实不用那么麻烦, 一次偷一页已经足够了.

```c
// Try steal a free physical memory page from another core
// interrupt should already be turned off
// return NULL if not found free page
void *
ksteal(int cpu) {
  struct run *r;
  for (int i = 1; i < NCPU; i++) {
    // 从右边的第一个邻居开始偷
    int next_cpu = (cpu + i) % NCPU;
    // --- critical session ---
    acquire(&kmem[next_cpu].lock);
    r = kmem[next_cpu].freelist;
    if (r) {
      // steal one page
      kmem[next_cpu].freelist = r->next;
    }
    release(&kmem[next_cpu].lock);
    // --- end of critical session ---
    if (r) {
      break;
    }
  }
  // 有可能返回NULL, 如果邻居也都没有空余页的话
  return r;
}

void *
kalloc(void)
{
  struct run *r;
  push_off();
  int cpu = cpuid();
  // --- critical session ---
  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if (r) {
    kmem[cpu].freelist = r->next;
  }
  release(&kmem[cpu].lock);
  // --- end of critical session ---

  if (r == 0) {
    r = ksteal(cpu);
  }
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  pop_off();
  return (void*)r;
}
```

## buffer cache



**xv6**会缓存一些经常使用的**block**块在内存里(即这个**buffer cache pool**), 使得每次重复调用这个**block**块时直接从**内存**读取, 而不用做**磁盘I/O**.

原始版本的**buffer cache**由一个大锁**bcache.lock**保护, 限制了并行运行的效率. 我们要把它拆解为更精细的锁管理, 用hash bucket的思想. 并且放弃双链表的管理方式, **直接使用ticks时间戳来实现LRU(least-recently-used)算法.**

首先我们改写一下数据结构, 并把两个很简单的小功能**bpin**和**bunpin**修改一下, 然后在**binit**里把每个锁都初始化一下.

```c
# kernel/bio.c

#define BUCKETSIZE 13 // number of hashing buckets
#define BUFFERSIZE 5 // number of available buckets per bucket

extern uint ticks; // system time clock

// 一共有 13 * 5 = 65个buffer块儿
struct {
  struct spinlock lock;
  struct buf buf[BUFFERSIZE];
} bcachebucket[BUCKETSIZE];

int
hash(uint blockno)
{
  return blockno % BUCKETSIZE;
}

void
bpin(struct buf *b) {
  int bucket = hash(b->blockno);
  acquire(&bcachebucket[bucket].lock);
  b->refcnt++;
  release(&bcachebucket[bucket].lock);
}

void
bunpin(struct buf *b) {
  int bucket = hash(b->blockno);
  acquire(&bcachebucket[bucket].lock);
  b->refcnt--;
  release(&bcachebucket[bucket].lock);
}

void
binit(void)
{
  for (int i = 0; i < BUCKETSIZE; i++) {
    initlock(&bcachebucket[i].lock, "bcachebucket");
    for (int j = 0; j < BUFFERSIZE; j++) {
      initsleeplock(&bcachebucket[i].buf[j].lock, "buffer");
    }
  }
}
```

最重点的是**bget**函数: 根据所需要的**blockno**, 计算出对应哪个bucket后, 拿锁进行查找. 如果没能找到对应的buffer cache block, 则就在当前bucket里试图寻找一个空闲的来分配. (理论上这一步应该被扩展为, 如果当前bucket里没有空闲的, 应该去从邻居bucket里去**偷取**. 但是这里我们直接偷懒没做偷取的功能, 也通过了测试).

```c
# kernel/bio.c

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket = hash(blockno);
  acquire(&bcachebucket[bucket].lock);
  // --- critical session for the bucket ---
  // Is the block already cached?
  for (int i = 0; i < BUFFERSIZE; i++) {
    b = &bcachebucket[bucket].buf[i];
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      b->lastuse = ticks;
      release(&bcachebucket[bucket].lock);
      acquiresleep(&b->lock);
      // --- end of critical session
      return b;
    }
  }
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  uint least = 0xffffffff; // 这个是最大的unsigned int
  int least_idx = -1;
  for (int i = 0; i < BUFFERSIZE; i++) {
    b = &bcachebucket[bucket].buf[i];
    if(b->refcnt == 0 && b->lastuse < least) {
      least = b->lastuse;
      least_idx = i;
    }
  }

  if (least_idx == -1) {
    // 理论上, 这里应该去邻居bucket偷取空闲buffer
    panic("bget: no unused buffer for recycle");
  }

  b = &bcachebucket[bucket].buf[least_idx];
  b->dev = dev;
  b->blockno = blockno;
  b->lastuse = ticks;
  b->valid = 0;
  b->refcnt = 1;
  release(&bcachebucket[bucket].lock);
  acquiresleep(&b->lock);
  // --- end of critical session
  return b;
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  int bucket = hash(b->blockno);
  acquire(&bcachebucket[bucket].lock);
  b->refcnt--;
  release(&bcachebucket[bucket].lock);
  releasesleep(&b->lock);
}
```

到这里为止, **buffer cache**的改动已经完成了. 我们直接运行全lab测试, 顺利通过.

```bash
vagrant@developer:/vagrant/xv6-labs/xv6-labs-2020$ make grade
```