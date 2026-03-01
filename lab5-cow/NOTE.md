### Implement copy-on write

这个实验就只有这一个题目。不过2021年的实验似乎没有`lab lazy`，所以有些东西必须参考去年的实验。

这个实验的提示并没有给太多的信息，所以我们就自行思考。

我们先设置一块内存，用于引用计数：

```c
struct {
  struct spinlock lock;
  struct run *freelist;
  signed char mem_rc[(PHYSTOP - KERNBASE) / PGSIZE];
} kmem;
```

然后修改一下`kfree`的逻辑，首先减少引用计数，当引用计数记为0时，进行free操作：

```c
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP
    || kmem.mem_rc[((uint64)pa - KERNBASE) / PGSIZE] <= 0)
    panic("kfree");

  uint64 index = ((uint64)pa - KERNBASE) / PGSIZE;
  if (kmem.mem_rc[index] > 1) {
    kmem.mem_rc[index]--;
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  kmem.mem_rc[index] = 0;

  // --snip--
}
```

此外，`kalloc`在分配内存时，也必须将引用计数记为1。

```c
void *
kalloc(void)
{
  // --snip--
  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    kmem.mem_rc[((uint64)r - KERNBASE) / PGSIZE] = 1;
  }
  return (void*)r;
}
```

接下来，我们还需要使用页表项中预留给系统（RSW）的那两位：

```text
| Reserved | PPN[2] | PPN[1] | PPN[0] |  RSW  | D | A | G | U | X | W | R | V |
|  10bits  | 26bits | 9bits  | 9bits  | 2bits | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
```

我们先定义好一位：

```c
#define PTE_RSW1 (1L << 8)
```

然后就是处理`sys_fork`函数了，这个函数仅仅是调用了`fork`函数，在这个函数中，最关键的两个函数应该就是
`uvmcopy`和`freeproc`了。

我们首先修改`uvmcopy`的逻辑，让它不分配内存而是仅仅将虚地址映射到相同的物理块中，并且，对于只读的块，
我们没有必要设置PTE中`RSW`的位，因为这不可能触发COW，而对于可读的块，则必须屏蔽W，并且设置`RSW`位。
为了方便处理这些，首先在`kalloc.c`中定义一个`void cowalloc(void*);`，具体如下：

```c
void
cowalloc(void *pa) {
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP
    || kmem.mem_rc[((uint64)pa - KERNBASE) / PGSIZE] <= 0)
    panic("cowalloc");

  uint64 index = ((uint64)pa - KERNBASE) / PGSIZE;
  kmem.mem_rc[index]++;
}
```

它唯一做的事情就是将引用计数加1，因为`kmem`是仅存在于该文件中的。

修改后的`uvmcopy`如下：

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags, new_flags;
  // char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    // --snip--
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);

    cowalloc((void*)pa);
    if (flags & PTE_W) {
      flags &= ~PTE_W;
      flags |= PTE_RSW1;
      *pte = PA2PTE(pa) | flags;
    }
    new_flags = flags;

    if (mappages(new, i, PGSIZE, pa, new_flags) != 0) {
      kfree((void*)pa);
      goto err;
    }
    // --snip--
}
```

该函数最后的`uvmunmap`逻辑不需要任何变化，因为我们已经修改了`kfree`的逻辑，只在引用计数为0时才真正执
行free操作。

而`freeproc`函数内的操作最后都是调用`kfree`函数，目前来看不需要什么特殊处理。

最后就是`usertrap`中处理`page fault`的逻辑了。由于`scause`为`13`时是Load page fault，而`15`时是
Store/AMO page fault，所以我们这里只需要判断`r_scause() == 15`即可，在此情况下判断`PTE_RSW1`并执行
`copy`的逻辑。对于`scause`为13的情况，我们无须担心，因为我们只屏蔽了`W`权限。

对于`copy`的逻辑，可以借鉴`uvmcopy`当中原先的代码，其实就是分配一块新的内存空间，并将对应的虚页映射
到新的空间当中去，并且加上了`PTE_W`并去除了`PTE_RSW1`，最后要将原先物理空间当中的内存块的引用计数减1。

最后如果原内存空间的引用计数减为1的话，那么原内存空间中的`PTE_W`也可以置位了，并且`PTE_RSW1`要清除。
代码如下：

```c
void* cow(void* va, pagetable_t pagetable) {
  if ((uint64)va >= MAXVA || (uint64)va % PGSIZE != 0)
    panic("cow");

  pte_t* pte = walk(pagetable, (uint64)va, 0);
  if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 || (*pte & PTE_RSW1) == 0)
    return 0;

  // PTE_RSW1 == 1
  char* mem;
  uint64 pa = PTE2PA(*pte);
  uint64 flags = PTE_FLAGS(*pte);
  int rc = kmemrc((void*)pa);

  if (rc <= 0) {
    panic("cow");
  }

  if (rc == 1) {
    *pte |= PTE_W;
    *pte &= ~PTE_RSW1;
    return (void*)pa;
  }

  uvmunmap(pagetable, (uint64)va, 1, 1);

  if ((mem = kalloc()) == 0) {
    return 0;
  }
  memmove(mem, (char*)pa, PGSIZE);
  if (mappages(pagetable, (uint64)va, PGSIZE, (uint64)mem, (flags | PTE_W) & ~PTE_RSW1) != 0) {
    kfree(mem);
    return 0;
  }

  if (kmemrc((void*)pa) == 1) {
    *pte |= PTE_W;
    *pte &= ~PTE_RSW1;
  }

  return mem;
}

