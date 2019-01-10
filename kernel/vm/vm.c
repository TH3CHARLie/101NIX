#include "vm.h"
#include <driver/vga.h>
#include <exc.h>
#include <intr.h>
#include <zjunix/slab.h>
#include <zjunix/syscall.h>
#include <zjunix/utils.h>

#pragma GCC push_options
#pragma GCC optimize("O0")

struct shared_page_struct shared_pages;

void init_vm() {
    register_exception_handler(2, tlb_refill);
    register_exception_handler(3, tlb_refill);
    init_shared_page();
}

void* vm_create() { return ptd_create(); }

void vm_delete(task_struct* pcb) {
    tlb_delete(pcb->pid);
    ptd_delete(pcb, pcb->vm);
}

void vm_print(void* vm) {
    void* ptd = vm;
    int i;
    for (i = 0; i < PAGE_SIZE / sizeof(void*); i++) {
        void* pt = (void*)(((int*)ptd)[i]);
        if (pt != NULL) {
            int j;
            for (j = 0; j < PAGE_SIZE / sizeof(void*); j++) {
                void* pte = (void*)(((int*)pt)[j]);
                if (pte != NULL) {
                    void* virtual_addr = (void*)((i << 22) | (j << 12));
                    kernel_printf("virtual addr = %x, physical addr = %x\n",
                                  (unsigned int)virtual_addr,
                                  (unsigned int)pte);
                }
            }
        }
    }
}

void set_active_asid(unsigned int asid) {
    asm volatile(
        "mfc0 $t0, $10\n\t"
        "li $t1, 0xffffe000\n\t"
        "and $t0, $t0, $t1\n\t"
        "or $t0, $t0, %0\n\t"
        "mtc0 $t0, $10\n\t"
        "nop"
        :
        : "r"(asid));
}

void* ptd_create() {
    // kernel_printf("[debug][ptd_create]aaa\n");
    void* ptd = kmalloc(PAGE_SIZE);
    // kernel_printf("[debug][ptd_create]bbb\n");
    if (ptd == NULL) {
        // kernel_printf("[debug][ptd_create] no memory !\n");
        while (1)
            ;
    }
    // kernel_printf("[debug][ptd_create]ptd:%x\n", ptd);
    kernel_memset(ptd, 0, PAGE_SIZE);
    // kernel_printf("[debug][ptd_create]ccc\n");
    return ptd;
}

void ptd_delete(task_struct* pcb, void* ptd) {
    int i;
    for (i = 0; i < PAGE_SIZE / sizeof(void*); i++) {
        void* pt = (void*)(((int*)ptd)[i]);
        if (pt != NULL) {
            pt_delete(pcb, pt);
            ((int*)ptd)[i] = NULL;
        }
    }
    kfree(ptd);
}

void* pt_create() {
    void* pt = kmalloc(PAGE_SIZE);
    kernel_memset(pt, 0, PAGE_SIZE);
    return pt;
}

void pt_delete(task_struct* pcb, void* pt) {
    int i;
    for (i = 0; i < PAGE_SIZE / sizeof(void*); i++) {
        void* pte = (void*)(((int*)pt)[i]);
        if (pte != NULL) {
            if (shared_page_delete(pcb, pte) != 0) {
                kfree(pte);
            }
            ((int*)pt)[i] = NULL;
        }
    }
    kfree(pt);
}

