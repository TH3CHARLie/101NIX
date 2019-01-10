#ifndef _ZJUNIX_SEMAPHORE_H
#define _ZJUNIX_SEMAPHORE_H

#include <zjunix/pc.h>
#include <zjunix/type.h>
#include <zjunix/utils.h>

typedef struct {
    pid_t pid;
    struct list_head list;
} semaphore_waiting_process_queue_struct;

typedef struct {
    char name[256];
    int count;
    semaphore_waiting_process_queue_struct* wait;
    struct list_head list;
} semaphore_struct;

void semaphore_init();
int semaphore_create(void* name, int count);
void semaphore_delete(void* name);
void semaphore_wait(void* name);
void semaphore_signal(void* name);
semaphore_struct* semaphore_get(void* name);
void customer_proc();
void producer_proc();

#endif
