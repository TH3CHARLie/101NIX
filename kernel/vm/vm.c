#include "vm.h"
#include <driver/vga.h>
#include <exc.h>
#include <intr.h>
#include <zjunix/slab.h>
#include <zjunix/syscall.h>
#include <zjunix/utils.h>

#pragma GCC push_options
#pragma GCC optimize("O0")


// 共享内存结构链表头
struct shared_page_struct shared_pages;


// 初始化虚拟内存地址机制
void init_vm() {
    // 注册读写虚拟地址的 TLB Refill 中断
    register_exception_handler(2, tlb_refill);
    register_exception_handler(3, tlb_refill);
    // 初始化共享页
    init_shared_page();
}


// 创建新的虚拟内存结构
void* vm_create() { return ptd_create(); }


// 删除虚拟内存结构
void vm_delete(task_struct* pcb) {
    tlb_delete(pcb->pid);
    ptd_delete(pcb, pcb->vm);
}


// 打印进程虚拟内存信息（页表）
void vm_print(void* vm) {
    void* ptd = vm;
    int i;
    for (i = 0; i < PAGE_SIZE / sizeof(void*); i++) {
        // 遍历页目录项
        void* pt = (void*)(((int*)ptd)[i]);
        if (pt != NULL) {
            // 遍历页表项
            int j;
            for (j = 0; j < PAGE_SIZE / sizeof(void*); j++) {
                void* pte = (void*)(((int*)pt)[j]);
                if (pte != NULL) {
                    // 页表项有效
                    void* virtual_addr = (void*)((i << 22) | (j << 12));
                    kernel_printf("virtual addr = %x, physical addr = %x\n",
                                  (unsigned int)virtual_addr,
                                  (unsigned int)pte);
                }
            }
        }
    }
}


// 设置当前活动ASID，以匹配TLB表项中的ASID
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


// 创建页目录
void* ptd_create() {
    void* ptd = kmalloc(PAGE_SIZE);
    kernel_memset(ptd, 0, PAGE_SIZE);
    return ptd;
}


// 删除页目录
void ptd_delete(task_struct* pcb, void* ptd) {
    int i;
    for (i = 0; i < PAGE_SIZE / sizeof(void*); i++) {
        void* pt = (void*)(((int*)ptd)[i]);
        if (pt != NULL) {
            // 若存在页目录项，则删除页表
            pt_delete(pcb, pt);
            ((int*)ptd)[i] = NULL;
        }
    }
    kfree(ptd);
}


// 创建页表
void* pt_create() {
    void* pt = kmalloc(PAGE_SIZE);
    kernel_memset(pt, 0, PAGE_SIZE);
    return pt;
}


// 删除页表
void pt_delete(task_struct* pcb, void* pt) {
    int i;
    for (i = 0; i < PAGE_SIZE / sizeof(void*); i++) {
        void* pte = (void*)(((int*)pt)[i]);
        if (pte != NULL) {
            // 若存在页表项
            // 先看看是不是共享页，若不是的话，再由此删除，
            // 若是则移交shared_page_delete处理
            if (shared_page_delete(pcb, pte) != 0) {
                kfree(pte);
            }
            ((int*)pt)[i] = NULL;
        }
    }
    kfree(pt);
}