void tlb_refill(unsigned int status, unsigned int cause, context* pt_context) {
    task_struct* pcb;
    void* virtual_addr;
    void* ptd;
    unsigned int ptd_index;
    void* pt;
    unsigned int pt_index;
    void* pte;
    unsigned int pt_near_index;
    void* pte_near;
    unsigned int entry_lo0;
    unsigned int entry_lo1;
    unsigned int entry_hi;
    unsigned int old_ie;
    old_ie = disable_interrupts();
    asm volatile("mfc0 %0, $8\n\t" : "=r"(virtual_addr));
    pcb = get_current_task();
    // kernel_printf("current_task name %s vm=%x\n", pcb->name,
    //               (unsigned int)pcb->vm);
    ptd = pcb->vm;
    if (ptd == NULL) {
        kernel_printf(
            "[tlb_refill]: Error. Process %s exited due to accessing addr=%x, "
            "*addr=%x "
            "epc=%x\n",
            pcb->name, (unsigned int)virtual_addr,
            *((unsigned int*)virtual_addr), (unsigned int)pcb->context.epc);
        while (1)
            ;
    }
    ptd_index = get_ptd_index(virtual_addr);
    pt = (void*)((unsigned int*)ptd)[ptd_index];
    if (pt == NULL) {
        pt = pt_create();
        ((unsigned int*)ptd)[ptd_index] = (unsigned int)pt;
    }
    pt_index = get_pt_index(virtual_addr);
    pte = (void*)((unsigned int*)pt)[pt_index];
    if (pte == NULL) {
        if ((unsigned int)virtual_addr >= 0x10000) {
            kernel_printf(
                "[tlb_refill]: Error. Process %s exited due to accessing "
                "addr=%x, *addr=%x "
                "epc=%x\n",
                pcb->name, (unsigned int)virtual_addr,
                *((unsigned int*)virtual_addr), (unsigned int)pcb->context.epc);
            while (1)
                ;
        }
        pte = kmalloc(PAGE_SIZE);
        kernel_memset(pte, 0, PAGE_SIZE);
        ((unsigned int*)pt)[pt_index] = (unsigned int)pte;
    }
    pt_near_index = pt_index ^ 1;
    pte_near = (void*)((unsigned int*)pt)[pt_near_index];
    if (pte_near == NULL) {
        pte_near = kmalloc(PAGE_SIZE);
        kernel_memset(pte_near, 0, PAGE_SIZE);
        ((unsigned int*)pt)[pt_near_index] = (unsigned int)pte_near;
    }
    if ((pt_index & 1) == 0) {
        entry_lo0 = get_entry_lo(pte);
        entry_lo1 = get_entry_lo(pte_near);
    } else {
        entry_lo0 = get_entry_lo(pte_near);
        entry_lo1 = get_entry_lo(pte);
    }
    entry_hi = get_entry_hi(virtual_addr);
    asm volatile(
        "mtc0 $zero, $5\n\t"
        "mtc0 %0, $10\n\t"
        "mtc0 %1, $2\n\t"
        "mtc0 %2, $3\n\t"
        "nop\n\t"
        "nop\n\t"
        "tlbwr\n\t"
        "nop\n\t"
        "nop\n\t"
        :
        : "r"(entry_hi), "r"(entry_lo0), "r"(entry_lo1));
    if (old_ie) {
        enable_interrupts();
    }
}

void tlb_delete(unsigned int asid) {
    int i;
    for (i = 0; i < 32; i++) {
        unsigned int entry_hi;
        unsigned int curr_asid;
        asm volatile(
            "mtc0 %1, $0\n\t"
            "nop\n\t"
            "tlbr\n\t"
            "nop\n\t"
            "mfc0 %0, $10\n\t"
            "nop"
            : "=r"(entry_hi)
            : "r"(i));
        curr_asid = entry_hi & 0xFF;
        if (curr_asid != asid) {
            continue;
        }
        asm volatile(
            "mtc0 $zero, $2\n\t"
            "mtc0 $zero, $3\n\t"
            "mtc0 $zero, $5\n\t"
            "mtc0 $zero, $10\n\t"
            "mtc0 %0, $0\n\t"
            "tlbwi\n\t"
            "nop" ::"r"(i));
    }
}

unsigned int get_ptd_index(void* addr) { return ((unsigned int)addr >> 22); }

unsigned int get_pt_index(void* addr) {
    return ((((unsigned int)addr) & 0x003FF000) >> 12);
}

unsigned int get_entry_lo(void* pte) {
    void* physical_addr;
    unsigned int entry_lo;
    physical_addr = (void*)((unsigned int)pte - 0x80000000);
    entry_lo = ((unsigned int)physical_addr >> 12) << 6;
    entry_lo |= (3 << 3);
    entry_lo |= 0x06;
    return entry_lo;
}

unsigned int get_entry_hi(void* addr) {
    unsigned int asid;
    asm volatile(
        "mfc0 $t0, $10\n\t"
        "nop\n\t"
        "andi %0, $t0, 0xFF\n\t"
        : "=r"(asid));
    return (((unsigned int)addr & 0xFFFFE000) | asid);
}

void init_shared_page() { INIT_LIST_HEAD(&shared_pages.list); }

