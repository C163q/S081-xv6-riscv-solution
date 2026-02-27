### 首先是第一个题目`RISC-V assembly`

这题要求我们掌握一点`RISC-V`的汇编，我目前还不了解`RISC-V`，所以这题会有一点难度。题解要求是写在
`answers-traps.txt`文件中。

第一问是哪些寄存器包含了函数的参数？例如，那个寄存器包含了`main`中对`printf`调用的参数`13`？

通过阅读`riscv-calling.pdf`，我们就可以知道，`a0-a7`是存放参数的，不过，这是针对整型和指针而言的。
我们查看`call.asm`中可以发现，在`main`的`printf("%d %d\n", f(8)+1, 13);`有汇编`li a2,13`，因此，是
`a2`包含了参数`13`。

第二问是`main`中调用`f`的汇编代码在哪？调用`g`的汇编呢？

我们可以看到原函数是这样的：

```c
int g(int x) {
  return x+3;
}

int f(int x) {
  return g(x);
}

void main(void) {
  printf("%d %d\n", f(8)+1, 13);
  exit(0);
}
```

编译器似乎直接把调用`f`和`g`的代码内联了，只剩下：`li a1,12`。

第三问是函数`printf`的地址在哪里？

我们在汇编中可以看见：

```asm
  3c:	606080e7          	jalr	1542(ra) # 63e <printf>
```

因此，`printf`的地址是`0x63e`。

第四问是在`main`中执行`jalr printf`之后，寄存器`ra`的值是什么。

我们在`riscv-calling.pdf`中可以看到`ra`保存`Return address`，因此，`ra`应该存储`main`中下一条指令的
地址，也就是`0x40`。

第五问是运行如下代码：

```c
unsigned int i = 0x00646c72;
printf("H%x Wo%s", 57616, &i);
```

输出是什么？这个输出结果的原因是`RISC-V`是小端序的，如果`RISC-V`是大端的话，应该设置`i`为什么值来
产生相同的输出？是否需要将`57616`改为不同的值？

虽然目前我是在x86-64环境下的，但是`unsigned int`都是32位，并且都是小端，所以运行结果应该都是一样的。

结果是`He110 World`。

如果在大端还要有相同的输出的话，`i`就必须是`0x726c6400`了。而`57616`则无须改变。

第六问是在如下代码中，`"y="`后面将会输出什么？为什么会这样？

```c
printf("x=%d y=%d", 3);
```

像`printf`这样的函数，属于`variadic functions`，这种函数通过C中`stdarg.h`给出的`va_list`来使用后续的
参数。本质还是使用了特定的寄存器或者栈。对于上面的调用，函数调用方是不会给`a2`寄存器传入任何值的，
但函数当中会尝试使用`a2`寄存器，这就导致了未定义行为，我们无法确定到底会输出什么。

### 第二题是`Backtrace`

它要求我们在`kernel/printf.c`中实现`backtrace`函数，并在`sys_sleep`中插入对这个函数的调用，然后运行
`bttest`，这个函数会调用`sys_sleep`，并产生类似如下输出：

```text
backtrace:
0x0000000080002cda
0x0000000080002bb6
0x0000000080002898
```

这个输出可能会有所不同，但是当运行`addr2line -e kernel/kernel`（或`riscv64-unknown-elf-addr2line
-e kernel/kernel`）并将上面的地址像下面这样输入之后：

```shell
$ addr2line -e kernel/kernel
0x0000000080002de2
0x0000000080002f4a
0x0000000080002bfc
Ctrl-D
```

应当看到类似如下输出：

```text
kernel/sysproc.c:74
kernel/syscall.c:224
kernel/trap.c:85
```

这个很显然要利用函数调用时会将返回值压入栈中的特性。
我们先跟着下面的提示做：

我们首先要在`kernel/defs.h`中定义`backtrace`函数：`void backtrace(void);`。

由于寄存器`s0`中存放当前运行的函数的栈顶部，为了获取它，需要在`kernel/riscv.h`中定义如下代码：