// TLB Refill 处理程序
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
    // 获取当前访问的虚拟地址
    asm volatile("mfc0 %0, $8\n\t" : "=r"(virtual_addr));
    // 获取当前进程
    pcb = get_current_task();
    ptd = pcb->vm;
    if (ptd == NULL) {
        // 若当前进程的页目录为空，则报错
        kernel_printf(
            "[tlb_refill]: Error. Process %s exited due to accessing addr=%x, "
            "*addr=%x "
            "epc=%x\n",
            pcb->name, (unsigned int)virtual_addr,
            *((unsigned int*)virtual_addr), (unsigned int)pcb->context.epc);
        while (1)
            ;
    }
    // 根据虚拟地址，获取对应的页目录项
    ptd_index = get_ptd_index(virtual_addr);
    pt = (void*)((unsigned int*)ptd)[ptd_index];
    if (pt == NULL) {
        // 若该项页目录项不存在，则创建新页表
        pt = pt_create();
        ((unsigned int*)ptd)[ptd_index] = (unsigned int)pt;
    }
    // 在页表中，获取页表项下标
    pt_index = get_pt_index(virtual_addr);
    // 获取页表项
    pte = (void*)((unsigned int*)pt)[pt_index];
    if (pte == NULL) {
        // 若页表项为空，且 >= 0x10000 说明非法访问
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
        // 否则创建新空间，分配给该进程
        pte = kmalloc(PAGE_SIZE);
        kernel_memset(pte, 0, PAGE_SIZE);
        ((unsigned int*)pt)[pt_index] = (unsigned int)pte;
    }
    // 获取相邻的页表项下标
    pt_near_index = pt_index ^ 1;
    // 获取相邻的页表项
    pte_near = (void*)((unsigned int*)pt)[pt_near_index];
    if (pte_near == NULL) {
        // 若相邻页表项为空，则申请
        pte_near = kmalloc(PAGE_SIZE);
        kernel_memset(pte_near, 0, PAGE_SIZE);
        ((unsigned int*)pt)[pt_near_index] = (unsigned int)pte_near;
    }
    // 设置对应的TLB表项中的两个匹配项
    if ((pt_index & 1) == 0) {
        entry_lo0 = get_entry_lo(pte);
        entry_lo1 = get_entry_lo(pte_near);
    } else {
        entry_lo0 = get_entry_lo(pte_near);
        entry_lo1 = get_entry_lo(pte);
    }
    // 设置entry_hi
    entry_hi = get_entry_hi(virtual_addr);
    // 将该记录加载到TLB表中
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


// 删除某个进程的TLB表项
void tlb_delete(unsigned int asid) {
    int i;
    // 遍历32个TLB表项
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
            // 若该项不是当前进程的，不管
            continue;
        }
        // 若是当前进程的，则填充为0，相当于删除
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


// 根据虚拟地址，获得页目录项下标
unsigned int get_ptd_index(void* addr) { return ((unsigned int)addr >> 22); }


// 根据虚拟地址，获得页表项下标
unsigned int get_pt_index(void* addr) {
    return ((((unsigned int)addr) & 0x003FF000) >> 12);
}


// 根据PTE，生成对应的entry_lo
unsigned int get_entry_lo(void* pte) {
    void* physical_addr;
    unsigned int entry_lo;
    physical_addr = (void*)((unsigned int)pte - 0x80000000);
    entry_lo = ((unsigned int)physical_addr >> 12) << 6;
    entry_lo |= (3 << 3);
    entry_lo |= 0x06;
    return entry_lo;
}


// 根据虚拟地址，生成对应的entry_hi
unsigned int get_entry_hi(void* addr) {
    unsigned int asid;
    // 获取当前进程的ASID
    asm volatile(
        "mfc0 $t0, $10\n\t"
        "nop\n\t"
        "andi %0, $t0, 0xFF\n\t"
        : "=r"(asid));
    return (((unsigned int)addr & 0xFFFFE000) | asid);
}


// 初始化共享页链表
void init_shared_page() { INIT_LIST_HEAD(&shared_pages.list); }


// 创建共享页，返回虚拟地址
void* shared_page_create(task_struct* pcb, const char* name) {
    void* virtual_addr;
    struct shared_page_struct* shared_page;
    // 寻找可用的虚拟地址
    virtual_addr = (void*)0x10000;
    while (vma_va_to_pa(pcb, virtual_addr) != NULL) {
        virtual_addr = (void*)((unsigned int)virtual_addr + 0x1000);
    }
    // 判断该共享页名是否已经存在
    shared_page = find_shared_page_by_name(name);
    if (shared_page != NULL) {
        // 若存在，直接加引用计数
        shared_page->count++;
    } else {
        // 若不存在，创建新的记录
        shared_page = kmalloc(sizeof(struct shared_page_struct));
        kernel_strcpy(shared_page->name, name);
        shared_page->count = 1;
        shared_page->physical_addr = kmalloc(PAGE_SIZE);
        kernel_memset(shared_page->physical_addr, 0, PAGE_SIZE);
        list_add(&(shared_page->list), &(shared_pages.list));
    }
    // 设置虚拟地址映射关系
    vma_set_mapping(pcb, virtual_addr, shared_page->physical_addr);
    return virtual_addr;
}


