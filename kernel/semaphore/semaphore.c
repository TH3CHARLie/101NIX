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


// 信号量结构链表
static semaphore_struct semaphores;


// 初始化信号量结构链表
void semaphore_init() { INIT_LIST_HEAD(&semaphores.list); }


// 创建新的信号量
int semaphore_create(void* name, int count) {
    semaphore_struct* semaphore = semaphore_get(name);
    if (semaphore != NULL) {
        // 若已存在，则创建失败
        return 1;
    }
    // 否则创建一个信号量的结构体，并加入链表
    semaphore = kmalloc(sizeof(semaphore_struct));
    kernel_strcpy(semaphore->name, name);
    semaphore->count = count;
    semaphore->wait = kmalloc(sizeof(semaphore_waiting_process_queue_struct));
    INIT_LIST_HEAD(&semaphore->wait->list);
    list_add(&(semaphore->list), &(semaphores.list));
    return 0;
}


// 删除信号量
void semaphore_delete(void* name) {
    semaphore_struct* semaphore = semaphore_get(name);
    if (semaphore == NULL) {
        // 若不存在，则返回
        return;
    }
    // 存在，则从链表中删除
    list_del_init(&semaphore->list);
}


// 信号量wait操作
void semaphore_wait(void* name) {
    unsigned int old_ie;
    semaphore_struct* semaphore = semaphore_get(name);
    if (semaphore == NULL) {
        // 不存在，直接返回
        return;
    }
    // 关中断
    old_ie = disable_interrupts();
    // 计数器减1
    semaphore->count--;
    if (semaphore->count < 0) {
        // 小于0了，则加入等待队列，并挂起进程
        semaphore_waiting_process_queue_struct* wait =
            kmalloc(sizeof(semaphore_waiting_process_queue_struct));
        wait->pid = get_current_task()->pid;
        list_add_tail(&(wait->list), &(semaphore->wait->list));
        kernel_printf("[semaphore_wait]process id:%d blocked\n", wait->pid);
        // 开中断
        enable_interrupts();
        task_wait(wait->pid);
    } else if (old_ie) {
        // 开中断
        enable_interrupts();
    }
}


// 信号量signal操作
void semaphore_signal(void* name) {
    unsigned int old_ie;
    semaphore_struct* semaphore = semaphore_get(name);
    if (semaphore == NULL) {
        // 不存在，直接返回
        return;
    }
    // 关中断
    old_ie = disable_interrupts();
    // 计数器加1
    semaphore->count++;
    if (semaphore->count <= 0) {
        // 小于等于0了，则需要唤醒在等待队列中的进程
        semaphore_waiting_process_queue_struct* wait =
            list_first_entry(&semaphore->wait->list,
                             semaphore_waiting_process_queue_struct, list);
        task_wakeup(wait->pid);
        // 从队列中删除
        list_del_init(&wait->list);
        kernel_printf("[semaphore_signal]process id:%d wakeup\n", wait->pid);
    }
    if (old_ie) {
        // 开中断
        enable_interrupts();
    }
}


// 获取信号量
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


// 创建信号量的系统调用
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


// 删除信号量的系统调用
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


// wait信号量的系统调用
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


// signal信号量的系统调用
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


// 消费者进程
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
    // 创建共享内存页
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
    // 初始化mutex, full, empty
    create_syscall(semaphore_mutex_name, mutex);
    create_syscall(semaphore_full_name, full);
    create_syscall(semaphore_empty_name, empty);
    kernel_printf("[customer_proc]init, mutex:%d, empty:%d, full:%d\n", mutex,
                  empty, full);
    // 循环获取product
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
    // 删除信号量
    delete_syscall(semaphore_mutex_name);
    delete_syscall(semaphore_full_name);
    delete_syscall(semaphore_empty_name);
    // 删除共享页
    asm volatile(
        "move $a0, %0\n\t"
        "li $v0, 51\n\t"
        "syscall\n\t"
        :
        : "r"(shared_page_addr));
    // 退出进程
    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}


// 生产者进程
void producer_proc() {
    int* shared_page_addr;
    char* shared_page_name = "cp_shared_page";
    int buffer_size = 5;
    char* semaphore_mutex_name = "mutex";
    char* semaphore_full_name = "full";
    char* semaphore_empty_name = "empty";
    int pos = 0;
    int product = 0;
    // 创建共享内存页
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
    // 循环生成product
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
    // 删除共享页
    asm volatile(
        "move $a0, %0\n\t"
        "li $v0, 51\n\t"
        "syscall\n\t"
        :
        : "r"(shared_page_addr));
    // 退出进程
    asm volatile(
        "li $v0, 16\n\t"
        "syscall\n\t");
}

#pragma GCC pop_options