void* shared_page_create(task_struct* pcb, const char* name) {
    void* virtual_addr;
    struct shared_page_struct* shared_page;
    virtual_addr = (void*)0x10000;
    while (vma_va_to_pa(pcb, virtual_addr) != NULL) {
        virtual_addr = (void*)((unsigned int)virtual_addr + 0x1000);
    }
    shared_page = find_shared_page_by_name(name);
    if (shared_page != NULL) {
        shared_page->count++;
    } else {
        shared_page = kmalloc(sizeof(struct shared_page_struct));
        kernel_strcpy(shared_page->name, name);
        shared_page->count = 1;
        shared_page->physical_addr = kmalloc(PAGE_SIZE);
        kernel_memset(shared_page->physical_addr, 0, PAGE_SIZE);
        list_add(&(shared_page->list), &(shared_pages.list));
    }
    vma_set_mapping(pcb, virtual_addr, shared_page->physical_addr);
    return virtual_addr;
}

int shared_page_delete(task_struct* pcb, void* virtual_addr) {
    void* physical_addr;
    struct shared_page_struct* shared_page;
    physical_addr = vma_va_to_pa(pcb, virtual_addr);
    if (physical_addr == NULL) {
        return 1;
    }
    shared_page = find_shared_page_by_physical_addr(physical_addr);
    if (shared_page == NULL) {
        return 2;
    }
    vma_set_mapping(pcb, virtual_addr, NULL);
    tlb_delete(pcb->pid);
    shared_page->count--;
    if (shared_page->count == 0) {
        kfree(shared_page->physical_addr);
        list_del_init(&shared_page->list);
    }
    return 0;
}

struct shared_page_struct* find_shared_page_by_name(const char* name) {
    struct list_head* pos;
    list_for_each(pos, &shared_pages.list) {
        struct shared_page_struct* shared_page =
            list_entry(pos, struct shared_page_struct, list);
        if (kernel_strcmp(shared_page->name, name) == 0) {
            return shared_page;
        }
    }
    return NULL;
}

struct shared_page_struct* find_shared_page_by_physical_addr(
    void* physical_addr) {
    struct list_head* pos;
    list_for_each(pos, &shared_pages.list) {
        struct shared_page_struct* shared_page =
            list_entry(pos, struct shared_page_struct, list);
        if (shared_page->physical_addr == physical_addr) {
            return shared_page;
        }
    }
    return NULL;
}

void* vma_va_to_pa(task_struct* pcb, void* virtual_addr) {
    void* ptd;
    unsigned int ptd_index;
    void* pt;
    unsigned int pt_index;
    void* pte;
    ptd = pcb->vm;
    ptd_index = get_ptd_index(virtual_addr);
    pt = (void*)((unsigned int*)ptd)[ptd_index];
    if (pt == NULL) {
        return NULL;
    }
    pt_index = get_pt_index(virtual_addr);
    pte = (void*)((unsigned int*)pt)[pt_index];
    if (pte == NULL) {
        return NULL;
    }
    return (void*)((unsigned int)pte | ((unsigned int)virtual_addr & 0xFFF));
}

void vma_set_mapping(task_struct* pcb, void* virtual_addr,
                     void* physical_addr) {
    void* ptd;
    unsigned int ptd_index;
    void* pt;
    unsigned int pt_index;
    ptd = pcb->vm;
    ptd_index = get_ptd_index(virtual_addr);
    pt = (void*)((unsigned int*)ptd)[ptd_index];
    if (pt == NULL) {
        pt = pt_create();
        ((unsigned int*)ptd)[ptd_index] = (unsigned int)pt;
    }
    pt_index = get_pt_index(virtual_addr);
    ((unsigned int*)pt)[pt_index] = (unsigned int)physical_addr;
}

void memory_pool_create(task_struct* pcb) {
    int i;
    for (i = 0; i < 8; i++) {
        unsigned int chunk_size;
        pcb->user_pc_memory_blocks[i] = kmalloc(sizeof(memory_block_struct));
        kernel_memset(pcb->user_pc_memory_blocks[i], 0,
                      sizeof(memory_block_struct));
        chunk_size = 16 << i;
        // kernel_printf("[debug][memory_pool_create]chunk_size: %d\n",
        // chunk_size);
        init_memory_block(pcb, pcb->user_pc_memory_blocks[i], chunk_size);
    }
    pcb->user_pc_memory_blocks[8] = NULL;
}

