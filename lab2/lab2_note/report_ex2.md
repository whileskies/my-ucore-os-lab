## 练习2：实现寻找虚拟地址对应的页表项（需要编程）

通过设置页表和对应的页表项，可建立虚拟内存地址和物理内存地址的对应关系。其中的get_pte函数是设置页表项环节中的一个重要步骤。此函数找到一个虚地址对应的二级页表项的内核虚地址，如果此二级页表项不存在，则分配一个包含此项的二级页表。本练习需要补全get_pte函数 in kern/mm/pmm.c，实现其功能。请仔细查看和理解get_pte函数中的注释。get_pte函数的调用关系图如下所示：

![img](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200911094106.png)

请在实验报告中简要说明你的设计实现过程。请回答如下问题：

- 请描述页目录项（Page Directory Entry）和页表项（Page Table Entry）中每个组成部分的含义以及对ucore而言的潜在用处。
- 如果ucore执行过程中访问内存，出现了页访问异常，请问硬件要做哪些事情？

### PDE与PTE

ucore采用二级分页机制，进行逻辑地址（逻辑地址=线性地址）到物理地址的映射，os内核只需通过页目录表和页表存储建立该映射后，当cpu访问逻辑地址时，会通过mmu硬件自动转换到物理地址。

mmu转换过程如下，先分段将逻辑地址转为线性地址，再分页，将线性地址转为物理地址：

<img src="https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200911094846.png" alt="img" style="zoom: 67%;" />

PDE与PTE结构如下所示：

<img src="https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200911095112.png" alt="img" style="zoom:67%;" />

PDE组成部分与潜在用途：

| 标识                    | 含义及潜在用途             |
| ----------------------- | -------------------------- |
| Page-Table Base Address | 页表基址                   |
| Avail                   | 保留OS使用                 |
| Global                  | 全局页                     |
| Page Size               | 页大小，0为4K              |
| 0                       | PTE_MBZ                    |
| Accessed                | 确定页表是否被访问过       |
| Cache Disabled          | 页表是否被缓存             |
| Write Through           | 缓存策略                   |
| User/Supervisor         | 相关的页是否可在用户态访问 |
| Read/Write              | 相关的页是否可写           |
| Present                 | 对应的页表是否存在         |

PTE组成部分与潜在用途：

| 标识                  | 含义及潜在用途                           |
| --------------------- | ---------------------------------------- |
| Physical Page Address | 物理页基址                               |
| Avail                 | 保留OS使用                               |
| Global                |                                          |
| 0                     | PTE_MBZ                                  |
| Dirty                 | 是否物理页被写过，虚拟内存时可使用       |
| Accessed              | 确定物理页是否被访问过，虚拟内存时可使用 |
| Cache Disabled        | 页是否被缓存                             |
| Write Through         | 缓存策略                                 |
| User/Supervisor       | 物理页是否可在用户态访问                 |
| Read/Write            | 物理页是否可写                           |
| Present               | 对应的页表是否存在                       |

### get_pte()

该函数用于给出一虚拟地址，返回该虚拟地址对应的页表项的虚拟地址，若不存在且create置位，创建该页表并返回，否则返回NULL。

如果相应的页表不存在，则调用alloc_page函数分配一空闲页，将该页作为页表，并将相应的页目录表项指向该页表，实现代码如下：

```c
pte_t *
get_pte(pde_t *pgdir, uintptr_t la, bool create) {
    pde_t *pdep = &pgdir[PDX(la)];
    
    if (!(*pdep & PTE_P)) {
        if (create) {
            struct Page *page = alloc_page();
            set_page_ref(page, 1);
            
            uintptr_t pa = page2pa(page);
            memset(page2kva(page), 0, PGSIZE);

            *pdep = pa | PTE_P | PTE_W | PTE_U;
        } else {
            return NULL;
        }
    } 

    return &((pte_t *)KADDR(PTE_ADDR(*pdep)))[PTX(la)];
}
```

### 页访问异常

若ucore在执行过程中出现了页访问异常，硬件需要做如下事情：

- 将引发页访问异常的线性地址保存在cr2寄存器中
- 设置错误码
- 产生Page Fault中断，页错误中断处理程序将数据从外存加载到内存
- 退出中断，重新访问该页

### 参考资料

- Intel® 64 and IA-32 Architectures Software Developer’s Manual  3A
- [ucore lab2 操作系统实验](https://blog.csdn.net/dingdingdodo/article/details/100622753)