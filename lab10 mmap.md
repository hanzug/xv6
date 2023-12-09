# lab10: mmap

## 目的

实现 **mmap** ，针对一个文件, 将其映射到用户态的一个地址中去, 使得用户可以直接通过对于这个地址的读/写来实现**文件的操作**. 





## 背景知识

![User Address Space](https://raw.githubusercontent.com/hanzug/images/master/34c87bfac1de449fa5a46a7355770dd8%7Etplv-k3u1fbpfcp-zoom-in-crop-mark%3A1512%3A0%3A0%3A0.awebp)

`mmap`的主要用途包括：

- 将一个普通文件映射到内存中，以便进行高效的读写操作
- 创建一个匿名内存映射，用于在进程之间共享数据
- 在特定情况下，可以使用`mmap`来代替`malloc`等动态内存分配函数

`mmap`函数的基本形式如下：

```c
void *mmap (void *addr, size_t length, int prot, int flags, int fd, off_t offset);
```



其中，

- `addr`参数指定映射的起始地址，如果为NULL，则由系统自动选择地址
- `length`参数指定映射的长度
- `prot`参数指定映射区的保护模式，可以是`PROT_READ`、`PROT_WRITE`或`PROT_EXEC`等值的组合
- `flags`参数指定映射的行为，可以是`MAP_SHARED`、`MAP_PRIVATE`等值的组合
- `fd`参数指定文件描述符，如果是匿名映射则设为-1
- `offset`参数指定从文件开头开始映射的位置

 通过`munmap()`函数来删除映射

### mmap

按照实验要求, 为每个struct proc增加一个长度为16的vma表

```c
# kernel/proc.h

// Number of virtual memory area per process
#define NVMA 16

// Virtual Memory Area
struct vma {
 int valid;
 uint64 addr;
 int length;
 int prot;
 int flags;
 int fd;
 int offset;
 struct file* f;
};

// Per-process state
struct proc {
  ...省略...
  char name[16];               // Process name (debugging)
  struct vma vmas[NVMA];       // Virtual memory area array
};
```

首先我们把两个新的系统调用**mmap**和**munmap**的一系列函数签名, 调用代码, 跳转指针等常规操作都做好. 然后直接来看如何实现**mmap**这个函数. 为了效率, 我们并不直接分配物理页, 因为用户或许只会读到一个很大的文件的一小部分. 但是我们确实通过增加**p->sz**这个参数, 为整个文件大小在用户态的地址空间里预留了位置.

```c
# kernel/sysfile.c

uint64
sys_mmap(void) {
  uint64 failure = (uint64)((char *) -1);
  struct proc* p = myproc();
  uint64 addr;
  int length, prot, flags, fd, offset;
  struct file* f;

  // parse argument 
  if (argaddr(0, &addr) < 0 || argint(1, &length) < 0 || argint(2, &prot) < 0
      || argint(3, &flags) < 0 || argfd(4, &fd, &f) < 0 || argint(5, &offset) < 0)
    return failure;

  // sanity check 安全检查
  length = PGROUNDUP(length);
  if (MAXVA - length < p->sz)
    return failure;
  if (!f->readable && (prot & PROT_READ))
    return failure;
  if (!f->writable && (prot & PROT_WRITE) && (flags == MAP_SHARED))
    return failure;

  // find an empty vma slot and fill in
  for (int i = 0; i < NVMA; i++) {
    struct vma* vma = &p->vmas[i];
    if (vma->valid == 0) {
      vma->valid = 1;
      vma->addr = p->sz;
      p->sz += length; // 虚拟的增加进程大小, 但没有实际分配物理页
      vma->length = length;
      vma->prot = prot;
      vma->flags = flags;
      vma->fd = fd;
      vma->f = f;
      filedup(f); // 增加文件的引用数, 保证它在mmap期间一定不会被关闭
      vma->offset = offset;
      return vma->addr;
    }
  }

  // all vma are in use
  return failure;
}
```

在这样写完**mmap**后, 当用户试图去访问**mmap**所返回的地址时, 由于我们没有分配物理页, 将会触发**缺页中断**. 这个时候我们就需要在**usertrap**里把对应offset的文件内容读到一个新分配的物理页中, 并把这个物理页加入这个进程的虚拟内存映射表里.

```c
# kernel/trap.c

void
usertrap(void)
{
  ...省略...

  if(r_scause() == 8){
    ...省略...
  } else if(r_scause() == 13 || r_scause() == 15) { // 读或写造成的缺页中断
    uint64 va = r_stval();
    struct proc* p = myproc();
    if (va > MAXVA || va > p->sz) {
      // sanity check安全检查
      p->killed = 1;
    } else {
      int found = 0;
      for (int i = 0; i < NVMA; i++) {
        struct vma* vma = &p->vmas[i];
        if (vma->valid && va >= vma->addr && va < vma->addr+vma->length) {
          // 找到对应的vma, 分配一个新的4096字节的物理页
          // 并把对应的文件内容读进这个页, 插入进程的虚拟内存映射表
          va = PGROUNDDOWN(va);
          uint64 pa = (uint64)kalloc();
          if (pa == 0) {
            break;
          }
          memset((void *)pa, 0, PGSIZE);
          ilock(vma->f->ip);
          if(readi(vma->f->ip, 0, pa, vma->offset + va - vma->addr, PGSIZE) < 0) {
            iunlock(vma->f->ip);
            break;
          }
          iunlock(vma->f->ip);
          int perm = PTE_U; // 权限设置
          if (vma->prot & PROT_READ)
            perm |= PTE_R;
          if (vma->prot & PROT_WRITE)
            perm |= PTE_W;
          if (vma->prot & PROT_EXEC)
            perm |= PTE_X;
          if (mappages(p->pagetable, va, PGSIZE, pa, perm) < 0) {
            kfree((void*)pa);
            break;
          }
          found = 1;
          break;
        }
      }

      if (!found)
        p->killed = 1;
    }
  } 
  ...省略...
}
```

然后, 在**munmap**时, 我们需要把分配的物理页释放掉, 而且如果flag是**MAP_SHARED**, 直接把**unmap**的区域无脑复写回文件中, 不管有没有被修改. (其实可以优化, 通过观察**dirty bit**来决定一个页是否需要被复写)

```c
# kernel/sysfile.c

uint64
sys_munmap(void) {
  uint64 addr;
  int length;
  if (argaddr(0, &addr) < 0 || argint(1, &length) < 0)
    return -1;
  struct proc *p = myproc();
  struct vma* vma = 0;
  int idx = -1;
  // find the corresponding vma
  for (int i = 0; i < NVMA; i++) {
    if (p->vmas[i].valid && addr >= p->vmas[i].addr && addr <= p->vmas[i].addr + p->vmas[i].length) {
      idx = i;
      vma = &p->vmas[i];
      break;
    }
  }
  if (idx == -1)
    // not in a valid VMA
    return -1;

  addr = PGROUNDDOWN(addr);
  length = PGROUNDUP(length);
  if (vma->flags & MAP_SHARED) {
    // write back 将区域复写回文件
    if (filewrite(vma->f, addr, length) < 0) {
      printf("munmap: filewrite < 0\n");
    }
  }
// 删除虚拟内存映射并释放物理页
  uvmunmap(p->pagetable, addr, length/PGSIZE, 1); 

  // change the mmap parameter
  if (addr == vma->addr && length == vma->length) {
    // fully unmapped 完全释放
    fileclose(vma->f);
    vma->valid = 0;
  } else if (addr == vma->addr) {
    // cover the beginning 释放区域包括头部
    vma->addr += length;
    vma->length -= length;
    vma->offset += length;
  } else if ((addr + length) == (vma->addr + vma->length)) {
    // cover the end 释放区域包括尾部
    vma->length -= length;
  } else {
    panic("munmap neither cover beginning or end of mapped region");
  }
  return 0;
}
```

最后, 在**exit**时我们也要把**mmap**的区域释放掉. 在**fork**时需要拷贝**vma**表, 并增加一次文件**f**的引用数量

```c
# kernel/proc.c
void
exit(int status)
{
  ...省略...

  // unmap any mmapped region
  for (int i = 0; i < NVMA; i++) {
    if (p->vmas[i].valid) {
      if (p->vmas[i].flags & MAP_SHARED) {
        filewrite(p->vmas[i].f, p->vmas[i].addr, p->vmas[i].length);
      }
      fileclose(p->vmas[i].f);
      uvmunmap(p->pagetable, p->vmas[i].addr, p->vmas[i].length / PGSIZE, 1);
      p->vmas[i].valid = 0;
    }
  }

  begin_op();
  ...省略...
}

int
fork(void)
{
  ...省略..
  for (int i = 0; i < NVMA; i++) {
    np->vmas[i].valid = 0;
    if (p->vmas[i].valid) { // 复制vma entry
      memmove(&np->vmas[i], &p->vmas[i], sizeof(struct vma));
      filedup(p->vmas[i].f); // 增加引用次数
    }
  }

  np->state = RUNNABLE;

  release(&np->lock);

  return pid;
}
```

现在我们将mmaptest加入Makefile, 编译后顺利通过全部测试.

```bash
vagrant@developer:/vagrant/xv6-labs/xv6-labs-2020$ make grade
```