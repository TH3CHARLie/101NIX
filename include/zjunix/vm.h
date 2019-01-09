#ifndef _ZJUNIX_VM_H
#define _ZJUNIX_VM_H

#include <zjunix/pc.h>

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
void* shared_page_create(task_struct* pcb, const char* name);
int shared_page_delete(task_struct* pcb, void* virtual_addr);
void memory_pool_create(task_struct* pcb);
void memory_pool_delete(task_struct* pcb);
void* memory_alloc(task_struct* pcb, unsigned int size);
int memory_free(task_struct* pcb, void* virtual_addr);
void vma_proc();
void page_share_proc_1();
void page_share_proc_2();
void buffer_proc();

#endif