// 删除共享页
int shared_page_delete(task_struct* pcb, void* virtual_addr) {
    void* physical_addr;
    struct shared_page_struct* shared_page;
    // 获取该页对应的物理地址
    physical_addr = vma_va_to_pa(pcb, virtual_addr);
    if (physical_addr == NULL) {
        return 1;
    }
    // 根据物理地址，查找对应的共享页信息
    shared_page = find_shared_page_by_physical_addr(physical_addr);
    if (shared_page == NULL) {
        // 若未找到，说明是非共享页，直接返回
        return 2;
    }
    // 解除映射关系
    vma_set_mapping(pcb, virtual_addr, NULL);
    // 清除TLB表
    tlb_delete(pcb->pid);
    // 计数器减1
    shared_page->count--;
    if (shared_page->count == 0) {
        // 若没有进程在使用该共享页了，则删除
        kfree(shared_page->physical_addr);
        list_del_init(&shared_page->list);
    }
    return 0;
}


// 根据名字遍历共享页链表，查找对应的共享页
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


// 根据物理地址遍历共享页链表，查找对应的共享页
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


// 根据虚拟地址，查页表获取对应的物理地址
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


// 设置虚拟地址到物理地址的映射关系，即修改页表项
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


// 创建进程的内存池，用于堆空间的分配
void memory_pool_create(task_struct* pcb) {
    int i;
    for (i = 0; i < 8; i++) {
        // 针对8种不同的大小
        // 预先申请对应的内存块
        // 并做初始化
        unsigned int chunk_size;
        pcb->user_pc_memory_blocks[i] = kmalloc(sizeof(memory_block_struct));
        kernel_memset(pcb->user_pc_memory_blocks[i], 0,
                      sizeof(memory_block_struct));
        chunk_size = 16 << i;
        init_memory_block(pcb, pcb->user_pc_memory_blocks[i], chunk_size);
    }
    // 第9项用于记录超出大小的堆空间的申请
    pcb->user_pc_memory_blocks[8] = NULL;
}


// 删除进程的内存池
void memory_pool_delete(task_struct* pcb) {
    int i;
    for (i = 0; i < 9; i++) {
        memory_block_struct* memory_block = pcb->user_pc_memory_blocks[i];
        // 逐项遍历内存池块
        while (memory_block) {
            memory_block_struct* next_memory_block;
            // 解除映射关系
            vma_set_mapping(pcb, memory_block->base_virtual_addr, NULL);
            next_memory_block = memory_block->next_memory_block;
            kfree(memory_block);
            memory_block = next_memory_block;
        }
    }
}


// 用户程序的malloc，用于堆空间申请内存
void* memory_alloc(task_struct* pcb, unsigned int size) {
    int i;
    for (i = 0; i < 8; i++) {
        // Best-Fit查找合适大小的内存块
        if (pcb->user_pc_memory_blocks[i]->chunk_size >= size) {
            break;
        }
    }
    if (i >= 8) {
        // 太大了，直接从Buddy系统分配
        return big_chunk_alloc(pcb, size);
    } else {
        // 有合适的内存块，则切一块出去
        return small_chunk_alloc(pcb, pcb->user_pc_memory_blocks[i]);
    }
}


