#### 练习1：理解通过make生成执行文件的过程。（要求在报告中写出对下述问题的回答）

列出本实验各练习中对应的OS原理的知识点，并说明本实验中的实现部分如何对应和体现了原理中的基本概念和关键知识点。

在此练习中，大家需要通过静态分析代码来了解：

1. 操作系统镜像文件ucore.img是如何一步一步生成的？(需要比较详细地解释Makefile中每一条相关命令和命令参数的含义，以及说明命令导致的结果)
2. 一个被系统认为是符合规范的硬盘主引导扇区的特征是什么？



##### 1. 操作系统镜像文件ucore.img是如何一步一步生成的？

使用`make V= >> lab1_note/make_result.txt`命令对内核进行编译，并将编译过程重定向到make_result.txt文件

###### （1） 编译内核相关文件

make首先对各目录的C文件进行预处理、编译、汇编生成目标文件，对.S汇编文件直接生成目标文件，处理的文件按照先后顺序如下：

- kern/init目录：init.c
- kern/libs目录：stdio.c、readline.c
- kern/debug目录：panic.c、kdebug.c、kmonitor.c
- kern/driver目录：clock.c、console.c、picirq.c、intr.c
- kern/trap目录：trap.c、vectors.S、trapentry.S
- kern/mm目录：pmm.c
- libs目录：、string.c、printfmt.c

编译上述文件使用命令如下所示：

```shell
+ cc kern/init/init.c
gcc -Ikern/init/ -march=i686 -fno-builtin -fno-PIC -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -Ilibs/ -Ikern/debug/ -Ikern/driver/ -Ikern/trap/ -Ikern/mm/ -c kern/init/init.c -o obj/kern/init/init.o

+ cc kern/libs/stdio.c
gcc -Ikern/libs/ -march=i686 -fno-builtin -fno-PIC -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -Ilibs/ -Ikern/debug/ -Ikern/driver/ -Ikern/trap/ -Ikern/mm/ -c kern/libs/stdio.c -o obj/kern/libs/stdio.o
```

其中有一些关键参数：

- -nostdinc：生成OS时不应包含C语言标准库函数，需要自己实现，因此对于C文件里包含的头文件应给出头文件所在目录，如-Ikern/init/、-Ikern/driver/，已-I开头+文件夹表示寻找头文件的路径
- -march=i686：编译生成的目标文件指令集架构为i686，即Intel 32位指令集
- -m32：生成32位目标文件
- -fno-builtin：只识别以`__builtin_`作为前缀的内置函数，并可对其进行优化，也即防止内核代码函数名与内置函数名冲突而被优化的问题
- -fno-PIC：PIC（position independent code），使用绝对位置，而不是相对位置
- -Wall：显示所有编译警告
- -ggdb：尽可能的生成 gdb 的可以使用的调试信息
- -gstabs：以 stabs 格式声称调试信息，但是不包括 gdb 调试信息
- -fno-stack-protector：禁用堆栈保护器

###### （2） 链接内核文件，生成内核可执行程序

使用ld命令链接上面生成的各目标文件，并根据`tools/kernel.ld`脚本文件进行链接，链接后生成bin/kernelOS内核文件

```shell
+ ld bin/kernel
ld -m    elf_i386 -nostdlib -T tools/kernel.ld -o bin/kernel  obj/kern/init/init.o obj/kern/libs/stdio.o obj/kern/libs/readline.o obj/kern/debug/panic.o obj/kern/debug/kdebug.o obj/kern/debug/kmonitor.o obj/kern/driver/clock.o obj/kern/driver/console.o obj/kern/driver/picirq.o obj/kern/driver/intr.o obj/kern/trap/trap.o obj/kern/trap/vectors.o obj/kern/trap/trapentry.o obj/kern/mm/pmm.o  obj/libs/string.o obj/libs/printfmt.o
```

###### （3）编译、链接bootloader

首先使用gcc将bootasm.S、bootmain.c生成目标文件，再使用ld命令将两个目标文件链接，设置Entry入口为start段，代码段起始位置为0x7c00，使用sign程序将bootblock.o文件添加主引导扇区的标志，使其作为bootloader

```shell
+ cc boot/bootasm.S
gcc -Iboot/ -march=i686 -fno-builtin -fno-PIC -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -Ilibs/ -Os -nostdinc -c boot/bootasm.S -o obj/boot/bootasm.o

+ cc boot/bootmain.c
gcc -Iboot/ -march=i686 -fno-builtin -fno-PIC -Wall -ggdb -m32 -gstabs -nostdinc  -fno-stack-protector -Ilibs/ -Os -nostdinc -c boot/bootmain.c -o obj/boot/bootmain.o

+ cc tools/sign.c
gcc -Itools/ -g -Wall -O2 -c tools/sign.c -o obj/sign/tools/sign.o
gcc -g -Wall -O2 obj/sign/tools/sign.o -o bin/sign

+ ld bin/bootblock
ld -m    elf_i386 -nostdlib -N -e start -Ttext 0x7C00 obj/boot/bootasm.o obj/boot/bootmain.o -o obj/bootblock.o
'obj/bootblock.out' size: 496 bytes
build 512 bytes boot sector: 'bin/bootblock' success!
```

###### （4）生成OS镜像文件

dd命令用于转换和复制文件，这里使用dd来生成最终的ucore镜像文件

1. 使用/dev/zero虚拟设备，生成10000个块的空字符(0x00)，每个块大小为512B，因此ucore.img总大小为5,120,000B
2. 将bootloader（bin/bootblock文件）代码复制到ucore.img文件头处，共512B大小
3. 将kernel（bin/kernel文件）复制到ucore.img距文件头偏移1个块大小的地方，也即ucore.img前512B放bootloader，紧接着放kernel

```shell
dd if=/dev/zero of=bin/ucore.img count=10000
dd if=bin/bootblock of=bin/ucore.img conv=notrunc
dd if=bin/kernel of=bin/ucore.img seek=1 conv=notrunc
```



##### 2. 一个被系统认为是符合规范的硬盘主引导扇区的特征是什么？

生成符合规范的硬盘主引导扇区的程序源文件为tools/sign.c，核心代码如下：

```c
char buf[512];
memset(buf, 0, sizeof(buf));
FILE *ifp = fopen(argv[1], "rb");
int size = fread(buf, 1, st.st_size, ifp);
if (size != st.st_size) {
    fprintf(stderr, "read '%s' error, size is %d.\n", argv[1], size);
    return -1;
}
fclose(ifp);
buf[510] = 0x55;
buf[511] = 0xAA;
FILE *ofp = fopen(argv[2], "wb+");
size = fwrite(buf, 1, 512, ofp);
if (size != 512) {
    fprintf(stderr, "write '%s' error, size is %d.\n", argv[2], size);
    return -1;
}
fclose(ofp);
printf("build 512 bytes boot sector: '%s' success!\n", argv[2])
```

该程序主要功能是将输入文件（需小于等于510B大小），第511个字节设为0x55，第512个字节设为0xAA

因此MBR的特征为：共512字节，最后两个字节分别为0x55、0xAA