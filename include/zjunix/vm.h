#ifndef _ZJUNIX_VM_H
#define _ZJUNIX_VM_H

#include <zjunix/pc.h>

// 用户程序内存池区块结构
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


// 创建共享页，返回虚拟地址
void* shared_page_create(task_struct* pcb, const char* name);


// 删除共享页
int shared_page_delete(task_struct* pcb, void* virtual_addr);


// 创建进程的内存池，用于堆空间的分配
void memory_pool_create(task_struct* pcb);


// 删除进程的内存池
void memory_pool_delete(task_struct* pcb);


// 用户程序的free，用于堆空间释放内存
void* memory_alloc(task_struct* pcb, unsigned int size);


// 用户程序的free，用于堆空间释放内存
int memory_free(task_struct* pcb, void* virtual_addr);


// 虚拟内存地址测试程序
void vma_proc();


// 共享内存页的测试程序1
void page_share_proc_1();


// 共享内存页的测试程序2
void page_share_proc_2();


// 用户程序内存申请测试程序
void buffer_proc();


// 根据虚拟地址，查页表获取对应的物理地址
void* vma_va_to_pa(task_struct* pcb, void* virtual_addr);


// 设置虚拟地址到物理地址的映射关系，即修改页表项
void vma_set_mapping(task_struct* pcb, void* virtual_addr, void* physical_addr);


#endif