// 用户程序的free，用于堆空间释放内存
int memory_free(task_struct* pcb, void* virtual_addr) {
    int i;
    for (i = 0; i < 8; i++) {
        memory_block_struct* memory_block = pcb->user_pc_memory_blocks[i];
        // 遍历内存池块
        while (memory_block) {
            void *start_addr, *end_addr;
            start_addr = memory_block->base_virtual_addr;
            end_addr = (void*)((unsigned int)start_addr + PAGE_SIZE);
            // 根据虚拟地址，找对应的区块
            if (virtual_addr >= start_addr && virtual_addr < end_addr) {
                void* physical_addr = vma_va_to_pa(pcb, virtual_addr);
                *(unsigned int*)physical_addr =
                    (unsigned int)pcb->user_pc_memory_blocks[i]->head;
                // 释放后的空间，加入空闲列表
                pcb->user_pc_memory_blocks[i]->head = physical_addr;
                return 0;
            }
            memory_block = memory_block->next_memory_block;
        }
    }
    return 1;
}


// 初始化内存池内存块
void init_memory_block(task_struct* pcb, memory_block_struct* memory_block,
                       unsigned int chunk_size) {
    void* virtual_addr;
    void* physical_addr;
    // 为该块分配对应的虚拟地址
    virtual_addr = (void*)0x10000;
    while (vma_va_to_pa(pcb, virtual_addr) != NULL) {
        virtual_addr = (void*)((unsigned int)virtual_addr + 0x1000);
    }
    // 为该块申请物理内存空间
    physical_addr = kmalloc(PAGE_SIZE);
    kernel_memset(physical_addr, 0, PAGE_SIZE);
    // 设置映射关系
    vma_set_mapping(pcb, virtual_addr, physical_addr);
    // 初始化块信息
    memory_block->base_virtual_addr = virtual_addr;
    memory_block->base_physical_addr = physical_addr;
    memory_block->chunk_size = chunk_size;
    memory_block->head = physical_addr;
    memory_block->next_memory_block = NULL;
    // 初始化空闲链表
    init_chunk_list(memory_block);
}