```c
static inline uint64
r_fp()
{
  uint64 x;
  asm volatile("mv %0, s0" : "=r" (x) );
  return x;
}
```

有了栈顶部（高地址）之后，由于栈是向低地址方向增长的，可以知道当前函数的栈如下：

```text
+--------------------+ <- fp
|   Return Address   | 8 bytes
+--------------------+
| To Prev Frame (fp) | 8 bytes
+--------------------+
|  Saved Registers   |
+--------------------+
|  Local Variables   |
+--------------------+
```

函数的返回地址在`fp - 8`的位置，而上一个帧的fp在`fp - 16`的位置。

由于`xv6`在内核只为每个进程分配一个内核栈，并且地址都是对齐一页的，所以可以轻松地计算出栈顶和栈底的
地址，只需要使用`PGROUNDDOWN(fp)`和`PGROUNDUP(fp)`即可。

这样思路就很清楚了，首先，可以通过`return address`推导出函数调用的地址。不过这里不确定到底要打印的是
什么地址，因此就暂时先把`return address`打印出来。接着再跳转到`To Prev Frame`当中的内容，继续上面的
操作。直到最后超过了`PGROUNDDOWN(fp)`和`PGROUNDUP(fp)`的范围。

函数实现如下：

```c
void backtrace(void)
{
  uint64 fp = r_fp();
  uint64 stack_top = PGROUNDUP(fp);
  uint64 stack_bottom = PGROUNDDOWN(fp);

  printf("backtrace:\n");

  while (fp >= stack_bottom && fp < stack_top) {
    uint64 ra = *((uint64*)(fp - 8));
    printf("%p\n", ra);
    fp = *((uint64*)(fp - 16));
  }
}
```

在`kernel/sysproc.c`中的`sys_sleep`中加入这个函数：

```c
uint64
sys_sleep(void)
{
  backtrace();
// --snip--
```

我们来运行一下`bttest`试试：

```shell
$ bttest
backtrace:
0x0000000080002166
0x0000000080002042
0x0000000080001d2a
```

感觉应该是对了，跑一下测试：

```text
./grade-lab-traps backtrace
make: 'kernel/kernel' is up to date.
== Test backtrace test == backtrace test: OK (1.1s)
```

果然，只需要打印返回的地址即可。

### 接下来是`Alarm`

我们需要加入一个新的`sigalarm(interval, handler)`系统调用。如果一个程序调用`sigalarm(n, fn)`，那么
程序每消耗`n` ticks的CPU时间，内核就应该让程序调用`fn`函数。当`fn`函数返回时，程序应该恢复被中断的
位置。一个tick在xv6是任意的一个时间单位，由硬件时钟产生一个中断的频率决定。如果一个函数调用了
`sigalarm(0, 0)`，内核应该停止产生周期性的中断。

`user/alarmtest.c`就是测试代码，需要我们实现了`sigalarm`和`sigreturn`之后才会编译。

`alarmtest`在`test0`中调用`sigalarm(2, periodic)`来让内核每2ticks调用`periodic`。`alarmtest`产生的
输出如下所示：

```shell
$ alarmtest
test0 start
........alarm!
test0 passed
test1 start
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
test1 passed
test2 start
................alarm!
test2 passed
```

我们首先修改一下`Makefile`，让`user/alarmtest.c`编译。

```Makefile
UPROGS=\
    // --snip--
    $U/_alarmtest\
```

我们先尝试让`test0`输出`alarm!`。

首先需要在`user/user.h`声明函数`sigalarm`和`sigreturn`：

```c
int sigalarm(int ticks, void (*handler)());
int sigreturn(void);
```

然后更新`user/usys.pl`，`kernel/syscall.h`和`kernel/syscall.c`来让`alarmtest`能够调用`sigalarm`和
`sigreturn`。这些都是之前的实验做过的东西。