void memory_pool_delete(task_struct* pcb) {
    int i;
    // kernel_printf("[debug][memory_pool_delete]start\n");
    for (i = 0; i < 9; i++) {
        memory_block_struct* memory_block = pcb->user_pc_memory_blocks[i];
        while (memory_block) {
            memory_block_struct* next_memory_block;
            // kernel_printf(
            //     "[debug][memory_pool_delete]virtual_addr: %x, "
            //     "physical_addr:%x\n",
            //     memory_block->base_virtual_addr,
            //     memory_block->base_physical_addr);
            vma_set_mapping(pcb, memory_block->base_virtual_addr, NULL);
            next_memory_block = memory_block->next_memory_block;
            kfree(memory_block);
            memory_block = next_memory_block;
        }
    }
}

void* memory_alloc(task_struct* pcb, unsigned int size) {
    int i;
    // kernel_printf("[debug][memory_alloc]size: %d\n", size);
    for (i = 0; i < 8; i++) {
        if (pcb->user_pc_memory_blocks[i]->chunk_size >= size) {
            break;
        }
    }
    if (i >= 8) {
        return big_chunk_alloc(pcb, size);
    } else {
        return small_chunk_alloc(pcb, pcb->user_pc_memory_blocks[i]);
    }
}

int memory_free(task_struct* pcb, void* virtual_addr) {
    int i;
    // kernel_printf("[debug][memory_free]virtual_addr:%x\n", virtual_addr);
    for (i = 0; i < 8; i++) {
        memory_block_struct* memory_block = pcb->user_pc_memory_blocks[i];
        while (memory_block) {
            void *start_addr, *end_addr;
            start_addr = memory_block->base_virtual_addr;
            end_addr = (void*)((unsigned int)start_addr + PAGE_SIZE);
            if (virtual_addr >= start_addr && virtual_addr < end_addr) {
                void* physical_addr = vma_va_to_pa(pcb, virtual_addr);
                *(unsigned int*)physical_addr =
                    (unsigned int)pcb->user_pc_memory_blocks[i]->head;
                pcb->user_pc_memory_blocks[i]->head = physical_addr;
                // kernel_printf("[debug][memory_free]physical_addr:%x\n",
                //               physical_addr);
                return 0;
            }
            memory_block = memory_block->next_memory_block;
        }
    }
    return 1;
}

void init_memory_block(task_struct* pcb, memory_block_struct* memory_block,
                       unsigned int chunk_size) {
    void* virtual_addr;
    void* physical_addr;
    virtual_addr = (void*)0x10000;
    while (vma_va_to_pa(pcb, virtual_addr) != NULL) {
        virtual_addr = (void*)((unsigned int)virtual_addr + 0x1000);
    }
    physical_addr = kmalloc(PAGE_SIZE);
    kernel_memset(physical_addr, 0, PAGE_SIZE);
    vma_set_mapping(pcb, virtual_addr, physical_addr);
    memory_block->base_virtual_addr = virtual_addr;
    memory_block->base_physical_addr = physical_addr;
    memory_block->chunk_size = chunk_size;
    memory_block->head = physical_addr;
    memory_block->next_memory_block = NULL;
    // kernel_printf(
    //     "[debug][init_memory_block]chunk_size: %d, bva: %x, "
    //     "bpa: %x\n",
    //     chunk_size, memory_block->base_virtual_addr,
    //     memory_block->base_physical_addr);
    init_chunk_list(memory_block);
}

void init_chunk_list(memory_block_struct* memory_block) {
    int i;
    unsigned int* p;
    i = 0;
    p = (unsigned int*)memory_block->head;
    while (1) {
        i += memory_block->chunk_size;
        if (i < PAGE_SIZE) {
            unsigned int* q =
                (unsigned int*)((unsigned int)p + memory_block->chunk_size);
            *p = (unsigned int)q;
            p = q;
        } else {
            *p = (unsigned int)NULL;
            break;
        }
    }
}

