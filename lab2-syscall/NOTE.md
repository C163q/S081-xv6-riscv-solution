我们现在要处理的毕竟是kernel，所以一般的调试程序调试方法肯定是不行的了，直接打开`riscv64-linux-gnu-gdb`，
gdb会说不知道如何运行。

网上的资料说是要使用`make qemu-gdb`来运行kernel，此时会显示：

```
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 3 -nographic -drive
file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 -S -gdb tcp::26000
```

首先用`riscv64-linux-gnu-gdb kernel/kernel`来加载程序的符号表，然后使用`target remote localhost:26000`连接远程
调试目标。

尝试下来一切正常，入口为`_entry`，所以只要`b _entry`就可以从头开始调试了。

在看了课程之后，我看见好像在指定参数`CPUS=1`之后，就可以以单核的方式运行了：`make CPUS=1 qemu-gdb`。

**来看实验。**

首先是`System call tracing`，实验要求我们增加一个系统调用追踪特性，我们需要创建一个新的系统调用`trace`
来控制tracing。它接收一个整形参数`mask`，来指定追踪哪个系统调用。例如，为了追踪`fork`系统调用，程序需要调用
`trace(1 << SYS_fork)`，`SYS_fork`是`kernel/syscall.h`指定的数字。我们必须修改`xv6`内核以便当指定位被设置是，
在系统调用即将返回前打印一行文字，这行文字包括进程ID、系统调用名称和返回值，无须打印系统调用参数。`trace`系统
调用为进程及接下来fork的子进程启用tracing，但不会影响其他进程。

接下来给了一些提示，基本上是我们要做的事情。首先，我们在`Makefile`中的`UPROGS`中加入`$U/_trace`，不然
`user/trace.c`不会编译并加到`fs.img`中。

然后，我们现在需要在`user/user.h`中加入函数声明`int trace(int);`，在`user/usys.pl`中加入`entry("trace");`，
然后在`kernel/syscall.h`中加入系统调用号`#define SYS_trace  22`。

接着，我们需要在`kernel/sysproc.c`中实现函数`sys_trace()`。我们来分析一下，既然tracing的mask是进程之间独立的，
那么这个数据必定是存储在与进程关联的数据结构中的，这个数据结构应该还包括类似pid之类的内容。很明显，就是`kernel/proc.h`
中的`struct proc`了。添加`int trace_mask`字段：

```c
// Per-process state
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  int trace_mask;              // Mask of traced system calls

  // wait_lock must be held when using this:
  struct proc *parent;         // Parent process

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};
```

对照其它函数获取参数的方法，此时`sys_trace`就很好实现了：

```c
uint64
sys_trace(void)
{
  int new_mask;

  if (argint(0, &new_mask) < 0)
    return -1;

  myproc()->trace_mask = new_mask;
  return 0;
}
```

我们还需要修改`kernel/proc.c`当中的`fork`函数，因为要求中提到之后fork的子进程也应该有相同的`mask`。
```c
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

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // copy trace mask
  np->trace_mask = p->trace_mask;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}
```

最后，我们需要修改`kernel/syscall.c`中的`syscall`来打印tracing。但这要求我们拥有系统调用名称的字符串，
我搜了整个项目，没有这种东西，所以需要我们手动添加：

```c
extern uint64 sys_trace(void);

static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_trace]   sys_trace,
};

static const char *syscall_names[] = {
  "fork", "exit", "wait", "pipe", "read",
  "kill", "exec", "fstat", "chdir", "dup",
  "getpid", "sbrk", "sleep", "uptime", "open",
  "write", "mknod", "unlink", "link", "mkdir",
  "close", "trace"
};
```

哦，还有，上面系统调用表也不能忘记添加`SYS_trace`索引与`sys_trace`函数的对应关系。

`syscall`函数则修改为：

