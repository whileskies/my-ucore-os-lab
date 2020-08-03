## 练习6：完善中断初始化和处理 （需要编程）

请完成编码工作和回答如下问题：

1. 中断描述符表（也可简称为保护模式下的中断向量表）中一个表项占多少字节？其中哪几位代表中断处理代码的入口？
2. 请编程完善kern/trap/trap.c中对中断向量表进行初始化的函数idt_init。在idt_init函数中，依次对所有中断入口进行初始化。使用mmu.h中的SETGATE宏，填充idt数组内容。每个中断的入口由tools/vectors.c生成，使用trap.c中声明的vectors数组即可。
3. 请编程完善trap.c中的中断处理函数trap，在对时钟中断进行处理的部分填写trap函数中处理时钟中断的部分，使操作系统每遇到100次时钟中断后，调用print_ticks子程序，向屏幕上打印一行文字”100 ticks”。

> 【注意】除了系统调用中断(T_SYSCALL)使用陷阱门描述符且权限为用户态权限以外，其它中断均使用特权级(DPL)为０的中断门描述符，权限为内核态权限；而ucore的应用程序处于特权级３，需要采用｀int 0x80`指令操作（这种方式称为软中断，软件中断，Tra中断，在lab5会碰到）来发出系统调用请求，并要能实现从特权级３到特权级０的转换，所以系统调用中断(T_SYSCALL)所对应的中断门描述符中的特权级（DPL）需要设置为３。

要求完成问题2和问题3 提出的相关函数实现，提交改进后的源代码包（可以编译执行），并在实验报告中简要说明实现过程，并写出对问题1的回答。完成这问题2和3要求的部分代码后，运行整个系统，可以看到大约每1秒会输出一次”100 ticks”，而按下的键也会在屏幕上显示。

提示：可阅读小节“中断与异常”。

### 中断描述符表

操作系统是由中断驱动的，用于当某事件发生时，可以主动通知cpu及os进行处理，主要的中断类型有外部中断、内部中断（异常）、软中断（陷阱、系统调用）。

- 外部中断：用于cpu与外设进行通信，当外设需要输入或输出时主动向cpu发出中断请求；
- 内部中断：cpu执行期间检测到不正常或非法条件（如除零错、地址访问越界）时会引起内部中断；
- 系统调用：用于程序使用系统调用服务。

当中断发生时，cpu会得到一个中断向量号，作为IDT（中断描述符表）的索引，IDT表起始地址由IDTR寄存器存储，cpu会从IDT表中找到该中断向量号相应的中断服务程序入口地址，跳转到中断处理程序处执行，并保存当前现场；当中断程序执行完毕，恢复现场，跳转到原中断点处继续执行。

IDT的表项为中断描述符，主要类型有中断门、陷阱门、调用门，其中中断门与陷阱门格式如下所示：

![image-20200730160344378](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200730160344.png)

![image-20200730160406067](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200730160406.png)

中断门与陷阱门作为IDT的表项，每个表项占据8字节，其中段选择子和偏移地址用来代表中断处理程序入口地址，具体先通过选择子查找GDT对应段描述符，得到该代码段的基址，基址加上偏移地址为中断处理程序入口地址。

### 初始化IDT

vectors.S文件为各中断处理程序的入口，示例如下：

```asm
.text
.globl __alltraps
.globl vector0
vector0:
  pushl $0
  pushl $0
  jmp __alltraps
.globl vector1
vector1:
  pushl $0
  pushl $1
  jmp __alltraps
// 省略
# vector table
.data
.globl __vectors
__vectors:
  .long vector0
  .long vector1
  .long vector2
```

\_\_vectors在数据段，是存储了各中断处理程序入口地址的数组，每一个中断处理程序依次将错误码、中断向量号压栈（一些由cpu自动压入错误码的只压入中断向量号），再调用trapentry.S中的 \_\_alltraps过程进行处理。

根据中断门、陷阱门描述符格式使用SETGATE宏函数对IDT进行初始化，在这里先全部设为中断门，中断处理程序均在内核态执行，因此代码段为内核的代码段，DPL为内核态的0。

```c
/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S */
void idt_init(void) {
    extern uintptr_t __vectors[];

    for (int i = 0; i < 256; i++) {
        SETGATE(idt[i], 0, GD_KTEXT, __vectors[i], DPL_KERNEL);
    }

    lidt(&idt_pd);
}
```

在进行中断处理时，会保存现场，将返回地址、中断号、相关寄存器等数据保存到trapframe结构中：

```c
/* registers as pushed by pushal */
struct pushregs {
    uint32_t reg_edi;
    uint32_t reg_esi;
    uint32_t reg_ebp;
    uint32_t reg_oesp;            /* Useless */
    uint32_t reg_ebx;
    uint32_t reg_edx;
    uint32_t reg_ecx;
    uint32_t reg_eax;
};

struct trapframe {
    struct pushregs tf_regs;
    uint16_t tf_gs;
    uint16_t tf_padding0;
    uint16_t tf_fs;
    uint16_t tf_padding1;
    uint16_t tf_es;
    uint16_t tf_padding2;
    uint16_t tf_ds;
    uint16_t tf_padding3;
    uint32_t tf_trapno;
    /* below here defined by x86 hardware */
    uint32_t tf_err;
    uintptr_t tf_eip;
    uint16_t tf_cs;
    uint16_t tf_padding4;
    uint32_t tf_eflags;
    /* below here only when crossing rings, such as from user to kernel */
    uintptr_t tf_esp;
    uint16_t tf_ss;
    uint16_t tf_padding5;
} __attribute__((packed));
```

\_\_alltraps为各中断处理程序的前置代码，用于继续在栈中完成trapframe结构，依次压入ds、es、fs、gs、通用寄存器，并将数据段切换为内核数据段（代码段在IDT初始化过程中设置为内核代码段），最后压入trapframe结构体指针作为trap函数的参数，再调用trap函数完成具体的中断处理，代码如下：

```assembly
__alltraps:
    # push registers to build a trap frame
    # therefore make the stack look like a struct trapframe
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    pushal

    # load GD_KDATA into %ds and %es to set up data segments for kernel
    movl $GD_KDATA, %eax
    movw %ax, %ds
    movw %ax, %es

    # push %esp to pass a pointer to the trapframe as an argument to trap()
    pushl %esp

    # call trap(tf), where tf=%esp
    call trap
```

### 处理时钟中断

trap_dispatch函数根据trapframe获取中断号去处理相应中断，处理时钟中断的代码如下：

```c
void trap(struct trapframe *tf) {
    // dispatch based on what type of trap occurred
    trap_dispatch(tf);
}

/* trap_dispatch - dispatch based on what type of trap occurred */
static void trap_dispatch(struct trapframe *tf) {
    char c;

    switch (tf->tf_trapno) {
    case IRQ_OFFSET + IRQ_TIMER:
        ticks++;
        
        if (ticks % TICK_NUM == 0) {
            print_ticks();
        }

        break;
    }
}
```

### 中断返回

trap函数执行完中断处理程序后，恢复现场，重新弹出各寄存器值，iret指令弹出cs、eip、eflags，跳转到之前中断的地方继续执行。

注：此篇并未考虑特权级变化时的中断处理情况

### 执行结果

![image-20200730204740976](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200730204741.png)

### 参考

- 操作系统真相还原
- [ucore文档-中断与异常](https://chyyuu.gitbooks.io/ucore_os_docs/content/lab1/lab1_3_3_2_interrupt_exception.html)
- [xv6-chinese文档-中断、陷入、驱动程序](https://th0ar.gitbooks.io/xv6-chinese/content/content/chapter3.html)
