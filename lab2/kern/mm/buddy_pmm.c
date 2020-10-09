#include <pmm.h>
#include <buddy_pmm.h>

size_t buddy_size;
size_t *longest;
size_t free_size;
struct Page *mem_begin_page;

struct buddy {
    size_t size;
    uintptr_t *longest;
    size_t free_size;
    struct Page *begin_page;
    list_entry_t link;
};

struct buddy mem_buddy_link;


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
    list_init(&mem_buddy_link.link);
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    struct buddy buddy;
    buddy.longest = page2ka(base);
    
    buddy.begin_page = pa2page(PADDR(ROUND_UP(buddy.longest + 2 * n * sizeof(uintptr_t), PGSIZE)));
    buddy_free_size = buddy.begin_page - base;
    

    mem_begin_page = base;
    buddy_size = next_power_of_2(n) / 2;
    free_size = buddy_size;
    size_t node_size = buddy_size * 2;
    
    for (int i = 0; i < 2 * buddy_size - 1; i++) {
        if (IS_POWER_OF_2(i + 1)) {
            node_size /= 2;
        }
        longest[i] = node_size;
    }

    struct Page *p = base;
    for (; p != base + buddy_size; p ++) {
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

    if (longest[index] < n) {
        return NULL;
    }

    for (node_size = buddy_size; node_size != n; node_size /= 2) {
        if (longest[LEFT_LEAF(index)] >= n) 
            index = LEFT_LEAF(index);
        else 
            index = RIGHT_LEAF(index);
    }

    longest[index] = 0;
    offset = (index + 1) * node_size - buddy_size;

    while (index) {
        index = PARENT(index);
        longest[index] = MAX(longest[LEFT_LEAF(index)], longest[RIGHT_LEAF(index)]);
    }

    free_size -= n;

    return mem_begin_page + offset;
}


static void
buddy_free_pages(struct Page *base, size_t n) {
    unsigned node_size, index = 0;
    unsigned left_longest, right_longest;
    unsigned offset = base - mem_begin_page;

    assert(offset >= 0 && offset < buddy_size);

    node_size = 1;
    index = offset + buddy_size - 1;

    for (; longest[index]; index = PARENT(index)) {
        node_size *= 2;
        if (index == 0)
            return;
    }

    longest[index] = node_size;
    free_size += node_size;

    while (index) {
        index = PARENT(index);
        node_size *= 2;

        left_longest = longest[LEFT_LEAF(index)];
        right_longest = longest[RIGHT_LEAF(index)];

        if (left_longest + right_longest == node_size)
            longest[index] = node_size;
        else 
            longest[index] = MAX(left_longest, right_longest);
    }

}


static size_t
buddy_nr_free_pages(void) {
    return free_size;
}


static void
buddy_check(void) {
    cprintf("buddy_size: %d\n", buddy_size);
    for (int i = 0; i < 10; i++) {
        cprintf("longest[%d] = %d\n", i, longest[i]);
    }

    assert(buddy_nr_free_pages() == buddy_size);
    struct Page *p0 = alloc_page();
    assert(p0 != NULL);
    assert(buddy_nr_free_pages() == buddy_size - 1);
    assert(p0 == mem_begin_page);

    struct Page *p1 = alloc_page();
    assert(p1 != NULL);
    assert(buddy_nr_free_pages() == buddy_size - 2);
    assert(p1 == mem_begin_page + 1);

    buddy_free_pages(p0, 1);
    buddy_free_pages(p1, 1);
    assert(buddy_nr_free_pages() == buddy_size);

    p0 = buddy_alloc_pages(11);
    assert(buddy_nr_free_pages() == buddy_size - 16);

    p1 = buddy_alloc_pages(100);

    for (int i = 0; i < 10; i++) {
        cprintf("longest[%d] = %d\n", i, longest[i]);
    }
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