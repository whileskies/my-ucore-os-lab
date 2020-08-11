## 练习1：实现 first-fit 连续物理内存分配算法（需要编程）

在实现first fit 内存分配算法的回收函数时，要考虑地址连续的空闲块之间的合并操作。提示:在建立空闲页块链表时，需要按照空闲页块起始地址来排序，形成一个有序的链表。可能会修改default_pmm.c中的default_init，default_init_memmap，default_alloc_pages， default_free_pages等相关函数。请仔细查看和理解default_pmm.c中的注释。

请在实验报告中简要说明你的设计实现过程。请回答如下问题：

- 你的first fit算法是否有进一步的改进空间

### 分页内存管理

ucore使用物理内存分页管理机制，将可用的物理内存按照固定的大小单位—4K，分为若干物理页进行管理。同样的，程序虚拟地址空间也按照4K大小划分为若干虚拟页，若虚拟页需要加载到内存中去运行使用时，os将虚拟页映射到物理页。os通过维护页表结构来实现虚拟页到物理页的映射，cpu mmu的分页管理机构在执行时，通过查找页表，将程序的线性地址转换为物理地址。

ucore要对物理页进行管理，了解物理页的状态与使用情况等信息。因此ucore为每一个物理页创建一个Page结构体变量，这些Page数据保存在ucore BSS段之后。ucore内存布局结构如下图所示：

![img](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200810145053.png)

Page结构如下所示，每个Page结构变量描述一个物理页：

- ref表示物理页被页表的引用计数
- flags代表该物理页的标识，如第0位(PG_reserved)表示是否被内核使用的物理页，不进行alloc/free操作、第1位(PG_property)表示在first fit连续页分配管理中是否为空闲块(若干个待分配的连续的页)的首页
- property代表在first fit连续页分配管理中一个空闲块中页的个数(只在空闲块首页中设置)
- page_link表示双向链表的链接结点，该Page可与其他Page链接起来，在first fit空闲块管理时使用

```c
struct Page {
    int ref;                        // page frame's reference counter
    uint32_t flags;                 // array of flags that describe the status of the page frame
    unsigned int property;          // the num of free block, used in first fit pm manager
    list_entry_t page_link;         // free list link
};

/* Flags describing the status of a page frame */
#define PG_reserved                 0       // if this bit=1: the Page is reserved for kernel, cannot be used in alloc/free_pages; otherwise, this bit=0 
#define PG_property                 1       // if this bit=1: the Page is the head page of a free memory block(contains some continuous_addrress pages), and can be used in alloc_pages; if this bit=0: if the Page is the the head page of a free memory block, then this Page and the memory block is alloced. Or this Page isn't the head page.
```

### first fit 连续物理内存分配算法

针对可以向内核申请若干个连续的物理页、释放若干个连续的物理页的需求，可以使用first fit、best fit、worst-fit、buddy、slab等连续物理内存分配算法解决，只不过在ucore中物理内存是按照页为单位进行管理的，因此实际实现为连续物理页的分配。

first fit算法维护一个空闲块链表，且这些空闲块按照起始地址由小到大进行排列，每次进行分配时，依次遍历空闲块链表，找到第一个符合大小要求的空闲块进行分配，分配剩余的部分仍加入到空闲块链表中。

在实际实现时，一个空闲块代表多个待分配的连续的页，仅将其中的首页加入到空闲块链表中，首页设置PG_property=1，property为该空闲块连续空闲页的个数。

### first fit 内存管理初始化

初始化将base起始的n页作为可分配的空闲页，并将首页加入到空闲块链表中。

```c
static void default_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    list_add(&free_list, &(base->page_link));
}
```

### first fit 内存管理—alloc

alloc函数首先遍历空闲块链表，当找到第一个空闲块大小不小于请求页个数时停止，分配此块。若该块大小比请求页个数大时，将此块的前n页作为分配的页，其余的页作为新的空闲块插入到链表中。

```c
static struct Page* default_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }
    if (page != NULL) {
        if (page->property > n) {
            struct Page *p = page + n;
            p->property = page->property - n;
            SetPageProperty(p);

            list_add(&(page->page_link), &(p->page_link));
        }

        ClearPageProperty(page);
        list_del(&(page->page_link));
        nr_free -= n;
    }

    return page;
}
```

### first fit 内存管理—free方法一

free时将base作为新空闲块的首页，插入到空闲块链表中，但插入的过程中可能会发生连续的空闲块之间的合并。

因此先依次遍历每一个空闲块，如果可以发生合并将该空闲块并入到新空闲块中，并删除该空闲块。当遍历完最后一个后，新空闲块已于链表中可以合并的空闲块合并完成。

最后，将新空闲块按照起始地址插入到空闲块链表中。代码中找到第一个比新空闲块起始地址大的空闲块结点，插入到该结点前面即可。但存在两个特殊情况：

- 链表为空，无空闲块
- 新空闲块起始地址比链表中所有空闲块起始地址都大

