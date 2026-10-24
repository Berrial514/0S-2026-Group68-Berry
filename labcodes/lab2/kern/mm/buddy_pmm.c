#include <list.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>
#include <buddy_pmm.h>

/* ... 头部注释不变 ... */

#ifdef BUDDY_PMM_DEBUG
#define BUDDY_TRACE(...) cprintf(__VA_ARGS__)
#else
#define BUDDY_TRACE(...) ((void)0)
#endif

#define BUDDY_MAX_ORDER 10

static free_area_t buddy_free_area[BUDDY_MAX_ORDER + 1];

#define free_list(order) (buddy_free_area[order].free_list)
#define nr_free_of(order) (buddy_free_area[order].nr_free)

static size_t buddy_nr_free = 0;

static inline size_t
buddy_size(size_t order)
{
    return (size_t)1 << order;
}

static inline size_t
buddy_round_up_pow2(size_t n)
{
    size_t size = 1;
    while (size < n)
    {
        size <<= 1;
    }
    return size;
}

static inline size_t
buddy_pow2_order(size_t size)
{
    size_t order = 0;
    while ((size >> order) != 1)
    {
        order++;
    }
    return order;
}

static inline size_t
buddy_ppn(struct Page *page)
{
    assert(page >= pages);
    return (size_t)(page - pages) + nbase;
}

static struct Page *
buddy_ppn2page(size_t ppn)
{
    if (ppn < nbase || ppn >= npage)
    {
        return NULL;
    }
    return pages + (ppn - nbase);
}

static inline bool
buddy_ppn_aligned(size_t ppn, size_t order)
{
    return (ppn & (buddy_size(order) - 1)) == 0;
}

static inline bool
buddy_in_range(struct Page *page)
{
    return page >= pages && page < pages + (npage - nbase);
}

static void
buddy_list_add(struct Page *page, size_t order)
{
    list_entry_t *head = &free_list(order);
    list_entry_t *le = list_next(head);
    while (le != head)
    {
        struct Page *p = le2page(le, page_link);
        if (page < p)
        {
            break;
        }
        le = list_next(le);
    }
    list_add_before(le, &(page->page_link));
    nr_free_of(order)++;
    buddy_nr_free += buddy_size(order);
}

static void
buddy_list_del(struct Page *page, size_t order)
{
    list_del_init(&(page->page_link));
    nr_free_of(order)--;
    buddy_nr_free -= buddy_size(order);
}

static struct Page *
buddy_list_first(size_t order)
{
    assert(!list_empty(&free_list(order)));
    return le2page(list_next(&free_list(order)), page_link);
}

static void
buddy_set_free_block(struct Page *page, size_t order)
{
    page->property = (unsigned int)buddy_size(order);
    SetPageProperty(page);
    set_page_ref(page, 0);
}

static void
buddy_clear_free_block(struct Page *page)
{
    page->property = 0;
    ClearPageProperty(page);
}

static void
buddy_init(void)
{
    for (size_t order = 0; order <= BUDDY_MAX_ORDER; order++)
    {
        list_init(&free_list(order));
        nr_free_of(order) = 0;
    }
    buddy_nr_free = 0;
}

static void
buddy_init_split(struct Page *base, size_t n)
{
    while (n > 0)
    {
        size_t order = 0;
        while ((order < BUDDY_MAX_ORDER) && (buddy_size(order + 1) <= n))
        {
            order++;
        }
        struct Page *p = base;
        for (size_t i = 0; i < buddy_size(order); i++, p++)
        {
            assert(PageReserved(p));
            p->flags = p->property = 0;
            set_page_ref(p, 0);
        }
        buddy_set_free_block(base, order);
        buddy_list_add(base, order);
        base += buddy_size(order);
        n -= buddy_size(order);
    }
}

static void
buddy_init_memmap(struct Page *base, size_t n)
{
    assert(n > 0);
    buddy_init_split(base, n);
}

static int
buddy_find_order(size_t order)
{
    for (size_t i = order; i <= BUDDY_MAX_ORDER; i++)
    {
        if (!list_empty(&free_list(i)))
        {
            return (int)i;
        }
    }
    return -1;
}

static struct Page *
buddy_split_block(struct Page *page, size_t from, size_t to)
{
    assert(from <= BUDDY_MAX_ORDER && to <= from);
    while (from > to)
    {
        from--;
        struct Page *buddy = page + buddy_size(from);
        assert(buddy_in_range(buddy));
        buddy_set_free_block(buddy, from);
        buddy_list_add(buddy, from);
    }
    return page;
}

