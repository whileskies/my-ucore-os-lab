## 扩展练习Challenge：buddy system（伙伴系统）分配算法（需要编程）

Buddy System算法把系统中的可用存储空间划分为存储块(Block)来进行管理, 每个存储块的大小必须是2的n次幂(Pow(2, n)), 即1, 2, 4, 8, 16, 32, 64, 128...

- 参考[伙伴分配器的一个极简实现](http://coolshell.cn/articles/10427.html)， 在ucore中实现buddy system分配算法，要求有比较充分的测试用例说明实现的正确性，需要有设计文档。

### buddy system

伙伴系统(buddy system)是一种内存分配算法，将物理内存按照2的幂进行划分为若干空闲块，每次分配选择合适且最小的内存块进行分配。其优点是快速搜索合并(O(logN)时间复杂度)、低外部碎片(最佳适配best-fit)，其缺点是内部碎片，因为空闲块一定是按照2的幂划分的。

在ucore中，分配的基本单位是页，因此每一空闲块都是2^n个连续的页。

### 数据结构

采用完全二叉树结构来管理连续内存页，如下图buddy system共管理16个连续内存页，每一结点记录与管理若干连续内存页，如结点0管理连续的16个页，结点1管理其下连续的8个页，结点15管理连续内存的第一个页，每个结点存储一个longest，记录该结点所管理的所有页中最大可连续分配页数目。

<img src="https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20201009172121.png" alt="image-20201009172121474" style="zoom:80%;" />

定义buddy的结构如下所示，size表示所管理的连续内存页大小(需要是2的幂)，longest为上面所说的数组，这里是直接以分配物理内存的形式存在(指针)，longest_num_page表示longest数组的大小，free_size表示当前该区域的空闲页大小，begin_page指针指向所管理的连续内存页的第一页的Page结构。

由于可能存在多个可管理的内存区域(pmm.c/page_init中)，因此定义了buddy数组，分别管理num_buddy_zone个内存区域。

```c
struct buddy {
    size_t size;
    uintptr_t *longest;
    size_t longest_num_page;
    size_t total_num_page;
    size_t free_size;
    struct Page *begin_page;
};

struct buddy mem_buddy[MAX_NUM_BUDDY_ZONE];
int num_buddy_zone = 0;
```

### 初始化

buddy_init_memmap用于buddy system的初始化，输入参数base为可管理区域起始页的Page结构，n为页个数。

初始化函数完成以下工作：

- 将base开始的若干个内存页作为longest数组的存储空间，之后开始的内存页作为可进行分配的物理页。

- 将管理的连续内存页大小设为2的n次幂，再从顶向下初始每一结点的longest。

```c
static void
buddy_init_memmap(struct Page *base, size_t n) {
    cprintf("n: %d\n", n);
    struct buddy *buddy = &mem_buddy[num_buddy_zone++];

    size_t v_size = next_power_of_2(n);
    size_t excess = v_size - n;
    size_t v_alloced_size = next_power_of_2(excess);

    buddy->size = v_size;
    buddy->free_size = v_size - v_alloced_size;
    buddy->longest = page2kva(base);
    buddy->begin_page = pa2page(PADDR(ROUNDUP(buddy->longest + 2 * v_size * sizeof(uintptr_t), PGSIZE)));
    buddy->longest_num_page = buddy->begin_page - base;
    buddy->total_num_page = n - buddy->longest_num_page;

    size_t node_size = buddy->size * 2;

    for (int i = 0; i < 2 * buddy->size - 1; i++) {
        if (IS_POWER_OF_2(i + 1)) {
            node_size /= 2;
        }
        buddy->longest[i] = node_size;
    }

    int index = 0;
    while (1) {
        if (buddy->longest[index] == v_alloced_size) {
            buddy->longest[index] = 0;
            break;
        }
        index = RIGHT_LEAF(index);
    }

    while (index) {
        index = PARENT(index);
        buddy->longest[index] = MAX(buddy->longest[LEFT_LEAF(index)], buddy->longest[RIGHT_LEAF(index)]);
    }

    struct Page *p = buddy->begin_page;
    for (; p != base + buddy->free_size; p ++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
}
```

在固定可分配内存大小时，必须是2的n次幂个页，如果向下取幂，则可能会有较大的内存浪费；这里采用向上取幂的方式，对于实际不存在的连续页(取2的n次幂)，将其对应longest进行标记，假设其已经分配过。这样可以充分使用物理页空间。

### alloc

分配函数从树根出发寻找合适页数目的空闲块，找到后将其longest设为0，表示此结点对应的连续物理页均被分配，再从该结点出发沿着到树根的路径更新其longest。

```c
static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (!IS_POWER_OF_2(n))
        n = next_power_of_2(n);

    size_t index = 0;
    size_t node_size;
    size_t offset = 0;

    struct buddy *buddy = NULL;
    for (int i = 0; i < num_buddy_zone; i++) {
        if (mem_buddy[i].longest[index] >= n) {
            buddy = &mem_buddy[i];
            break;
        }
    }

    if (!buddy) {
        return NULL;
    }

    for (node_size = buddy->size; node_size != n; node_size /= 2) {
        if (buddy->longest[LEFT_LEAF(index)] >= n)
            index = LEFT_LEAF(index);
        else
            index = RIGHT_LEAF(index);
    }

    buddy->longest[index] = 0;
    offset = (index + 1) * node_size - buddy->size;

    while (index) {
        index = PARENT(index);
        buddy->longest[index] = MAX(buddy->longest[LEFT_LEAF(index)], buddy->longest[RIGHT_LEAF(index)]);
    }

    buddy->free_size -= n;

    return buddy->begin_page + offset;
}
```

### free

free函数根据所要释放的内存页在管理的内存页中的偏移，从相应树叶开始从底向上寻找当初一次分配的连续物理页，进行释放，同时更新该结点到树根路径上的longest。

此函数的参数n并无实际作用，只为统一接口。

```c
static void
buddy_free_pages(struct Page *base, size_t n) {
    struct buddy *buddy = NULL;

    for (int i = 0; i < num_buddy_zone; i++) {
        struct buddy *t = &mem_buddy[i];
        if (base >= t->begin_page && base < t->begin_page + t->size) {
            buddy = t;
        }
    }

    if (!buddy) return;

    unsigned node_size, index = 0;
    unsigned left_longest, right_longest;
    unsigned offset = base - buddy->begin_page;

    assert(offset >= 0 && offset < buddy->size);

    node_size = 1;
    index = offset + buddy->size - 1;

    for (; buddy->longest[index]; index = PARENT(index)) {
        node_size *= 2;
        if (index == 0)
            return;
    }

    buddy->longest[index] = node_size;
    buddy->free_size += node_size;

    while (index) {
        index = PARENT(index);
        node_size *= 2;

        left_longest = buddy->longest[LEFT_LEAF(index)];
        right_longest = buddy->longest[RIGHT_LEAF(index)];

        if (left_longest + right_longest == node_size)
            buddy->longest[index] = node_size;
        else 
            buddy->longest[index] = MAX(left_longest, right_longest);
    }

}
```

### 其他

next_power_of_2函数求出size的下一个n次幂。

```c
static size_t next_power_of_2(size_t size) {
    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    size |= size >> 8;
    size |= size >> 16;
    return size + 1;
}
```

### 实验结果

![image-20201009220154165](https://whileskies-pic.oss-cn-beijing.aliyuncs.com/20201009220154.png)

### 参考

[伙伴分配器的一个极简实现](https://coolshell.cn/articles/10427.html) 