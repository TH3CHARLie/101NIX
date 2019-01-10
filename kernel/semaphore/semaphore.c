#include <driver/vga.h>
#include <exc.h>
#include <intr.h>
#include <zjunix/pc.h>
#include <zjunix/semaphore.h>
#include <zjunix/slab.h>
#include <zjunix/syscall.h>
#include <zjunix/vm.h>

#pragma GCC push_options
#pragma GCC optimize("O0")

extern struct cfs_rq cfs_rq;
static semaphore_struct semaphores;

void semaphore_init() { INIT_LIST_HEAD(&semaphores.list); }

int semaphore_create(void* name, int count) {
    semaphore_struct* semaphore = semaphore_get(name);
    if (semaphore != NULL) {
        return 1;
    }
    semaphore = kmalloc(sizeof(semaphore_struct));
    kernel_strcpy(semaphore->name, name);
    semaphore->count = count;
    semaphore->wait = kmalloc(sizeof(semaphore_waiting_process_queue_struct));
    INIT_LIST_HEAD(&semaphore->wait->list);
    list_add(&(semaphore->list), &(semaphores.list));
    // kernel_printf("[debug][semaphore_create]name:%s, count:%d\n", name,
    // count);
    return 0;
}

void semaphore_delete(void* name) {
    semaphore_struct* semaphore = semaphore_get(name);
    if (semaphore == NULL) {
        return;
    }
    list_del_init(&semaphore->list);
    // kernel_printf("[debug][semaphore_delete]name:%s\n", name);
}

void semaphore_wait(void* name) {
    unsigned int old_ie;
    semaphore_struct* semaphore = semaphore_get(name);
    if (semaphore == NULL) {
        return;
    }
    old_ie = disable_interrupts();
    semaphore->count--;
    // kernel_printf("[debug][semaphore_wait]name:%s, new_count:%d\n", name,
    //               semaphore->count);
    if (semaphore->count < 0) {
        semaphore_waiting_process_queue_struct* wait =
            kmalloc(sizeof(semaphore_waiting_process_queue_struct));
        wait->pid = get_current_task()->pid;
        list_add_tail(&(wait->list), &(semaphore->wait->list));
        kernel_printf("[semaphore_wait]process id:%d blocked\n", wait->pid);
        enable_interrupts();
        task_wait(wait->pid);
    } else if (old_ie) {
        enable_interrupts();
    }
}

void semaphore_signal(void* name) {
    unsigned int old_ie;
    semaphore_struct* semaphore = semaphore_get(name);
    if (semaphore == NULL) {
        return;
    }
    old_ie = disable_interrupts();
    semaphore->count++;
    // kernel_printf("[debug][semaphore_signal]name:%s, new_count:%d\n", name,
    //               semaphore->count);
    if (semaphore->count <= 0) {
        semaphore_waiting_process_queue_struct* wait =
            list_first_entry(&semaphore->wait->list,
                             semaphore_waiting_process_queue_struct, list);
        task_wakeup(wait->pid);
        list_del_init(&wait->list);
        kernel_printf("[semaphore_signal]process id:%d wakeup\n", wait->pid);
    }
    if (old_ie) {
        enable_interrupts();
    }
}

semaphore_struct* semaphore_get(void* name) {
    semaphore_struct* semaphore;
    struct list_head* pos;
    list_for_each(pos, &semaphores.list) {
        semaphore = list_entry(pos, semaphore_struct, list);
        if (kernel_strcmp(semaphore->name, name) == 0) {
            return semaphore;
        }
    }
    return NULL;
}

void create_syscall(char* name, int count) {
    asm volatile(
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "move $a0, %0\n\t"
        "move $a1, %1\n\t"
        "li $v0, 70\n\t"
        "syscall\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        :
        : "r"(name), "r"(count));
}

void delete_syscall(char* name) {
    asm volatile(
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "move $a0, %0\n\t"
        "li $v0, 71\n\t"
        "syscall\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        :
        : "r"(name));
}

void wait_syscall(char* name) {
    asm volatile(
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "move $a0, %0\n\t"
        "li $v0, 72\n\t"
        "syscall\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        :
        : "r"(name));
}

void signal_syscall(char* name) {
    asm volatile(
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        "move $a0, %0\n\t"
        "li $v0, 73\n\t"
        "syscall\n\t"
        "nop\n\t"
        "nop\n\t"
        "nop\n\t"
        :
        : "r"(name));
}

void customer_proc() {
    int* shared_page_addr;
    char* shared_page_name = "cp_shared_page";
    int buffer_size = 5;
    int mutex = 1;
    int full = 0;
    int empty = buffer_size;
    char* semaphore_mutex_name = "mutex";
    char* semaphore_full_name = "full";
    char* semaphore_empty_name = "empty";
    int pos = 0;
    asm volatile(
        "move $a0, %1\n\t"
        "li $v0, 50\n\t"
        "syscall\n\t"
        "move %0, $v0"
        : "=r"(shared_page_addr)
        : "r"(shared_page_name));
    kernel_printf("[producer_proc]shared_page va: %x, pa: %x\n",
                  shared_page_addr,
                  vma_va_to_pa(get_current_task(), shared_page_addr));
    create_syscall(semaphore_mutex_name, mutex);
    create_syscall(semaphore_full_name, full);
    create_syscall(semaphore_empty_name, empty);
    kernel_printf("[customer_proc]init, mutex:%d, empty:%d, full:%d\n", mutex,
                  empty, full);
    while (1) {
        int product;
        wait_syscall(semaphore_full_name);
        wait_syscall(semaphore_mutex_name);
        product = shared_page_addr[pos];
        kernel_printf("[customer_proc]receive product:%d\n", product);
        if (product == 100) {
            break;
        }
        pos = (pos + 1) % buffer_size;
        signal_syscall(semaphore_mutex_name);
        signal_syscall(semaphore_empty_name);
    }
    sleep(1000 * 1000 * 10);
    delete_syscall(semaphore_mutex_name);
    delete_syscall(semaphore_full_name);
    delete_syscall(semaphore_empty_name);
    asm volatile(
        "move $a0, %0\n\t"
        "li $v0, 51\n\t"
        "syscall\n\t"
        :
        : "r"(shared_page_addr));
    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}

void producer_proc() {
    int* shared_page_addr;
    char* shared_page_name = "cp_shared_page";
    int buffer_size = 5;
    char* semaphore_mutex_name = "mutex";
    char* semaphore_full_name = "full";
    char* semaphore_empty_name = "empty";
    int pos = 0;
    int product = 0;
    asm volatile(
        "move $a0, %1\n\t"
        "li $v0, 50\n\t"
        "syscall\n\t"
        "move %0, $v0"
        : "=r"(shared_page_addr)
        : "r"(shared_page_name));
    kernel_printf("[producer_proc]shared_page va: %x, pa: %x\n",
                  shared_page_addr,
                  vma_va_to_pa(get_current_task(), shared_page_addr));
    while (1) {
        wait_syscall(semaphore_empty_name);
        wait_syscall(semaphore_mutex_name);
        shared_page_addr[pos] = product;
        kernel_printf("[producer_proc]send product:%d\n", product);
        product++;
        pos = (pos + 1) % buffer_size;
        signal_syscall(semaphore_mutex_name);
        signal_syscall(semaphore_full_name);
        if (product > 100) {
            break;
        }
    }
    asm volatile(
        "move $a0, %0\n\t"
        "li $v0, 51\n\t"
        "syscall\n\t"
        :
        : "r"(shared_page_addr));
    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}

#pragma GCC pop_options