void* small_chunk_alloc(task_struct* pcb, memory_block_struct* memory_block) {
    void* physical_addr = memory_block->head;
    // kernel_printf("[debug][small_chunk_alloc]memory_block_chunk_size: %d\n",
    //               memory_block->chunk_size);
    if (physical_addr != NULL) {
        void* virtual_addr;
        memory_block_struct* curr_memory_block = memory_block;
        while (curr_memory_block) {
            void *start_addr, *end_addr;
            start_addr = curr_memory_block->base_physical_addr;
            end_addr = (void*)((unsigned int)start_addr + PAGE_SIZE);
            if (physical_addr >= start_addr && physical_addr < end_addr) {
                virtual_addr = physical_addr -
                               curr_memory_block->base_physical_addr +
                               curr_memory_block->base_virtual_addr;
                break;
            }
            curr_memory_block = curr_memory_block->next_memory_block;
        }
        memory_block->head = (void*)(*(int*)physical_addr);
        // kernel_printf(
        //     "[debug][small_chunk_alloc]virtual_addr: %x, physical_addr: %x, "
        //     "new "
        //     "head:%x\n",
        //     virtual_addr, physical_addr, memory_block->head);
        return virtual_addr;
    } else {
        memory_block_struct* new_memory_block;
        memory_block_struct* last_memory_block;
        new_memory_block =
            (memory_block_struct*)kmalloc(sizeof(memory_block_struct));
        init_memory_block(pcb, new_memory_block, memory_block->chunk_size);
        memory_block->head = new_memory_block->head;
        last_memory_block = memory_block;
        while (last_memory_block->next_memory_block != NULL) {
            last_memory_block = memory_block->next_memory_block;
        }
        last_memory_block->next_memory_block = new_memory_block;
        return small_chunk_alloc(pcb, memory_block);
    }
}

void* big_chunk_alloc(task_struct* pcb, unsigned int size) {
    void* virtual_addr;
    void* physical_addr;
    int num_of_pages;
    int i;
    size += PAGE_SIZE - 1;
    size &= ~(PAGE_SIZE - 1);
    num_of_pages = size / PAGE_SIZE;
    virtual_addr = (void*)0x10000;
    while (1) {
        int found = 1;
        for (i = 0; i < num_of_pages; i++) {
            void* curr_virtual_addr =
                (void*)((unsigned int)virtual_addr + i * PAGE_SIZE);
            if (vma_va_to_pa(pcb, curr_virtual_addr) != NULL) {
                virtual_addr = curr_virtual_addr;
                found = 0;
                break;
            }
        }
        if (found == 1) {
            break;
        }
    }
    physical_addr = kmalloc(size);
    kernel_memset(physical_addr, 0, size);
    for (i = 0; i < num_of_pages; i++) {
        memory_block_struct* memory_block;
        memory_block_struct* last_memory_block;
        void* curr_virtual_addr =
            (void*)((unsigned int)virtual_addr + i * PAGE_SIZE);
        void* curr_physical_addr =
            (void*)((unsigned int)physical_addr + i * PAGE_SIZE);
        vma_set_mapping(pcb, curr_virtual_addr, curr_physical_addr);
        // kernel_printf(
        //     "[debug][big_chunk_alloc]mapping, virtual_addr: %x, "
        //     "physical_addr: "
        //     "%x\n",
        //     curr_virtual_addr, curr_physical_addr);
        memory_block = kmalloc(sizeof(memory_block_struct));
        memory_block->base_virtual_addr = curr_virtual_addr;
        memory_block->base_physical_addr = curr_physical_addr;
        memory_block->chunk_size = 0;
        memory_block->head = NULL;
        memory_block->next_memory_block = NULL;
        last_memory_block = pcb->user_pc_memory_blocks[8];
        if (last_memory_block == NULL) {
            pcb->user_pc_memory_blocks[8] = memory_block;
        } else {
            while (last_memory_block->next_memory_block != NULL) {
                last_memory_block = last_memory_block->next_memory_block;
            }
            last_memory_block->next_memory_block = memory_block;
        }
    }
    // kernel_printf(
    //     "[debug][big_chunk_alloc]result, virtual_addr: %x, physical_addr: "
    //     "%x\n",
    //     virtual_addr, physical_addr);
    return virtual_addr;
}

void vma_proc() {
    int i;
    asm volatile(
        "xor $t0, $t0, $t0\n\t"
        "lw $t1, 0($t0)\n\t"
        "addi $t0, $zero, 4092\n\t"
        "sw $t1, 0($t0)\n\t"
        "addi $t0, $zero, 4096\n\t"
        "sw $t1, 0($t0)\n\t"
        "addi $t0, $zero, 8188\n\t"
        "lw $t1, 0($t0)\n\t"
        "addi $t0, $zero, 8192\n\t"
        "sw $t1, 0($t0)\n\t"
        "addi $t0, $zero, 12288\n\t"
        "sw $t1, 0($t0)\n\t"
        "addi $t0, $zero, 20488\n\t"
        "sw $t1, 0($t0)\n\t"
        "addi $t0, $zero, 16388\n\t"
        "lw $t1, 0($t0)\n\t"
        "addi $t1, $zero, 1997\n\t"
        "sw $t1, 24580($zero)\n\t"
        "lw %0, 24580($zero)\n\t"
        : "=r"(i));
    kernel_printf("i=%d\n", i);
    for (i = 0; i < 10; i++) {
        int val;
        int* p = (int*)((i * 4) + 0x8000);
        *p = 12345678 + i;
        val = *p;
        kernel_printf("addr: %x, write: %d, read: %d\n", p, 12345678 + i, val);
    }
    vm_print(get_current_task()->vm);
    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}

