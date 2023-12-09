# lab9: File system



## 目的

为 xv6 的文件系统添加大文件以及符号链接支持，主要涉及对 inode 的操作。



## 背景知识



**inode**:

inode，中文译名为"索引节点"，是Unix/Linux文件系统中的一个重要概念。每一个文件都有对应的inode，里面包含了与该文件有关的一些信息。具体来说，inode包含以下内容：

- 文件的字节数
- 文件拥有者的User ID
- 文件的Group ID
- 文件的读、写、执行权限
- 文件的时间戳，共有三个：ctime指inode上一次变动的时间，mtime指文件内容上一次变动的时间，atime指文件上一次打开的时间。
- 链接数，即有多少文件名指向这个inode
- 文件数据block的位置

除了文件名以外的所有文件信息，都存在inode之中。你可以使用`stat`命令来查看某个文件的inode信息。



在 xv6 中 inode 的结构体表示为：

```c
// kernel/fs.c

// note: NDIRECT=12
// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};
```



## larger file

**初始inode文件索引为：**

![image-20231209181458052](https://raw.githubusercontent.com/hanzug/images/master/image-20231209181458052.png)

文件大小最大为： （12 + 256）= 268 个 block



**增加二级索引后：**

![image-20231209181551678](https://raw.githubusercontent.com/hanzug/images/master/image-20231209181551678.png)

增加二级索引后：11 + 256 + 256 * 256 = 65803 个 block

在**bmap**寻找block的函数里加上双层间接映射的逻辑, 同时在**itrunc**里把对应的双层连接也清除掉.

```c
# kernel/fs.c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a, *b;
  struct buf *inbp, *ininbp;

  ... 直接映射层 和之前代码一样 ...
  bn -= NDIRECT;

  ... 单层间接映射 和之前代码一样 ...
  bn -= NINDIRECT; // 记得减掉offset

  // after subtraction, [0, 65535] is doubly-indirect block
  if (bn < NININDIRECT) 
    // Load 1st indirect block, allocating if necessary
    // index 10是最后一个直接映射, index 11是单层间接映射, index 12是双层间接映射
    if ((addr = ip->addrs[NDIRECT+1]) == 0)
      ip->addrs[NDIRECT+1] = addr = balloc(ip->dev);
    inbp = bread(ip->dev, addr);
    a = (uint*)inbp->data;
    if ((addr = a[bn/NINDIRECT]) == 0) { // 之后的每一个映射可以吃下NINDIRECT个blocks
      a[bn/NINDIRECT] = addr = balloc(ip->dev);
      log_write(inbp);
    }
    brelse(inbp);

    // Load the 2nd indirect block, allocating if necessary
    ininbp = bread(ip->dev, addr);
    b = (uint*)ininbp->data;
    if ((addr = b[bn % NINDIRECT]) == 0) { // 取余数
      b[bn % NINDIRECT] = addr = balloc(ip->dev);
      log_write(ininbp);
    }
    brelse(ininbp);
    return addr;
  }
  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Caller must hold ip->lock.
void
itrunc(struct inode *ip)
{
  int i, j, k;
  struct buf *bp, *inbp;
  uint *a, *b;

  ... 直接映射层和单层间接映射层 和之前代码一样 ...

  if(ip->addrs[NDIRECT+1]) {
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;
    for (j = 0; j < NINDIRECT; j++) {
      if (a[j]) {
        inbp = bread(ip->dev, a[j]);
        b = (uint*)inbp->data;
        for (k = 0; k < NINDIRECT; k++) {
          if (b[k])
            bfree(ip->dev, b[k]);
        }
        brelse(inbp);
        bfree(ip->dev, a[j]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);
    ip->addrs[NDIRECT+1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

运行**make clean & make qemu**, 重构file system后, 顺利通过**bigfile**和**usertests**测试 (都特别的慢, 要花**5**分钟和**10**分钟, 可能是因为我是在docker里跑Linux系统上再跑qemu).

------

## symbolic links

这个实验是要实现类似**linux**上的**symlin**k软链接功能, 比如**symlink('a', 'b')**后, **open('b')**这个指令会实际上索引到文件**'a'**所对应的文件data block.

首先我们按常规操作, 把一个新的系统调用**sys_symlink**的代号, 跳转函数等等设立好. 然后我们直接开始看**sys_symlink**的实现: 我们把其对应的**inode**的type设为**T_SYMLINK**, 然后在其data block的**[0, MAXPATH]**的范围里写上所要链接的**target path**的路径.

```c
# kernel/sysfile.c

int sys_symlink(char *target, char *path) {
  char kpath[MAXPATH], ktarget[MAXPATH];
  memset(kpath, 0, MAXPATH);
  memset(ktarget, 0, MAXPATH);
  struct inode *ip;
  int n, r;

  if((n = argstr(0, ktarget, MAXPATH)) < 0)
    return -1;

  if ((n = argstr(1, kpath, MAXPATH)) < 0)
    return -1;

  int ret = 0;
  begin_op();

  // 这个软链接已经存在了
  if((ip = namei(kpath)) != 0){
    // symlink already exists
    ret = -1;
    goto final;
  }

  // 为这个软链接allocate一个新的inode
  ip = create(kpath, T_SYMLINK, 0, 0);
  if(ip == 0){
    ret = -1;
    goto final;
  }
  // 把target path写入这个软链接inode的数据[0, MAXPATH]位置内
  if ((r = writei(ip, 0, (uint64)ktarget, 0, MAXPATH)) < 0)
    ret = -1;
  iunlockput(ip);

final:
  end_op();
  return ret;
}
```

然后在**sys_open**这个函数里, 如果传入的**path**是一个软链接的话, 我们需要为用户递归去"***寻址\***", 直到找到第一个不是软链接的**path** (或者递归层数太高, 默认陷入了**死循环**, 直接返回**-1**)

```c
# kernel/sysfile.c

uint64
sys_open(void)
{
  ... 省略, 和之前源码一样 ...

  if(omode & O_CREATE){
    ... 省略, 和之前源码一样 ...
  }

  // 是软链接且O_NOFOLLOW没被设立起来
  int depth = 0;
  while (ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)) {
    char ktarget[MAXPATH];
    memset(ktarget, 0, MAXPATH);
    // 从软链接的inode的[0, MAXPATH]读出它所对应的target path
    if ((r = readi(ip, 0, (uint64)ktarget, 0, MAXPATH)) < 0) {
      iunlockput(ip);
      end_op();
      return -1;
    }
    iunlockput(ip);
    if((ip = namei(ktarget)) == 0){ // target path 不存在
      end_op();
      return -1;
    }

    ilock(ip);
    depth++;
    if (depth > 10) {
      // maybe form a cycle 默认死循环
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  ... 省略, 和之前源码一样 ...
}
```

把symlinktest加入Makefile后进行编译, 顺利通过测试.