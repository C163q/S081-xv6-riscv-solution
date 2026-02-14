### 我们开始第一个题目：`Speed up system call`。

题目要求每当一个进程被创建，映射一个只读的页至`USYSCALL`（在`memlayout`中定义的虚拟地址）。在这个页开始的地方，
存储一个`struct usyscall`（也在`memlayout.h`中定义），并将其初始化为存储当前进程的PID。对于这个lab，提供了用户
空间的函数`ugetpid`，它会自动使用`USYSCALL`的映射。

接着来看一下`hints`，首先，我们可以在`kernel/proc.c`的`proc_pagetable`函数中实现映射。我们还需要设置合适的
bits来让用户只能读取该页，我们可能还需要用到`mappages`函数。此外，我们需要使用在`allocproc`中申请和初始化页。
此外，我们需要在`freeproc`中释放页。

那么，直接开始吧。

我们先看一下`allocproc`，因为这是调用`proc_pagetable`的唯一函数。它首先会找到一个`UNUSED`的进程位，然后设置`pid`
和`state`，然后分配`trapframe`的页。然后就直接调用`proc_pagetable`了。我们在看一下`proc_pagetable`，它直接将
`trampoline`和`trapframe`的虚地址映射到刚刚分配的空间中。提示告诉我们需要在`allocproc`中申请并初始化页。我们只
需要照着上面这么做就行了。

首先，我们需要让`proc`记录`struct usyscall`存放的物理地址，所以需要在`struct proc`中添加`struct usyscall *usyscall;`
这个字段。`usyscall`是来自`kernel/memlayout.h`的。

然后在`allocproc`的为`trapframe`申请页之后地方，申请`usyscall`的页：

```c
// --snip--

// Allocate a trapframe page.
if((p->trapframe = (struct trapframe *)kalloc()) == 0){
  freeproc(p);
  release(&p->lock);
  return 0;
}

// Allocate a usyscall page.
if ((p->usyscall = (struct usyscall*)kalloc()) == 0) {
  freeproc(p);
  release(&p->lock);
  return 0;
}

// --snip--
```

不得不吐槽一下C没有析构、也没有`defer`，内存和锁的管理就是麻烦。

之后自然是处理`freeproc`当中的内容，我们需要把`p->usyscall`中已分配的页释放了：

```c
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if (p->usyscall)
    kfree((void*)p->usyscall);
  p->usyscall = 0;

// --snip--
```

这边似乎还有一个`proc_freepagetable`需要我们处理，但现在还没有写映射的逻辑，所以先放着。

我们来到`proc_pagetable`函数，按照题目要求进行映射，并设置相应内容：

```c
// --snip--

  if (mappages(pagetable, USYSCALL, PGSIZE,
              (uint64)(p->usyscall), PTE_R | PTE_U) < 0) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }
  p->usyscall->pid = p->pid;

// --snip--
```

`PTE_R`就是只读，`PTE_U`就是用户态可以访问。如果映射失败了，我们还需要对撤销先前的两个映射。最后我们还要对值进行
设置。

最后在`proc_freepagetable`中，加上`uvmunmap(pagetable, USYSCALL, 1, 0);`，因为我们在这之前已经调用了`kfree`了，
并且这部分不属于`p->sz`的部分。

测试后顺利通过：

```
./grade-lab-pgtbl ugetpid
make: 'kernel/kernel' is up to date.
== Test pgtbltest == (1.0s)
== Test   pgtbltest: ugetpid ==
  pgtbltest: ugetpid: OK
```

### 第二个题目：`Print a page table`。

题目要求我们定义一个函数叫作`vmprint`，这个函数接收一个`pagetable_t`的参数，并以如下描述的方法打印该页表。在`exec.c`
中，在`return argc`前插入`if (p->pid == 1) vmprint(p->pagetable)`来打印第一个进程的页表。

页表打印格式如下：

```
page table 0x0000000087f6e000
 ..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
 .. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
 .. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
 .. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
 .. .. ..2: pte 0x0000000021fd9c1f pa 0x0000000087f67000
 ..255: pte 0x0000000021fdb401 pa 0x0000000087f6d000
 .. ..511: pte 0x0000000021fdb001 pa 0x0000000087f6c000
 .. .. ..509: pte 0x0000000021fdd813 pa 0x0000000087f76000
 .. .. ..510: pte 0x0000000021fddc07 pa 0x0000000087f77000
 .. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
```

第一行打印`vmprint`的参数。之后每一行都是一个`PTE`，每个`PTE`都由一系列的` ..`来缩进，代表树的深度。每个`PTE`行显示
了页表页当中`PTE`的索引，`PTE`的bits和从`PTE`中提取出的物理地址，不打印无效的`PTE`。在上面的例子中，最顶层的页表页
映射了第0项和第255项。下一层仅映射了第0项，最底层则映射了第0、1、2项。

接下来看一下`hints`。首先我们可以把`vmprint`定义在`kernel/vm.c`中。使用`kernel/riscv.h`文件最后定义的几个宏。函数
`freewalk`可以给一些提示。在`kernel/defs.h`中声明`vmprint`从而可以在`exec.c`中调用。使用`%p`来打印指针。

那么，我们首先看一下`freewalk`函数：

```c
// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}
```

这边基本上已经涵盖了大部分所需的步骤了，只需要稍微把`free`变成打印，加上对叶结点的处理即可。于是就可以得到
如下函数：