// 初始化内存池区块的空闲链表
void init_chunk_list(memory_block_struct* memory_block) {
    int i;
    unsigned int* p;
    i = 0;
    p = (unsigned int*)memory_block->head;
    while (1) {
        // 按照chunk_size，划分为逐个对象
        // 每个对象用指针链接
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


// 小片内存的用户内存申请
void* small_chunk_alloc(task_struct* pcb, memory_block_struct* memory_block) {
    void* physical_addr = memory_block->head;
    if (physical_addr != NULL) {
        // 空闲链表中还有剩余项可以分配
        // 直接分配
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
        // 更新空闲表头
        memory_block->head = (void*)(*(int*)physical_addr);
        return virtual_addr;
    } else {
        // 空闲链表中没有剩余项可以分配
        // 申请新的内存块，再继续分配
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


// 大片内存的用户内存申请
void* big_chunk_alloc(task_struct* pcb, unsigned int size) {
    void* virtual_addr;
    void* physical_addr;
    int num_of_pages;
    int i;
    size += PAGE_SIZE - 1;
    size &= ~(PAGE_SIZE - 1);
    num_of_pages = size / PAGE_SIZE;
    // 查找连续的可用的虚拟地址
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
    // 申请物理内存空间
    physical_addr = kmalloc(size);
    kernel_memset(physical_addr, 0, size);
    for (i = 0; i < num_of_pages; i++) {
        memory_block_struct* memory_block;
        memory_block_struct* last_memory_block;
        void* curr_virtual_addr =
            (void*)((unsigned int)virtual_addr + i * PAGE_SIZE);
        void* curr_physical_addr =
            (void*)((unsigned int)physical_addr + i * PAGE_SIZE);
        // 设置映射关系
        vma_set_mapping(pcb, curr_virtual_addr, curr_physical_addr);
        // 设置内存块信息
        memory_block = kmalloc(sizeof(memory_block_struct));
        memory_block->base_virtual_addr = curr_virtual_addr;
        memory_block->base_physical_addr = curr_physical_addr;
        memory_block->chunk_size = 0;
        memory_block->head = NULL;
        memory_block->next_memory_block = NULL;
        // 加入到PCB内存块链表
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
    return virtual_addr;
}


// 虚拟内存地址测试程序
void vma_proc() {
    int i;
    // 访问0, 4092, 4096, ... 等虚拟地址
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
    // 测试对虚拟地址的写入与读写是否正确
    kernel_printf("i=%d\n", i);
    // 另一项对于虚拟地址的读写的测试
    for (i = 0; i < 10; i++) {
        int val;
        int* p = (int*)((i * 4) + 0x8000);
        *p = 12345678 + i;
        val = *p;
        kernel_printf("addr: %x, write: %d, read: %d\n", p, 12345678 + i, val);
    }
    // 打印进程页表
    vm_print(get_current_task()->vm);
    // 退出进程
    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}


// 共享内存页的测试程序1
void page_share_proc_1() {
    unsigned int* shared_page;
    unsigned int* shared_page_physical_addr;
    int i;
    // 创建共享内存页
    const char* name = "share_page";
    asm volatile(
        "move $a0, %1\n\t"
        "li $v0, 50\n\t"
        "syscall\n\t"
        "move %0, $v0"
        : "=r"(shared_page)
        : "r"(name));
    // 获取其物理地址
    shared_page_physical_addr = vma_va_to_pa(get_current_task(), shared_page);
    // 打印信息
    kernel_printf("[page_share_proc_1]shared_page va: %x, pa: %x\n",
                  shared_page, shared_page_physical_addr);
    sleep(1000 * 1000 * 30);
    // 读取共享内存页
    for (i = 0; i < 10; i++) {
        int val;
        val = shared_page[i];
        kernel_printf(
            "[page_share_proc_1]read shared memory by virtual addr, index: "
            "%d, "
            "val: %d\n",
            i, val);
    }
    // 释放共享内存页
    asm volatile(
        "move $a0, %0\n\t"
        "li $v0, 51\n\t"
        "syscall\n\t"
        :
        : "r"(shared_page));
    // 退出进程
    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}


// 共享内存页的测试程序2
void page_share_proc_2() {
    unsigned int* shared_page;
    unsigned int* shared_page_physical_addr;
    int i;
    // 创建共享内存页
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
    // 获取其物理地址
    shared_page_physical_addr = vma_va_to_pa(get_current_task(), shared_page);
    // 打印信息
    kernel_printf("[page_share_proc_2]shared_page va: %x, pa: %x\n",
                  shared_page, shared_page_physical_addr);
    // 写入共享内存页
    for (i = 0; i < 10; i++) {
        shared_page[i] = i;
        kernel_printf(
            "[page_share_proc_2]write shared memory by virtual addr, "
            "index: "
            "%d, "
            "val: %d\n",
            i, i);
    }
    // 释放共享内存页
    asm volatile(
        "move $a0, %0\n\t"
        "li $v0, 51\n\t"
        "syscall\n\t"
        :
        : "r"(shared_page));
    // 退出进程
    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}


// 用户程序内存申请测试程序的打印子程序
// 打印出当前的内存池信息
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


// 用户程序内存申请测试程序
void buffer_proc() {
    unsigned int* buffer[17];
    unsigned int* buffer_physical_addr[17];
    int i;
    // 打印内存池信息
    buffer_proc_print();  // 8
    // 循环申请512大小的内存
    for (i = 0; i < 17; i++) {
        asm volatile(
            "li $a0, 512\n\t"
            "li $v0, 60\n\t"
            "syscall\n\t"
            "move %0, $v0"
            : "=r"(buffer[i]));
        // 打印地址
        buffer_physical_addr[i] = vma_va_to_pa(get_current_task(), buffer[i]);
        kernel_printf("[buffer_proc]index: %d, va: %x, pa: %x\n", i, buffer[i],
                      buffer_physical_addr[i]);
        sleep(1000 * 1000 * 1);
        if (i == 15) {
            // 打印内存池信息
            buffer_proc_print();  // 0
        }
    }
    // 打印内存池信息
    buffer_proc_print();  // 7
    // 释放内存
    for (i = 0; i < 17; i++) {
        asm volatile(
            "move $a0, %0\n\t"
            "li $v0, 61\n\t"
            "syscall\n\t"
            :
            : "r"(buffer[i]));
    }
    buffer_proc_print();  // 24
    // 退出进程
    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}

#pragma GCC pop_options