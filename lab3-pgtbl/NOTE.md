我们开始第一个题目：`Speed up system call`。

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


