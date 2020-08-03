

## 扩展练习 Challenge 1（需要编程）

扩展proj4,增加syscall功能，即增加一用户态函数（可执行一特定系统调用：获得时钟计数值），当内核初始完毕后，可从内核态返回到用户态的函数，而用户态的函数又通过系统调用得到内核态的服务（通过网络查询所需信息，可找老师咨询。如果完成，且有兴趣做代替考试的实验，可找老师商量）。需写出详细的设计和分析报告。完成出色的可获得适当加分。

提示： 规范一下 challenge 的流程。

kern_init 调用 switch_test，该函数如下：

```c
static void
    switch_test(void) {
    print_cur_status();          // print 当前 cs/ss/ds 等寄存器状态
    cprintf("+++ switch to  user  mode +++\n");
    switch_to_user();            // switch to user mode
    print_cur_status();
    cprintf("+++ switch to kernel mode +++\n");
    switch_to_kernel();         // switch to kernel mode
    print_cur_status();
}
```

switch*to** 函数建议通过 中断处理的方式实现。主要要完成的代码是在 trap 里面处理 T_SWITCH_TO* 中断，并设置好返回的状态。

在 lab1 里面完成代码以后，执行 make grade 应该能够评测结果是否正确。

## 扩展练习 Challenge 2（需要编程）

用键盘实现用户模式内核模式切换。具体目标是：“键盘输入3时切换到用户模式，键盘输入0时切换到内核模式”。 基本思路是借鉴软中断(syscall功能)的代码，并且把trap.c中软中断处理的设置语句拿过来。

注意：

　1.关于调试工具，不建议用lab1_print_cur_status()来显示，要注意到寄存器的值要在中断完成后tranentry.S里面iret结束的时候才写回，所以再trap.c里面不好观察，建议用print_trapframe(tf)

　2.关于内联汇编，最开始调试的时候，参数容易出现错误，可能的错误代码如下

```c
asm volatile ( "sub $0x8, %%esp \n"
              "int %0 \n"
              "movl %%ebp, %%esp"
              : )
```

要去掉参数int %0 \n这一行

3.软中断是利用了临时栈来处理的，所以有压栈和出栈的汇编语句。硬件中断本身就在内核态了，直接处理就可以了。

### 特权级切换的中断过程

由练习6，所有的中断处理程序均在内核态执行。

- 若cpu在内核态执行时进行中断，特权级并无变化，直接在原栈（内核栈）进行中断处理，不涉及栈的变化，trapframe的ss、esp不会由cpu自动压入、也不会弹出，不会使用。

- 若在中断中涉及特权级的变换，中断的执行也会进行栈的切换。若cpu在用户态执行时产生了中断，由于中断处理程序是内核态，即CPL从3变为0，特权级进行了提升，需要从原来的用户栈切换到内核栈。内核栈的地址会被初始化在tss（任务状态段）的ss0、esp0中，当从用户态切换到内核态时，cpu从tss中得到ss0、esp0，切换到内核栈，并会在内核栈压入用户栈的ss、esp。
- 当处于内核态的中断处理程序执行完毕后，恢复现场，执行iret指令中断返回，从内核栈中弹出用户程序中断点的cs时，特权级会从高特权级变为低特权级，CPL从0变为3，也即从内核态切换回用户态，会弹出开始保存在内核栈的用户栈ss、esp，也即完成了从内核栈到用户栈的转换。

### 内核态切换为用户态

 内核态切换为用户态相当于内核态的中断处理程序返回为用户态的过程。

`sub $0x8, %%esp`为trapframe预留了用户栈ss、esp的空间，`int $T_SWITCH_TOU`进入相应中断完成内核态到用户态转换的过程，`movl %%ebp, %%esp`为转换到用户态后将`lab1_switch_to_user`函数处于刚进入函数执行的状态（esp = ebp）。

```c
static void lab1_switch_to_user(void) {
    asm volatile (
	    "sub $0x8, %%esp \n"
	    "int %0 \n"
	    "movl %%ebp, %%esp"
	    : 
	    : "i"(T_SWITCH_TOU)
	);
}
```

参考答案中是用了一个临时的trapframe结构，但由于上面已经预留了用户栈ss、esp的位置，可以直接修改trapframe结构。

主要的过程是将cs设为用户代码段，ds、es、ss数据段设为用户数据段，用户栈的esp仍设为中断时使用的栈，在中断返回时会自动弹出esp、ss用于恢复到用户栈。

```c
/* temporary trapframe or pointer to trapframe */
struct trapframe switchk2u, *switchu2k;

static inline __attribute__((always_inline)) void switch_to_user(struct trapframe *tf) {
    if (tf->tf_cs != USER_CS) {
        // tf->tf_cs = USER_CS;
        // tf->tf_ds = tf->tf_es = tf->tf_ss = USER_DS;
        // tf->tf_eflags |= FL_IOPL_MASK;
        // tf->tf_esp = (uint32_t)tf + sizeof(struct trapframe) - 8
        
        switchk2u = *tf;

        switchk2u.tf_cs = USER_CS;
        switchk2u.tf_ds = switchk2u.tf_es = switchk2u.tf_ss = USER_DS;
        switchk2u.tf_esp = (uint32_t)tf + sizeof(struct trapframe) - 8;
    
        // set eflags, make sure ucore can use io under user mode.
        // if CPL > IOPL, then cpu will generate a general protection.
        switchk2u.tf_eflags |= FL_IOPL_MASK;
    
        // set temporary stack
        // then iret will jump to the right stack
        *((uint32_t *)tf - 1) = (uint32_t)&switchk2u;
    }
}

//trap_dispatch函数中
case T_SWITCH_TOU:
    switch_to_user(tf);
    break;
```