接下来，对于`sys_sigreturn`，暂时就返回`0`。而`sys_sigalarm`应该将`interval`和`handler`存储在`proc`
结构体当中的字段内。我们还应该要在`proc`结构体中存储自从上次调用`handler`之后，经过了多少个`ticks`。
可以在`kernel/proc.c`中的`allocproc`函数中初始化这些字段。

所以，`struct proc`的定义就是这样：

```c
// Per-process state
struct proc {
  // --snip--
  int ticks;
  int elapsed_ticks;
  void (*alarm_handler)(void);
  // --snip--
};
```

而`sys_sigreturn`和`sys_sigalarm`定义为这样：

```c
uint64
sys_sigreturn(void)
{
  return 0;
}

uint64
sys_sigalarm(void)
{
  uint64 addr;
  int n;

  if (argint(0, &n) < 0 || argaddr(1, &addr) < 0)
    return -1;

  struct proc *p = myproc();
  p->ticks = n;
  p->alarm_handler = (void*)addr;
  p->elapsed_ticks = 0;

  return 0;
}
```

我们知道，当从用户空间中中断，会跳转到`kernel/trap.c`中的`usertrap`函数。而由计时器中断时，
`which_dev == 2`，因此，我们在`if (which_dev == 2) ...`分支下进行修改：

```c
// --snip--
if(which_dev == 2) {
  // --snip--
  if (p->alarm_handler || p->ticks) {
    p->elapsed_ticks += 1;
    if (p->elapsed_ticks >= p->ticks) {
      p->elapsed_ticks = 0;
      p->trapframe->epc = (uint64)p->alarm_handler;
    }
  } else {
    p->elapsed_ticks = 0;
  }
}
// --snip--
```

这里需要注意的是，有些测试样例中，`alarm_handler`的地址真的就是0，并且确实是有函数的，所以这里的条件
判断需要非常注意。

如果`p->alarm_handler`为`NULL`且`p->ticks`为0，那么就不需要执行`alarm`的逻辑，反之，那么就加
`elapsed_ticks`，如果`elapsed_ticks`超过设定的`ticks`，那么就调用`alarm_handler`，具体来说就是修改
`trapframe`当中的`epc`，使得`sret`后跳转到`alarm_handler`的位置。不过，我们目前还暂时没有处理
`sigreturn`，所以必然是会崩溃的。

不过我们先测试一下：

```shell
$ alarmtest
test0 start
.........................................alarm!
test0 passed
test1 start
...alarm!
..alarm!
..alarm!
..alarm!
..alarm!
..alarm!
..alarm!
..alarm!
..alarm!
..alarm!

test1 failed: foo() executed fewer times than it was called
usertrap(): unexpected scause 0x0000000000000002 pid=4
            sepc=0x0000000000000062 stval=0x0000000000000000
```

不知为何，`test0`确实是过了。先放一边，我们先处理`sys_sigreturn`。

在所有的`handler`当中，都是以`sys_sigreturn`结束的，所以我们可以在这个函数中恢复。由于要恢复的内容
包括寄存器以及`epc`，所以方便起见，不妨将整个`trapframe`都保存下来。我们还需要注意，必须保证`handler`
不能被重入，所以必须有个`bool`记录是否正在处理`handler`，然后在`sys_sigreturn`中恢复。

于是，`struct proc`就定义为：

```c
// Per-process state
struct proc {
  // --snip--
  char alarm_guard;                 // guard against reentrant calls to alarm handler
  int ticks;
  int elapsed_ticks;
  void (*alarm_handler)(void);
  struct trapframe alarm_trapframe; // to save trapframe when handling alarm
  // --snip--
};
```

而`usertrap`当中处理`alarm`的地方则修改为：

```c
// --snip--
if(which_dev == 2) {
  // --snip--
  if ((p->alarm_handler || p->ticks) && !(p->alarm_guard)) {
    p->elapsed_ticks += 1;
    if (p->elapsed_ticks >= p->ticks) {
      p->alarm_guard = 1;
      p->elapsed_ticks = 0;
      p->alarm_trapframe = *(p->trapframe);
      p->trapframe->epc = (uint64)(p->alarm_handler);
    }
  } else {
    p->elapsed_ticks = 0;
  }
}
// --snip--
```