```c
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();
    if (p->trace_mask & (1 << num)) {
      printf("%d: syscall %s -> %d\n",
          p->pid, syscall_names[num-1], p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

这里有一点要注意一下，我在使用`printf`的时候，发现使用`%ld`或者`%l`没有用，因为我看到`user/printf.c`内会把`%l`
视作是打印`uint64`，而这里的`p->trapframe->a0`是uint64类型的。但是实际上这里使用的`printf`是来自`kernel/printf.c`
的，里面没有打印`uint64`的。而且后来一想，这里也不适和打印`uint64`，毕竟`user/user.h`里面声明的返回值都是`int`型，
只不过寄存器都是64位的罢了。

我们先使用`trace`程序测试一下：

```
$ trace 32 grep hello README
3: syscall read -> 1023
3: syscall read -> 968
3: syscall read -> 235
3: syscall read -> 0
```

看起来不错。接下来看一下测试样例能不能通过：
```
$ ./grade-lab-syscall trace
make: 'kernel/kernel' is up to date.
== Test trace 32 grep == trace 32 grep: OK (1.1s)
== Test trace all grep == trace all grep: OK (0.9s)
== Test trace nothing == trace nothing: OK (1.0s)
== Test trace children == trace children: OK (13.1s)
```

成功通过。

下一个实验，`Sysinfo`。这个实验需要我们加入一个叫`sysinfo`的系统调用，这个系统调用获取运行的系统的信息。
该系统调用获取一个参数：指向`struct sysinfo`的指针。内核必须将这个结构体的所有字段都填充完毕。`freemem`字段
应当被设置为剩余内存的字节数，`nproc`字段应当被设置为`state`不为`UNUSED`的进程的数量。此处有一个叫`sysinfotest`
的测试程序，如果正确就会打印`"sysinfotest: OK"`。

我们仍然是按照它的`hint`来写。首先，在`Makefile`文件中的`UPROGS`中加入`$U/_sysinfotest`。然后按照上一个实验的
步骤修改特定的文件。

提示里面也说了，必须首先在`user/user.h`中声明`struct sysinfo`，才能够声明`sysinfo`函数。

接下来我们在`kernel/sysproc.c`中来实现一下`sys_sysinfo`。首先我们需要得到剩余的内存字节数，在提示说需要在
`kernel/kalloc.c`当中添加这个函数。我看了一下这个文件里面的结构和函数，我想`kmem`应该是管理空闲的物理块的，
将所有空闲的物理块的首地址通过链表的形式首尾连接了起来，这个链表的结尾应该是`NULL`。

所以计算空闲内存的函数就很好写了。我们这边先写一个计算空闲内存物理块的函数，然后后续自行乘以`PGSIZE`是一个不错
的做法，因为这里的函数都是以物理块的大小为单位的：

```c
// Available physical memory in pages.
uint64
kavail(void) {
  uint64 count = 0;
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  while (r) {
    count++;
    r = r->next;
  }
  release(&kmem.lock);

  return count;
}
```

我们此处使用了`acquire(&kmem.lock)`和`release(&kmem.lock)`，虽然没有学到为什么要用到这个，但是我大概可以猜测是
因为在并行环境下，如果多个核同时执行处理物理内存块的代码，就会导致`kmem`这个数据结构出问题。同理，我们在此处
需要访问`kmem`，我们不希望其他进程修改`kmem`。

顺便，这个还是的声明还需要加到`kernel/defs.h`中，不过，这里面函数的返回值要么是`int`要么是`void`，总感觉不太
应该返回一个`uint64`。

我们还需要获取进程的数量，`hint`中告诉我们需要在`kernel/proc.c`中写上相应的代码。我们看一下`allocproc`是怎么
写的：

```c
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  // --snip--
```

可以看到，`proc`是一个类似于进程池的东西，里面存了使用的和未使用的进程名额。上面的程序由于是申请一个进程名额，
所以在找到第一个`state`为`UNUSED`的进程后，就跳到`found`了。也就是说，我们的进程数量计算函数只要数`state`不为
`UNUSED`的进程的数量就可以了。那么这样程序就很好写了：

```c
// Get the number of processes
int
proc_count(void)
{
  int count = 0;
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state != UNUSED) {
      count++;
    }
  }

  return count;
}
```

这边我没有写`acquire`和`release`，原因是这边我们仅对`struct proc`的`state`字段进行一次读取，并且没有修改任何字段，
并且`proc`本身只是结构体，存储在一个数组而非链表中，所以我认为不写`acquire`和`release`也是没有问题的。

最后在`kernel/defs.h`上加上声明，这些前置工作就完成了。

接下来，我们在`kernel/sysproc.c`中加上相应的实现。我们首先需要获取地址参数，所以需要`argaddr`，我们也不能直接向该
地址写入数据，因为这是进程的虚地址，而函数处于内核当中，此时硬件当中并不是进程的页表，所以我们必须手动获取进程的
页表，也就是`struct proc* p = myproc(); p->pagetable`。仿照`kernel/sysfile.c`中的`sys_fstat`和`kernel/file.c`中的
`filestat`，我们可以得知使用`copyout`来将`sysinfo`拷贝到指定物理地址当中。因此，就可以写出这样的函数：

```c
uint64
sys_sysinfo(void) {
  uint64 addr;
  struct sysinfo info;
  struct proc* p = myproc();

  if (argaddr(0, &addr) < 0)
    return -1;

  info.freemem = kavail() * PGSIZE;
  info.nproc = proc_count();
  if (copyout(p->pagetable, addr, (char*)&info, sizeof(info)) < 0)
    return -1;

  return 0;
}
```

要注意`kavail()`的结果要乘以`PGSIZE`。最后，像上个实验那样处理`kernel/syscall.c`中的内容，然后就结束了。

我们来跑一下测试程序`sysinfotest`：

```
$ sysinfotest
sysinfotest: start
sysinfotest: OK
```

既然通过了，那就跑一下测试：

```c
$ ./grade-lab-syscall sysinfo
make: 'kernel/kernel' is up to date.
== Test sysinfotest == sysinfotest: OK (2.5s)
```

测试也通过了。

最后似乎还需要一个`time.txt`文件，写上完成这个实验花了多长时间。

```
./grade-lab-syscall
make: 'kernel/kernel' is up to date.
== Test trace 32 grep == trace 32 grep: OK (0.7s)
== Test trace all grep == trace all grep: OK (0.9s)
== Test trace nothing == trace nothing: OK (1.1s)
== Test trace children == trace children: OK (14.7s)
== Test sysinfotest == sysinfotest: OK (1.6s)
== Test time ==
time: OK
Score: 35/35
```

