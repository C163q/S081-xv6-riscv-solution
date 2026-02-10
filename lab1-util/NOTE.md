初来乍到，我现在对这个系统的内部细节一无所知，甚至不会RISC-V汇编，可以说是极大的劣势。

首先，实验要求我们实现一个叫作`sleep`的程序，这个程序能够暂停用户指定的tick数。`tick`是`xv6`内核
定义的时间概念，表示计时芯片两个中断之间的时间间隔。`sleep`的程序应该在`user/sleep.c`中。

我们先来看看其他程序是怎么写的，比如`rm`：

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    fprintf(2, "Usage: rm files...\n");
    exit(1);
  }

  for(i = 1; i < argc; i++){
    if(unlink(argv[i]) < 0){
      fprintf(2, "rm: %s failed to delete\n", argv[i]);
      break;
    }
  }

  exit(0);
}
```

它似乎调用了`user/user.h`当中声明的`unlink`函数。不过，`unlink`函数在哪里定义的？我找了一下，应该是
用`user/usys.pl`生成的，这个文件里的样板应该是生成汇编，让`${name}`去调用`SYS_${name}`的系统调用。
基本上是这样的内容：

```asm
.global fork
fork:
 li a7, SYS_fork
 ecall
 ret
```

而在`kernel/sysproc.c`中，已经有了`sys_sleep`的定义了，也就是说，我们只要放心使用`sleep`函数即可。

我们在提示中，可以看到，他说必须在`main`函数结束的时候调用`exit`来结束程序，看起来编译器不会主动帮助
我们往程序外面再套上一层`_start`，`main`是真正意义上的程序入口。

我们还可以看到，除了系统调用，`user/ulib.c`当中还定义了一些非常方便的函数比如`fprintf`、`atoi`等。
有了这些，`sleep`程序就很好写了：

```c
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int n;

  if(argc < 2){
    fprintf(2, "Usage: sleep ticks...\n");
    exit(1);
  }

  n = atoi(argv[1]);
  sleep(n);

  exit(0);
}
```

在`Makefile`中的`UPROGS`内加上`$U/_sleep\`就可以把程序加入系统了。

实际运行确实成功了，但是在sleep的期间，我的电脑风扇转的挺响，看起来程序是在空转，不知道为什么。
不过测试确实是通过了：

```
$ ./grade-lab-util sleep
make: 'kernel/kernel' is up to date.
== Test sleep, no arguments == sleep, no arguments: OK (0.8s)
== Test sleep, returns == sleep, returns: OK (0.9s)
== Test sleep, makes syscall == sleep, makes syscall: OK (1.0s)
```

接下来看一下`ping-pong`这个程序，首先需要两队管道，双向的，父进程用管道向子进程发送一字节，然后子进程打印
`"<pid>: receive ping"`，向父进程发送一字节并结束，父进程接收后打印`"<pid>: receive pong"`并结束。
题解写在`user/pingpong.c`中。

难度不大，照着说明一点一点写就可以了：

```c
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int ping[2], pong[2];
  pipe(ping);
  pipe(pong);
  if (fork() == 0) {  // child
    char buf[1];
    close(pong[0]);
    close(ping[1]);
    read(ping[0], buf, 1);
    fprintf(1, "%d: received ping\n", getpid());
    close(ping[0]);
    write(pong[1], "\0", 1);
    close(pong[1]);
  } else {  // parent
    char buf[1];
    close(ping[0]);
    close(pong[1]);
    write(ping[1], "\0", 1);
    close(ping[1]);
    read(pong[0], buf, 1);
    fprintf(1, "%d: received pong\n", getpid());
    close(pong[0]);
  }
  exit(0);
}
```

测试一下：

```c
$ ./grade-lab-util pingpong
make: 'kernel/kernel' is up to date.
== Test pingpong == pingpong: OK (1.4s)
```

*这些题目基本都是熟悉系统调用，因此就暂时写到这里，后面的题目后续抽时间补。*

