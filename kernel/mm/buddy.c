#include <driver/vga.h>
#include <zjunix/bootmm.h>
#include <zjunix/buddy.h>
#include <zjunix/list.h>
#include <zjunix/lock.h>
#include <zjunix/utils.h>

struct page *pages;
struct buddy_sys buddy;

// void set_bplevel(struct page* bp, unsigned int bplevel)
//{
//	bp->bplevel = bplevel;
//}

void buddy_info() {
    unsigned int index;
    kernel_printf("Buddy-system :\n");
    kernel_printf("\tstart page-frame number : %x\n", buddy.buddy_start_pfn);
    kernel_printf("\tend page-frame number : %x\n", buddy.buddy_end_pfn);
    for (index = 0; index <= MAX_BUDDY_ORDER; ++index) {
        kernel_printf("\t(%x)# : %x frees\n", index,
                      buddy.freelist[index].nr_free);
    }
}

// this function is to init all memory with page struct
void init_pages(unsigned int start_pfn, unsigned int end_pfn) {
    unsigned int i;
    for (i = start_pfn; i < end_pfn; i++) {
        set_flag(pages + i, _PAGE_RESERVED);
        (pages + i)->reference = 1;
        (pages + i)->virtual = (void *)(-1);
        (pages + i)->bplevel = (-1);
        (pages + i)->slabp = 0;  // initially, the free space is the whole page
        INIT_LIST_HEAD(&(pages[i].list));
    }
}

void init_buddy() {
    unsigned int i;
    unsigned int kernel_start_pfn, kernel_end_pfn;

    pages = (struct page *)((unsigned int)bootmm_alloc_pages(
                                sizeof(struct page) * bmm.max_pfn, _MM_KERNEL) |
                            0x80000000);

    init_pages(0, bmm.max_pfn);

    kernel_start_pfn = 0;
    kernel_end_pfn = 0;
    for (i = 0; i < bmm.cnt_infos; ++i) {
        if (bmm.info[i].end > kernel_end_pfn) kernel_end_pfn = bmm.info[i].end;
    }
    kernel_end_pfn >>= PAGE_SHIFT;
    buddy.buddy_start_pfn = kernel_end_pfn + 1;
    buddy.buddy_start_pfn += (1 << MAX_BUDDY_ORDER) - 1;
    buddy.buddy_start_pfn &= ~((1 << MAX_BUDDY_ORDER) - 1);
    buddy.buddy_end_pfn =
        bmm.max_pfn & ~((1 << MAX_BUDDY_ORDER) - 1);  // remain 2 pages for I/O

    // init freelists of all bplevels
    for (i = 0; i <= MAX_BUDDY_ORDER; i++) {
        buddy.freelist[i].nr_free = 0;
        INIT_LIST_HEAD(&(buddy.freelist[i].free_head));
    }
    buddy.start_page = pages + buddy.buddy_start_pfn;
    init_lock(&(buddy.lock));

    for (i = buddy.buddy_start_pfn; i < buddy.buddy_end_pfn; ++i) {
        __free_pages(pages + i, 0);
    }
}

void __free_pages(struct page *pbpage, unsigned int bplevel) {
    /* page_idx -> the current page
     * bgroup_idx -> the buddy group that current page is in
     */
    if (pbpage->flag == _PAGE_FREE) {
        return;
    }
    set_flag(pbpage, _PAGE_FREE);
    unsigned int page_idx, bgroup_idx;
    unsigned int combined_idx, tmp;
    struct page *bgroup_page;

    // dec_ref(pbpage, 1);
    // if(pbpage->reference)
    //	return;

    lockup(&buddy.lock);

    page_idx = pbpage - buddy.start_page;
    // complier do the sizeof(struct) operation, and now page_idx is the index

    while (bplevel < MAX_BUDDY_ORDER) {
        bgroup_idx = page_idx ^ (1 << bplevel);
        bgroup_page = pbpage + (bgroup_idx - page_idx);
        if (bgroup_page->flag != _PAGE_FREE) {
            break;  // Its pair has been allocated or reserved
        }
        if (!_is_same_bplevel(bgroup_page, bplevel)) {
            break;
        }
        list_del_init(&bgroup_page->list);
        --buddy.freelist[bplevel].nr_free;
        set_bplevel(bgroup_page, -1);
        combined_idx = bgroup_idx & page_idx;
        pbpage += (combined_idx - page_idx);
        page_idx = combined_idx;
        ++bplevel;
    }
    set_bplevel(pbpage, bplevel);
    set_flag(pbpage, _PAGE_FREE);
    list_add(&(pbpage->list), &(buddy.freelist[bplevel].free_head));
    ++buddy.freelist[bplevel].nr_free;
    unlock(&buddy.lock);
}

struct page *__alloc_pages(unsigned int bplevel) {
    unsigned int current_order, size;
    struct page *page, *buddy_page;
    struct freelist *free;

    lockup(&buddy.lock);

    for (current_order = bplevel; current_order <= MAX_BUDDY_ORDER;
         ++current_order) {
        free = buddy.freelist + current_order;
        if (!list_empty(&(free->free_head))) goto found;
    }

    unlock(&buddy.lock);
    return 0;

found:
    page = container_of(free->free_head.next, struct page, list);
    list_del_init(&(page->list));
    set_bplevel(page, bplevel);
    set_flag(page, _PAGE_ALLOCED);
    // set_ref(page, 1);
    --(free->nr_free);

    size = 1 << current_order;
    while (current_order > bplevel) {
        --free;
        --current_order;
        size >>= 1;
        buddy_page = page + size;
        list_add(&(buddy_page->list), &(free->free_head));
        ++(free->nr_free);
        set_bplevel(buddy_page, current_order);
    }

    unlock(&buddy.lock);
    return page;
}

void *alloc_pages(unsigned int bplevel) {
    struct page *page = __alloc_pages(bplevel);

    if (!page) return 0;

    return (void *)((page - pages) << PAGE_SHIFT);
}

void free_pages(void *addr, unsigned int bplevel) {
    __free_pages(pages + ((unsigned int)addr >> PAGE_SHIFT), bplevel);
}