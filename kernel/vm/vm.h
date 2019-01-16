#ifndef _VM_H
#define _VM_H

#include <zjunix/list.h>
#include <zjunix/pc.h>
#include <zjunix/type.h>
#include <zjunix/utils.h>


// 页面大小
#define PAGE_SIZE 4096


// 共享页结构
struct shared_page_struct {
    char name[256];
    int count;
    void* physical_addr;
    struct list_head list;
};


// 内存池内存块信息
typedef struct _memory_block_struct memory_block_struct;
struct _memory_block_struct {
    void* base_virtual_addr;
    void* base_physical_addr;
    unsigned int chunk_size;
    void* head;
    memory_block_struct* next_memory_block;
};


// 初始化虚拟内存地址机制
void init_vm();


// 创建新的虚拟内存结构
void* vm_create();


// 删除虚拟内存结构
void vm_delete(task_struct* pcb);


// 打印进程虚拟内存信息（页表）
void vm_print(void* vm);


// 设置当前活动ASID，以匹配TLB表项中的ASID
void set_active_asid(unsigned int asid);


// 创建页目录
void* ptd_create();


// 删除页目录
void ptd_delete(task_struct* pcb, void* ptd);


// 创建页表
void* pt_create();


// 删除页表
void pt_delete(task_struct* pcb, void* pt);


// TLB Refill 处理程序
void tlb_refill(unsigned int status, unsigned int cause, context* pt_context);


// 删除某个进程的TLB表项
void tlb_delete(unsigned int asid);


// 根据虚拟地址，获得页目录项下标
unsigned int get_ptd_index(void* addr);


// 根据虚拟地址，获得页表项下标
unsigned int get_pt_index(void* addr);


// 根据PTE，生成对应的entry_lo
unsigned int get_entry_lo(void* pte);


// 根据虚拟地址，生成对应的entry_hi
unsigned int get_entry_hi(void* addr);


// 初始化共享页链表
void init_shared_page();


// 创建共享页，返回虚拟地址
void* shared_page_create(task_struct* pcb, const char* name);


// 删除共享页
int shared_page_delete(task_struct* pcb, void* virtual_addr);


// 根据名字遍历共享页链表，查找对应的共享页
struct shared_page_struct* find_shared_page_by_name(const char* name);


// 根据物理地址遍历共享页链表，查找对应的共享页
struct shared_page_struct* find_shared_page_by_physical_addr(
    void* physical_addr);


// 根据虚拟地址，查页表获取对应的物理地址
void* vma_va_to_pa(task_struct* pcb, void* virtual_addr);


// 设置虚拟地址到物理地址的映射关系，即修改页表项
void vma_set_mapping(task_struct* pcb, void* virtual_addr, void* physical_addr);


// 创建进程的内存池，用于堆空间的分配
void memory_pool_create(task_struct* pcb);


// 删除进程的内存池
void memory_pool_delete(task_struct* pcb);


// 用户程序的malloc，用于堆空间申请内存
void* memory_alloc(task_struct* pcb, unsigned int size);


// 用户程序的free，用于堆空间释放内存
int memory_free(task_struct* pcb, void* virtual_addr);


// 初始化内存池内存块
void init_memory_block(task_struct* pcb, memory_block_struct* memory_block,
                       unsigned int chunk_size);


// 初始化内存池区块的空闲链表
void init_chunk_list(memory_block_struct* memory_block);


// 小片内存的用户内存申请
void* small_chunk_alloc(task_struct* pcb, memory_block_struct* memory_block);


// 大片内存的用户内存申请
void* big_chunk_alloc(task_struct* pcb, unsigned int size);


// 虚拟内存地址测试程序
void vma_proc();


// 共享内存页的测试程序1
void page_share_proc_1();


// 共享内存页的测试程序2
void page_share_proc_2();


// 用户程序内存申请测试程序
void buffer_proc();




#endif
