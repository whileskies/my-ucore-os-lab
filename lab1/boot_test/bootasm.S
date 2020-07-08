# 本程序用于测试bootloader功能，将cpu从实模式切换为保护模式，并在屏幕中输出hello world

#include "asm.h"

.set PROT_MODE_CSEG, 0x8        # 代码段选择子  0000 1 000
.set PROT_MODE_DSEG, 0x10       # 数据段选择子  0001 0 000 
.set CR0_PE_ON, 0x1             # 保护模式打开标志

.global start
start:
.code16     # 16位实模式
    cli     # 关闭中断
    cld     # 字符串操作时方向递增

    xorw %ax, %ax       # 通过异或操作，设置ax寄存器值为0
    # 设置ds, es, ss寄存器为0
    movw %ax, %ds      
    movw %ax, %es
    movw %ax, %ss 

    # 打开A20地址线
    inb $0x92, %al      # 通过0x92端口打开A20
    orb $0x2, %al
    outb %al, $0x92

    # 设置GDT表
    lgdt gdtdesc        # 将gdt信息写入GDTR（包括基址与界限）

    # 切换为保护模式
    movl %cr0, %eax
    orl $CR0_PE_ON, %eax
    movl %eax, %cr0 

    ljmp $PROT_MODE_CSEG, $protcseg 


.code32
protcseg:
    # 设置段寄存器选择子为GDT表中的数据段
    movw $PROT_MODE_DSEG, %ax
    movw %ax, %ds 
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs 
    movw %ax, %ss

    # 设置堆栈，从0开始到0x7c00（bootloader起始地址）
    movl $0x0, %ebp
    movl $start, %esp


print:
    # 向显卡输出字符
    movl $0xb8000, %eax
    movl $0x5a0, %ebx
    movb $'H', 0x0(%ebx, %eax)
    movb $'e', 0x2(%ebx, %eax)
    movb $'l', 0x4(%ebx, %eax)
    movb $'l', 0x6(%ebx, %eax)
    movb $'o', 0x8(%ebx, %eax)
    movb $',', 0xa(%ebx, %eax)
    movb $'W', 0xc(%ebx, %eax)
    movb $'o', 0xe(%ebx, %eax)
    movb $'r', 0x10(%ebx, %eax)
    movb $'l', 0x12(%ebx, %eax)
    movb $'d', 0x14(%ebx, %eax)
    movb $'!', 0x16(%ebx, %eax)

spin:
    jmp spin




# bootloader的GDT表
.p2align 2      # 强制4字节对齐
gdt:
    SEG_NULLASM     # 第一个段为空段
    SEG_ASM(STA_X | STA_R, 0x0, 0xffffffff)     # 代码段
    SEG_ASM(STA_W, 0x0, 0xffffffff)     # 数据段


gdtdesc:
    .word 0x17      # GDT边界，三个段，共3 * 8 = 24 B，值为24 - 1 = 23 (0x17)
    .long gdt       # GTD基址，长度32
