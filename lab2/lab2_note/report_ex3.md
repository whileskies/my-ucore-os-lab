## 练习3：释放某虚地址所在的页并取消对应二级页表项的映射（需要编程）

当释放一个包含某虚地址的物理内存页时，需要让对应此物理内存页的管理数据结构Page做相关的清除处理，使得此物理内存页成为空闲；另外还需把表示虚地址与物理地址对应关系的二级页表项清除。请仔细查看和理解page_remove_pte函数中的注释。为此，需要补全在 kern/mm/pmm.c中的page_remove_pte函数。page_remove_pte函数的调用关系图如下所示：

![image002](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200912163647.png)

请在实验报告中简要说明你的设计实现过程。请回答如下问题：

- 数据结构Page的全局变量（其实是一个数组）的每一项与页表中的页目录项和页表项有无对应关系？如果有，其对应关系是啥？
- 如果希望虚拟地址与物理地址相等，则需要如何修改lab2，完成此事？ **鼓励通过编程来具体完成这个问题**

### page_remove_pte()

当释放某虚拟地址对应的物理页时，需要如下步骤：

- 修改物理页对应的Page结构的引用值
- 若无虚拟地址引用，则回收该物理页
- 取消对应页表项对该物理页的映射
- 清除该虚拟地址在TLB中的缓存

实现代码如下所示：

```c
static inline void
page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep) {
    pde_t *pdep = &pgdir[PDX(la)];
    if (!(*pdep & PTE_P) || !ptep) return;

    pte_t *la_ptep = &((pte_t *)KADDR(PTE_ADDR(*pdep)))[PTX(la)];
    if (la_ptep == ptep) {
        struct Page *page = pte2page(*ptep);

        if (page_ref_dec(page) == 0) {
            free_page(page);
        }
            
        *ptep = 0;
        tlb_invalidate(pgdir, la);
    }
}
```

### 问题1

Page数组存储了每一物理页的属性信息，数组索引对应了物理页的序号，通过索引可直接得到Page项所管理的物理页基址，如page2pa()函数就是将Page结构转为物理地址。

页目录表项和页表项中的地址均为物理地址，页表与物理页均占一页，可对应一Page。

### 问题2

- 将链接文件内核起始地址设为0x100000

```
SECTIONS {
    /* Load the kernel at this address: "." means the current address */
    . = 0x00100000;

    .text : {
        *(.text .stub .text.* .gnu.linkonce.t.*)
    }
```

- 将KERNBASE改为0

```
#define KERNBASE            0x00000000
```

- 注释取消0~4M的映射

```
next:
    # unmap va 0 ~ 4M, it's temporary mapping
    xorl %eax, %eax
    # movl %eax, __boot_pgdir
```

### 实验结果

![image-20200912174153130](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200912174153.png)

### 参考

- [为何内核虚拟地址位于高地址处](https://wiki.osdev.org/Higher_Half_Kernel)