static struct Page *
buddy_alloc_pages(size_t n)
{
    assert(n > 0);
    if (n > buddy_nr_free)
    {
        return NULL;
    }

    size_t size = buddy_round_up_pow2(n);
    if (size > buddy_size(BUDDY_MAX_ORDER))
    {
        return NULL;
    }
    size_t order = buddy_pow2_order(size);

    int found = buddy_find_order(order);
    if (found < 0)
    {
        return NULL;
    }
    size_t cur = (size_t)found;

    struct Page *page = buddy_list_first(cur);
    assert(page != NULL);
    assert(buddy_in_range(page));
    buddy_list_del(page, cur);

    page = buddy_split_block(page, cur, order);

    buddy_clear_free_block(page);
    for (struct Page *p = page; p != page + size; p++)
    {
        assert(!PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }

    assert(buddy_nr_free >= size);
    // buddy_nr_free -= size; // 删除了这行，因为 buddy_list_del 和 buddy_split_block 已经正确维护了

    return page;
}

static int
buddy_merge_once(struct Page *page, size_t order)
{
    size_t ppn = buddy_ppn(page);

    if (!buddy_ppn_aligned(ppn, order))
    {
        return -1;
    }

    struct Page *buddy = buddy_ppn2page(ppn ^ buddy_size(order));
    if (buddy == NULL)
    {
        return -1;
    }
    if (!PageProperty(buddy) || PageReserved(buddy))
    {
        return -1;
    }
    if (buddy->property != (unsigned int)buddy_size(order))
    {
        return -1;
    }
    if (!buddy_ppn_aligned(buddy_ppn(buddy), order))
    {
        return -1;
    }

    struct Page *low = page < buddy ? page : buddy;
    struct Page *high = page < buddy ? buddy : page;

    buddy_list_del(high, order);
    buddy_list_del(low, order);
    buddy_clear_free_block(high);
    buddy_set_free_block(low, order + 1);
    return (int)(order + 1);
}

static void
buddy_free_pages(struct Page *base, size_t n)
{
    assert(n > 0);

    for (struct Page *p = base; p != base + n; p++)
    {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    // buddy_nr_free += n; // 删除这行，依靠 list_add 维护

    size_t ppn = (size_t)(page2pa(base) >> PGSHIFT);

    size_t first_order = BUDDY_MAX_ORDER;
    while ((buddy_size(first_order) > n) ||
           !buddy_ppn_aligned(ppn, first_order))
    {
        assert(first_order > 0);
        first_order--;
    }
    size_t rest_begin = buddy_size(first_order);
    while (rest_begin < n)
    {
        size_t rest = n - rest_begin;
        size_t order = 0;
        while ((order < BUDDY_MAX_ORDER) && (buddy_size(order + 1) <= rest))
        {
            order++;
        }
        struct Page *p = base + rest_begin;
        buddy_set_free_block(p, order);
        buddy_list_add(p, order);
        rest_begin += buddy_size(order);
    }

    struct Page *page = base;
    size_t order = first_order;
    buddy_set_free_block(page, order);

    // 将第一段加入到链表中
    buddy_list_add(page, order);

    while (order < BUDDY_MAX_ORDER)
    {
        int new_order = buddy_merge_once(page, order);
        if (new_order < 0)
        {
            break;
        }
        order = (size_t)new_order;
        page = buddy_ppn2page(buddy_ppn(page) & ~(buddy_size(order) - 1));
        assert(page != NULL);
        // 合并后的新块重新加入链表
        buddy_list_add(page, order);
    }
}

// ... buddy_nr_free_pages 等后面代码保持不变，因为它们本身逻辑是自洽的。

static size_t
buddy_nr_free_pages(void)
{
    return buddy_nr_free;
}

static bool
buddy_pa_aligned(struct Page *base, size_t n)
{
    return !PageProperty(base) &&
           (page2pa(base) & (((size_t)n << PGSHIFT) - 1)) == 0;
}

static bool
buddy_is_free_block(struct Page *page, size_t order)
{
    return PageProperty(page) && !PageReserved(page) &&
           page->property == (unsigned int)buddy_size(order) &&
           buddy_ppn_aligned(buddy_ppn(page), order);
}

static size_t
buddy_total_free_pages(void)
{
    size_t total = 0;
    for (size_t order = 0; order <= BUDDY_MAX_ORDER; order++)
    {
        list_entry_t *head = &free_list(order);
        list_entry_t *le = list_next(head);
        while (le != head)
        {
            struct Page *p = le2page(le, page_link);
            assert(buddy_is_free_block(p, order));
            total += buddy_size(order);
            le = list_next(le);
        }
    }
    return total;
}

struct buddy_state
{
    list_entry_t list[BUDDY_MAX_ORDER + 1];
    unsigned int nr_free[BUDDY_MAX_ORDER + 1];
    size_t total;
};

static void
buddy_save_state(struct buddy_state *st)
{
    for (size_t order = 0; order <= BUDDY_MAX_ORDER; order++)
    {
        st->list[order] = free_list(order);
        st->nr_free[order] = nr_free_of(order);
    }
    st->total = buddy_nr_free;
}

static void
buddy_restore_state(struct buddy_state *st)
{
    for (size_t order = 0; order <= BUDDY_MAX_ORDER; order++)
    {
        free_list(order) = st->list[order];
        nr_free_of(order) = st->nr_free[order];
    }
    buddy_nr_free = st->total;
}

static void
buddy_reset_lists(void)
{
    for (size_t order = 0; order <= BUDDY_MAX_ORDER; order++)
    {
        list_init(&free_list(order));
        nr_free_of(order) = 0;
    }
    buddy_nr_free = 0;
}

static void
buddy_mark_allocated(struct Page *base, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        base[i].flags = 0;
        base[i].property = 0;
        set_page_ref(&base[i], 0);
    }
}

// buddy_check 和 buddy_pmm_manager 的定义完全保持不变