我们需要处理重入问题，所以要使用`alarm_guard`，此外为了恢复上下文，需要保存`trapframe`。

最后是`sys_sigreturn`函数：

```c
uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  p->trapframe->epc = p->alarm_trapframe.epc;
  p->trapframe->ra = p->alarm_trapframe.ra;
  p->trapframe->sp = p->alarm_trapframe.sp;
  p->trapframe->gp = p->alarm_trapframe.gp;
  p->trapframe->tp = p->alarm_trapframe.tp;
  p->trapframe->t0 = p->alarm_trapframe.t0;
  p->trapframe->t1 = p->alarm_trapframe.t1;
  p->trapframe->t2 = p->alarm_trapframe.t2;
  p->trapframe->s0 = p->alarm_trapframe.s0;
  p->trapframe->s1 = p->alarm_trapframe.s1;
  p->trapframe->a0 = p->alarm_trapframe.a0;
  p->trapframe->a1 = p->alarm_trapframe.a1;
  p->trapframe->a2 = p->alarm_trapframe.a2;
  p->trapframe->a3 = p->alarm_trapframe.a3;
  p->trapframe->a4 = p->alarm_trapframe.a4;
  p->trapframe->a5 = p->alarm_trapframe.a5;
  p->trapframe->a6 = p->alarm_trapframe.a6;
  p->trapframe->a7 = p->alarm_trapframe.a7;
  p->trapframe->s2 = p->alarm_trapframe.s2;
  p->trapframe->s3 = p->alarm_trapframe.s3;
  p->trapframe->s4 = p->alarm_trapframe.s4;
  p->trapframe->s5 = p->alarm_trapframe.s5;
  p->trapframe->s6 = p->alarm_trapframe.s6;
  p->trapframe->s7 = p->alarm_trapframe.s7;
  p->trapframe->s8 = p->alarm_trapframe.s8;
  p->trapframe->s9 = p->alarm_trapframe.s9;
  p->trapframe->s10 = p->alarm_trapframe.s10;
  p->trapframe->s11 = p->alarm_trapframe.s11;
  p->trapframe->t3 = p->alarm_trapframe.t3;
  p->trapframe->t4 = p->alarm_trapframe.t4;
  p->trapframe->t5 = p->alarm_trapframe.t5;
  p->trapframe->t6 = p->alarm_trapframe.t6;
  p->alarm_guard = 0;
  return 0;
}
```

这里本来可以直接使用`*(p->trapframe) = p->alarm_trapframe`的，但考虑到其他字段可能会有问题，比如
`kernel_hartid`之类的会不会发生改变，所以稳妥起见，就使用了大量的赋值语句。

这样就差不多了，跑一下测试试试：

```shell
$ ./grade-lab-traps alarm
make: 'kernel/kernel' is up to date.
== Test running alarmtest == (4.1s)
== Test   alarmtest: test0 ==
  alarmtest: test0: OK
== Test   alarmtest: test1 ==
  alarmtest: test1: OK
== Test   alarmtest: test2 ==
  alarmtest: test2: OK
```

成功通过。最后跑一下总的测试：

```shell
./grade-lab-traps
make: 'kernel/kernel' is up to date.
== Test answers-traps.txt == answers-traps.txt: OK
== Test backtrace test == backtrace test: OK (1.3s)
== Test running alarmtest == (3.5s)
== Test   alarmtest: test0 ==
  alarmtest: test0: OK
== Test   alarmtest: test1 ==
  alarmtest: test1: OK
== Test   alarmtest: test2 ==
  alarmtest: test2: OK
== Test usertests == usertests: OK (123.2s)
== Test time ==
time: OK
Score: 85/85
```

成功通过。