最后一行的代码`list_add_before(&free_list, &(base->page_link));`可兼顾上面两种情况。双向链表中，头结点之前的元素也是正向的最后一个元素。

```c
static void default_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    list_entry_t *le = list_next(&free_list);
    while (le != &free_list) {
        p = le2page(le, page_link);
        le = list_next(le);
        if (base + base->property == p) {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
        }
        else if (p + p->property == base) {
            p->property += base->property;
            ClearPageProperty(base);
            base = p;
            list_del(&(p->page_link));
        }
    }
    nr_free += n;

    le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *next = le2page(le, page_link);
        if (base < next) {
            list_add_before(&(next->page_link), &(base->page_link));
            return;
        }
    }
    list_add_before(&free_list, &(base->page_link));
}
```

### first fit 内存管理—free方法二

上面方法一的free过程需要先合并，再插入，最坏情况下，遍历了整个链表两次，当空闲块比较多时，效率不是最优。方法二则是在遍历链表时找到合适位置后直接进行合并与插入，最坏情况下，只需遍历整个链表一次。

基本思路是使用一个指针cur，指向当前处理的链表结点，pre指针为cur上一结点，执行过程中cur指针依次从链表的第一个结点移动到链表头结点(free_list)，直到使得新空闲块起始地址位于pre地址与cur地址之间，此时进行合并与插入，共有四种情况：

- 将上一个块(pre)与新空闲块(base)、当前块(cur)合并，首、尾块除外(首块为cur指向链表第一个结点的块，尾块为cur指向free_list结点的块)，如下图所示，新空闲块起始地址位于空闲块2与空闲块3之间，新空闲块与空闲块2、3合并
- 与上一块的尾部合并，首块除外
- 与当前块的首部合并，尾块除外
- 独立插入到上一块与当前块之间

![image-20200810165358979](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200810165359.png)

合并与插入完成后函数即可结束，只需遍历链表一次，代码如下所示：

```c
static void default_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);

    nr_free += n;

    //当前空闲块链表为空
    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
        return;
    }

    list_entry_t *le = list_next(&free_list);
    while (1) {
        list_entry_t *cur_le = le;
        list_entry_t *pre_le = le->prev;

        struct Page *cur_page = NULL, *pre_page = NULL;
        if (cur_le != &free_list) {
            cur_page = le2page(cur_le, page_link);
        }
        if (pre_le != &free_list) {
            pre_page = le2page(pre_le, page_link);
        }

        //cur_page指向当前处理的空闲块，pre_page为上一空闲块
        //pre_page == NULL时为处理的首块，cur_page == NULL时为处理的尾块(实际为free_list，并不是空闲块，只为统一处理)
        //处理过程是从首块到尾块(包括)，找到base应合并的地址大小区间后，考虑当前处理的块与上一块之间的合并问题
        if ( (!pre_page && cur_page && base < cur_page) || 
            (pre_page && cur_page && base > pre_page && base < cur_page) ||
            (pre_page && !cur_page && base > pre_page) ) {
            //情况1：将上一个块(pre_page)与释放块(base)、当前块(cur_page)合并，首、尾块除外
            if (pre_page && cur_page && 
                pre_page + pre_page->property == base &&
                base + base->property == cur_page) {
                ClearPageProperty(base);
                ClearPageProperty(cur_page);
                pre_page->property += (base->property + cur_page->property);
                list_del(cur_le);
            } else if (pre_page && pre_page + pre_page->property == base) {
                //情况2：与上一块的尾部合并，首块除外
                ClearPageProperty(base);
                pre_page->property += base->property;
            } else if (cur_page && base + base->property == cur_page) {
                //情况3：与当前块的首部合并，尾块除外
                ClearPageProperty(cur_page);
                base->property += cur_page->property;
                list_add_before(cur_le, &(base->page_link));
                list_del(cur_le);
            } else {
                //情况4：独立插入到上一块与当前块之间
                list_add_before(cur_le, &(base->page_link));
            }

            return;
        }

        le = list_next(le);
    }
}
```

### 调试函数

在此算法编写过程中，比较容易出问题，gdb调试又不是很方便，因此写了一个输出空闲块信息的函数辅助调试，还是比较容易定位到bug的。

```c
static void
print_frea_area_info() {
    cprintf("-----free area info begin-----\n");
    cprintf("nr_free: %d\n", nr_free);
    cprintf("%10s%10s%10s%5s%15s%15s\n", "begin_ppn","end_ppn", "page_cnt", "ref", "PG_reserved", "PG_property");
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        cprintf("%10d%10d%10d%5d%15d%15d\n",
            page2ppn(p), page2ppn(p) + p->property - 1, p->property, 
            p->ref, PageReserved(p), PageProperty(p));
    }
    cprintf("-----free area info end-------\n");
}
```

### 运行结果

![image-20200810170539747](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20200810170539.png)

### 参考资料
- [ucore文档](https://chyyuu.gitbooks.io/ucore_os_docs/content/lab2.html)