// vm.c ^^^ / vvv trap.c

// --snip--
  if(r_scause() == 8){
    // system call
    // --snip--
  } else if (r_scause() == 15) {
    // store page fault
    uint64 va = PGROUNDDOWN(r_stval());
    if(va >= MAXVA || r_stval() > MAXVA) {
      p->killed = 1;
    }
    else if (cow((void*)va, p->pagetable) == 0) {
      p->killed = 1;
    }
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
kill:
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
// --snip--
```

这里需要特别注意内存的引用计数为1的情况，这种情况是存在的，因为可能`fork`之后从未写某个块，然后程序
就结束了，这种情况下就会导致在`cow`中出现引用计数为1。这个时候使用`uvmunmap`就会把那块内存销毁，这是
我们不希望看见的情况，所以特殊处理，并且让它不执行`uvmunmap`的代码。

最后，要求当中还包括了`copyout()`函数，这个函数也需要处理COW的情况。由于我们已经将`cow`给抽象出来了，
所以这个函数的修改也很好解决，只需要判断要修改的页是否有`PTE_RSW1`标志位即可。

```c
while(len > 0){
  va0 = PGROUNDDOWN(dstva);
  // pa0 = walkaddr(pagetable, va0);
  if (va0 >= MAXVA)
    return -1;
  pte_t *pte = walk(pagetable, va0, 0);
  if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
    return -1;
  pa0 = PTE2PA(*pte);

  if (*pte & PTE_RSW1) {
    if ((pa0 = (uint64)cow((void*)va0, pagetable)) == 0)
      return -1;
  }

// --snip--
```

在调试的时候，我还发现`kfree`上方的注释：

```c
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa) {
  // --snip--
}
```

也就是说，我们也必须修改一下`freerange`，在调用`kfree`前先让引用计数设置为1：

```c
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    uint64 index = ((uint64)p - KERNBASE) / PGSIZE;
    kmem.mem_rc[index] = 1;
    kfree(p);
  }
}
```

我们来测试一下：

```shell
$ cowtest
simple: ok
simple: ok
three: ok
three: ok
three: ok
file: ok
ALL COW TESTS PASSED
```

测试一下总的测试：

```shell
 ./grade-lab-cow
make: 'kernel/kernel' is up to date.
== Test running cowtest == (5.3s)
== Test   simple ==
  simple: OK
== Test   three ==
  three: OK
== Test   file ==
  file: OK
== Test usertests == (197.0s)
== Test   usertests: copyin ==
  usertests: copyin: OK
== Test   usertests: copyout ==
  usertests: copyout: OK
== Test   usertests: all tests ==
  usertests: all tests: OK
== Test time ==
time: OK
Score: 110/110
```

在几次修改之后也是通过了。不过可以明显看出来要花的时间变多了，估计是因为加了COW的原因。
