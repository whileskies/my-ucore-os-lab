#include <pmm.h>
#include <buddy_pmm.h>


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

static void
debug_buddy_tree(int level, char *label);

static size_t next_power_of_2(size_t size) {
    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    size |= size >> 8;
    size |= size >> 16;
    return size + 1;
}

static void
buddy_init() {

}

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

    //cprintf("base: %p, longest: %p, begin_page: %p, free_size: %d\n", page2kva(base), buddy->longest, page2kva(buddy->begin_page), buddy->free_size);

    //buddy->size = next_power_of_2(buddy->total_num_page) / 2;
    //buddy->free_size = buddy->size;

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


static size_t
buddy_nr_free_pages(void) {
    size_t total_free_pages = 0;
    for (int i = 0; i < num_buddy_zone; i++) {
        total_free_pages += mem_buddy[i].free_size;
    }
    return total_free_pages;
}


static void
buddy_check(void) {
    size_t total = buddy_nr_free_pages();
    cprintf("total: %d\n", total);

    struct Page *p0 = alloc_page();
    assert(p0 != NULL);
    assert(buddy_nr_free_pages() == total - 1);
    assert(p0 == mem_buddy[0].begin_page);

    struct Page *p1 = alloc_page();
    assert(p1 != NULL);
    assert(buddy_nr_free_pages() == total - 2);
    assert(p1 == mem_buddy[0].begin_page + 1);

    assert(p1 == p0 + 1);

    buddy_free_pages(p0, 1);
    buddy_free_pages(p1, 1);
    assert(buddy_nr_free_pages() == total);

    p0 = buddy_alloc_pages(11);
    assert(buddy_nr_free_pages() == total - 16);

    p1 = buddy_alloc_pages(100);
    assert(buddy_nr_free_pages() == total - 144);

    buddy_free_pages(p0, -1);
    buddy_free_pages(p1, -1);
    assert(buddy_nr_free_pages() == total);

    p0 = buddy_alloc_pages(total);
    assert(p0 == NULL);

    // debug_buddy_tree(7, "221, init");
    p0 = buddy_alloc_pages(512);
    // debug_buddy_tree(7, "221, alloc 512");
    assert(buddy_nr_free_pages() == total - 512);

    p1 = buddy_alloc_pages(1024);
    // debug_buddy_tree(7, "225, alloc 1024");
    assert(buddy_nr_free_pages() == total - 512 - 1024);

    struct Page *p2 = buddy_alloc_pages(2048);
    // debug_buddy_tree(7, "229, alloc 2048");
    assert(buddy_nr_free_pages() == total - 512 - 1024 - 2048);

    struct Page *p3 = buddy_alloc_pages(4096);
    // debug_buddy_tree(7, "233, alloc 4096");
    assert(buddy_nr_free_pages() == total - 512 - 1024 - 2048 - 4096);

    struct Page *p4 = buddy_alloc_pages(8192);
    // debug_buddy_tree(7, "237, alloc 8192");
    assert(buddy_nr_free_pages() == total - 512 - 1024 - 2048 - 4096 - 8192);

    struct Page *p5 = buddy_alloc_pages(8192);
    // debug_buddy_tree(7, "241, alloc 8192");
    assert(buddy_nr_free_pages() == total - 512 - 1024 - 2048 - 4096 - 8192 - 8192);

    buddy_free_pages(p0, -1);
    buddy_free_pages(p1, -1);
    buddy_free_pages(p2, -1);
    buddy_free_pages(p3, -1);
    buddy_free_pages(p4, -1);
    buddy_free_pages(p5, -1);

    assert(buddy_nr_free_pages() == total);

}


static void
debug_buddy_tree(int level, char *label) {
    cprintf("\ndebug buddy tree: %s\n", label);

    int num = 1;
    int index = 0;
    for (int i = 0; i < level; i++) {
        int cur_num = 0;
        while (1) {
            cprintf("%d ", mem_buddy[0].longest[index++]);
            cur_num++;
            if (cur_num == num) {
                cprintf("\n");
                cur_num = 0;
                num *= 2;
                break;
            }
        }
    }
    cprintf("debug buddy tree end\n\n");
}


const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};