void page_share_proc_1() {
    unsigned int* shared_page;
    unsigned int* shared_page_physical_addr;
    int i;
    const char* name = "share_page";
    asm volatile(
        "move $a0, %1\n\t"
        "li $v0, 50\n\t"
        "syscall\n\t"
        "move %0, $v0"
        : "=r"(shared_page)
        : "r"(name));
    shared_page_physical_addr = vma_va_to_pa(get_current_task(), shared_page);
    kernel_printf("[page_share_proc_1]shared_page va: %x, pa: %x\n",
                  shared_page, shared_page_physical_addr);
    sleep(1000 * 1000 * 30);
    for (i = 0; i < 10; i++) {
        int val;
        val = shared_page[i];
        kernel_printf(
            "[page_share_proc_1]read shared memory by virtual addr, index: "
            "%d, "
            "val: %d\n",
            i, val);
    }
    asm volatile(
        "move $a0, %0\n\t"
        "li $v0, 51\n\t"
        "syscall\n\t"
        :
        : "r"(shared_page));
    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}

void page_share_proc_2() {
    unsigned int* shared_page;
    unsigned int* shared_page_physical_addr;
    int i;
    const char* name = "share_page";
    const char* name_no_use = "share_page_no_use";
    asm volatile(
        "move $a0, %1\n\t"
        "li $v0, 50\n\t"
        "syscall\n\t"
        "move %0, $v0"
        : "=r"(shared_page)
        : "r"(name_no_use));
    asm volatile(
        "move $a0, %1\n\t"
        "li $v0, 50\n\t"
        "syscall\n\t"
        "move %0, $v0"
        : "=r"(shared_page)
        : "r"(name));
    shared_page_physical_addr = vma_va_to_pa(get_current_task(), shared_page);
    kernel_printf("[page_share_proc_2]shared_page va: %x, pa: %x\n",
                  shared_page, shared_page_physical_addr);
    for (i = 0; i < 10; i++) {
        shared_page[i] = i;
        kernel_printf(
            "[page_share_proc_2]write shared memory by virtual addr, "
            "index: "
            "%d, "
            "val: %d\n",
            i, i);
    }
    asm volatile(
        "move $a0, %0\n\t"
        "li $v0, 51\n\t"
        "syscall\n\t"
        :
        : "r"(shared_page));
    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}

void buffer_proc_print() {
    int count = 0;
    void* head = get_current_task()->user_pc_memory_blocks[5]->head;
    kernel_printf("****************************\n");
    while (head != NULL) {
        kernel_printf("%x\n", head);
        head = (void*)(*(unsigned int*)head);
        count++;
    }
    kernel_printf("[buffer_proc]count: %d\n", count);
    kernel_printf("****************************\n");
    sleep(1000 * 1000 * 1);
}

void buffer_proc() {
    unsigned int* buffer[17];
    unsigned int* buffer_physical_addr[17];
    int i;

    buffer_proc_print();  // 8

    for (i = 0; i < 17; i++) {
        asm volatile(
            "li $a0, 512\n\t"
            "li $v0, 60\n\t"
            "syscall\n\t"
            "move %0, $v0"
            : "=r"(buffer[i]));
        buffer_physical_addr[i] = vma_va_to_pa(get_current_task(), buffer[i]);
        kernel_printf("[buffer_proc]index: %d, va: %x, pa: %x\n", i, buffer[i],
                      buffer_physical_addr[i]);
        sleep(1000 * 1000 * 1);
        if (i == 15) {
            buffer_proc_print();  // 0
        }
    }
    buffer_proc_print();  // 7
    for (i = 0; i < 17; i++) {
        asm volatile(
            "move $a0, %0\n\t"
            "li $v0, 61\n\t"
            "syscall\n\t"
            :
            : "r"(buffer[i]));
    }
    buffer_proc_print();  // 24

    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}

#pragma GCC pop_options