```c
void vmprint_inner(pagetable_t pagetable, int level) {
  for (int i = 0; i < 512; ++i) {
    pte_t pte = pagetable[i];
    if (pte & PTE_V) {
      uint64 pa = PTE2PA(pte);
      for (int j = 0; j <= level; ++j) {
        printf(" ..");
      }
      printf("%d: pte %p pa %p\n", i, pte, pa);
      if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
        vmprint_inner((pagetable_t)pa, level + 1);
      }
    }
  }
}

void vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);
  vmprint_inner(pagetable, 0);
}
```

我们在`kernel/defs.h`中加上相应的声明：`void vmprint(pagetable_t);`。然后在`exec.c`中加上相应的函数调用：

```c
// --snip--

proc_freepagetable(oldpagetable, oldsz);

if (p->pid == 1) {
  vmprint(pagetable);
}

return argc; // this ends up in a0, the first argument to main(argc, argv)

// --snip--
```

启动内核看看：

```
xv6 kernel is booting

hart 2 starting
hart 1 starting
page table 0x0000000087f6e000
 ..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
 .. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
 .. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
 .. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
 .. .. ..2: pte 0x0000000021fd9c1f pa 0x0000000087f67000
 ..255: pte 0x0000000021fdb401 pa 0x0000000087f6d000
 .. ..511: pte 0x0000000021fdb001 pa 0x0000000087f6c000
 .. .. ..509: pte 0x0000000021fdd813 pa 0x0000000087f76000
 .. .. ..510: pte 0x0000000021fddc07 pa 0x0000000087f77000
 .. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
init: starting sh
```

应该是正确的，我们跑一下测试看看：

```
./grade-lab-pgtbl pte
make: 'kernel/kernel' is up to date.
== Test pte printout == pte printout: OK (1.3s)
```

成功通过测试。

### 下一个题目：`Detecting which pages have been accessed`

我们需要实现`pgaccess`函数，这个函数会报告访问过的页。这个系统调用接收三个参数，第一个参数是要检查的第一个
用户页，第二个是要检查的页的数量，第三个是一个指向缓冲区的指针，这个缓存区存储一个bitmask作为结果，一位代表
一页，第一页对应最低的bit位。

我们还可以看到，题目提到：

> The RISC-V hardware page walker marks these bits in the PTE whenever it resolves a TLB miss.

我认为这个bit应该是`PTE_A`，如果说TLB当中没有的话，就会在`PTE`中直接设置`PTE_A`位。在提示中提到，我们需要在
调用`pgaccess`的同时，清除`PTE_A`位，因为下次访问时，我们需要知道在上次调用了`pgaccess`后，又有哪些页被访问，
而系统调用时，由于要切换页表，所以`TLB`会被刷新，所以依赖`PTE_A`是可行的。

我们首先需要在`kernel/sysproc.c`中实现`sys_pgaccess`。它由于要解析三个参数，在`user/user.h`中声明为：
`int pgaccess(void *base, int len, void *mask);`，所以需要`argaddr()`和`argint()`。

于是：`if (argaddr(0, &base) < 0 || argint(1, &len) < 0 || argaddr(2, &ret) < 0) return -1;`。

提示中提到，可以设置扫描的页的上限，我们这里就设置为64，这样返回的mask就可以设置为一个`uint64`了。而测试样例中
似乎使用的是32位。于是：`if (len < 0 || len > 64) return -1;`

我们需要对每个页进行遍历，这就需要得出这些页的虚地址，然后得到它的页表项，这需要用到`walk`函数。但`walk`函数
没有作为接口暴露出来，所以我们将它声明在`kernel/defs.h`中：`pte_t* walk(pagetable_t pagetable, uint64 va, int alloc);`

我们还需要用到`PTE_A`，但是`kernel/riscv.h`中没有定义，查看`RISC-V privileged architecture manual`后可以知道，
对于`Sv39`页表项，结构为：

```
| Reserved | PPN[2] | PPN[1] | PPN[0] |  RSW  | D | A | G | U | X | W | R | V |
|  10bits  | 26bits | 9bits  | 9bits  | 2bits | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
```

所以说，应该定义为`#define PTE_A (1L << 6)`。

有了这些，就可以在一个循环中逐步完成`mask`的内容了：

```c
// --snip--

pagetable_t pagetable = myproc()->pagetable;
for (int i = 0; i < len; ++i) {
  uint64 va = base + i * PGSIZE;
  if (va >= MAXVA)
    return -1;

  pte_t* pte = walk(pagetable, va, 0);
  if (pte == 0)
    return -1;
  if ((*pte & PTE_V) == 0)
    return -1;
  if ((*pte & PTE_U) == 0)
    return -1;
  if ((*pte & PTE_A) != 0) {
    mask |= (1L << i);
    *pte &= ~PTE_A;
  }
}

// --snip--
```

最后还需要`copyout`：`if (copyout(pagetable, ret, (char*)&mask, sizeof(mask)) < 0) return -1;`。

完成！我们跑一下测试：

```
./grade-lab-pgtbl pgaccess
make: 'kernel/kernel' is up to date.
== Test pgtbltest == (0.9s)
== Test   pgtbltest: pgaccess ==
  pgtbltest: pgaccess: OK
```

成功通过。

最后跑一下所有测试：

```
./grade-lab-pgtbl
make: 'kernel/kernel' is up to date.
== Test pgtbltest == (1.3s)
== Test   pgtbltest: ugetpid ==
  pgtbltest: ugetpid: OK
== Test   pgtbltest: pgaccess ==
  pgtbltest: pgaccess: OK
== Test pte printout == pte printout: OK (1.0s)
== Test answers-pgtbl.txt == answers-pgtbl.txt: OK
== Test usertests == (182.9s)
== Test   usertests: all tests ==
  usertests: all tests: OK
== Test time ==
time: OK
Score: 46/46
```

虽然`usertests`跑了3分钟，但最后还是通过了。