### 用户态发生中断

指令在用户态执行时，也会进行中断处理，如时钟中断。但是中断处理程序都是在内核态，在执行过程中需要将用户栈转换为内核栈。具体做法是从TSS（任务状态段）中读取ss0、esp0作为内核栈地址，将ss设为ss0、esp设为esp0，同时自动压入用户栈的ss、esp，作为trapframe结构一部分，待中断返回时，再从内核栈转为用户栈。

TSS存储在GDT中，同时有一个TR的寄存器记录TSS段的起始地址。ucore中只使用一个全局的TSS，作用仅为进行中断时cpu找到内核栈的ss、esp，以便切换执行。

stack0即为临时的内核栈，当在用户态执行时进行中断，将切换到stack0堆栈进行执行。

```c
/* temporary kernel stack */
uint8_t stack0[1024];

/* gdt_init - initialize the default GDT and TSS */
static void
gdt_init(void) {
    // Setup a TSS so that we can get the right stack when we trap from
    // user to the kernel. But not safe here, it's only a temporary value,
    // it will be set to KSTACKTOP in lab2.
    ts.ts_esp0 = (uint32_t)&stack0 + sizeof(stack0);
    ts.ts_ss0 = KERNEL_DS;

    // initialize the TSS filed of the gdt
    gdt[SEG_TSS] = SEG16(STS_T32A, (uint32_t)&ts, sizeof(ts), DPL_KERNEL);
    gdt[SEG_TSS].sd_s = 0;

    // reload all segment registers
    lgdt(&gdt_pd);

    // load the TSS
    ltr(GD_TSS);
}
```

### 用户态切换为内核态

用户态切换为内核态相当于内核态的中断处理程序返回为内核态的过程。

当用户态进入内核态时，压入用户栈的ss、esp，在中断处理程序中切换cs为内核代码段、ds和es为内核数据段，为了使得中断返回后将原用户栈作为内核栈（0x7c00-0x0000），手动将trapframe移到用户栈的地方，因为内核态的中断处理程序返回到内核态不需要进行内核栈与用户栈的转换，因此新的trapframe不需要ss、esp。

```c
static inline __attribute__((always_inline)) void switch_to_kernel(struct trapframe *tf) {
    if (tf->tf_cs != KERNEL_CS) {
        tf->tf_cs = KERNEL_CS;
        tf->tf_ds = tf->tf_es = KERNEL_DS;
        tf->tf_eflags &= ~FL_IOPL_MASK;
        switchu2k = (struct trapframe *)(tf->tf_esp - (sizeof(struct trapframe) - 8));

        memmove(switchu2k, tf, sizeof(struct trapframe) - 8);
        *((uint32_t *)tf - 1) = (uint32_t)switchu2k;
    }
}

//trap_dispatch函数中
case T_SWITCH_TOK:
    switch_to_kernel(tf);
    break;
```

还有个细节，当用户态代码使用int指令调用中断处理程序时（如0x80系统调用），要进行权限检查，只有该中断描述符的DPL >= CPL时，才可顺利进行中断调用。因此在IDT初始化时（idt_init函数中），将T_SWITCH_TOK对应的中断描述符DPL设为用户态。

```c
SETGATE(idt[T_SWITCH_TOK], 0, GD_KTEXT, __vectors[T_SWITCH_TOK], DPL_USER)
```

### 键盘实现用户态、内核态切换

上述两种切换均是在内核态的中断处理程序中完成的，键盘的处理也是在中断处理程序中，因此可采用相同的方式进行切换，只需调用切换函数即可，代码如下：

```c
case IRQ_OFFSET + IRQ_KBD:
    c = cons_getc();
    cprintf("kbd [%03d] %c\n", c, c);

    if (c == '0') {
        //切换为内核态
        switch_to_kernel(tf);
    } else if (c == '3') {
        //切换为用户态
        switch_to_user(tf);
    }
    break;
```

### 运行结果

编程方式切换：

![image-20200803171800691](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200803171800.png)

键盘方式切换：

![image-20200803172441274](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200803172441.png)

### 参考

- 操作系统真相还原

- [ucore文档-中断与异常](https://chyyuu.gitbooks.io/ucore_os_docs/content/lab1/lab1_3_3_2_interrupt_exception.html)

- [xv6-chinese文档-中断、陷入、驱动程序](https://th0ar.gitbooks.io/xv6-chinese/content/content/chapter3.html)

- [CPU的运行环、特权级与保护](https://blog.csdn.net/drshenlei/article/details/4265101)

  