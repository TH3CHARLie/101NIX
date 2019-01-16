#ifndef _ZJUNIX_SEMAPHORE_H
#define _ZJUNIX_SEMAPHORE_H

#include <zjunix/pc.h>
#include <zjunix/type.h>
#include <zjunix/utils.h>


// 信号量等待队列结构
typedef struct {
    pid_t pid;
    struct list_head list;
} semaphore_waiting_process_queue_struct;


// 信号量结构
typedef struct {
    char name[256];
    int count;
    semaphore_waiting_process_queue_struct* wait;
    struct list_head list;
} semaphore_struct;


// 初始化信号量
void semaphore_init();


// 创建信号量
int semaphore_create(void* name, int count);

// 删除信号量
void semaphore_delete(void* name);


// wait信号量
void semaphore_wait(void* name);


// signal信号量
void semaphore_signal(void* name);


// 获取信号量
semaphore_struct* semaphore_get(void* name);


// 消费者
void customer_proc();


// 生产者
void producer_proc();

#endif
