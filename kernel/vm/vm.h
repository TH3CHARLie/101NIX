#ifndef _VM_H
#define _VM_H

#include <zjunix/list.h>
#include <zjunix/pc.h>
#include <zjunix/type.h>
#include <zjunix/utils.h>

#define PAGE_SIZE 4096

struct shared_page_struct {
    char name[256];
    int count;
    void* physical_addr;
    struct list_head list;
};

typedef struct _memory_block_struct memory_block_struct;
struct _memory_block_struct {
    void* base_virtual_addr;
    void* base_physical_addr;
    unsigned int chunk_size;
    void* head;
    memory_block_struct* next_memory_block;
};

void init_vm();
void* vm_create();
void vm_delete(task_struct* pcb);
void vm_print(void* vm);
void set_active_asid(unsigned int asid);
void* ptd_create();
void ptd_delete(task_struct* pcb, void* ptd);
void* pt_create();
void pt_delete(task_struct* pcb, void* pt);
void tlb_refill(unsigned int status, unsigned int cause, context* pt_context);
void tlb_delete(unsigned int asid);
unsigned int get_ptd_index(void* addr);
unsigned int get_pt_index(void* addr);
unsigned int get_entry_lo(void* pte);
unsigned int get_entry_hi(void* addr);
void init_shared_page();
void* shared_page_create(task_struct* pcb, const char* name);
int shared_page_delete(task_struct* pcb, void* virtual_addr);
struct shared_page_struct* find_shared_page_by_name(const char* name);
struct shared_page_struct* find_shared_page_by_physical_addr(
    void* physical_addr);
void* vma_va_to_pa(task_struct* pcb, void* virtual_addr);
void vma_set_mapping(task_struct* pcb, void* virtual_addr, void* physical_addr);
void memory_pool_create(task_struct* pcb);
void memory_pool_delete(task_struct* pcb);
void* memory_alloc(task_struct* pcb, unsigned int size);
int memory_free(task_struct* pcb, void* virtual_addr);
void init_memory_block(task_struct* pcb, memory_block_struct* memory_block,
                       unsigned int chunk_size);
void init_chunk_list(memory_block_struct* memory_block);
void* small_chunk_alloc(task_struct* pcb, memory_block_struct* memory_block);
void* big_chunk_alloc(task_struct* pcb, unsigned int size);
void vma_proc();
void page_share_proc_1();
void page_share_proc_2();
void buffer_proc();

#endif
