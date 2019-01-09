#include "syscall.h"
#include <arch.h>
#include <driver/vga.h>
#include <exc.h>
#include <intr.h>
#include <zjunix/semaphore.h>
#include <zjunix/syscall.h>
#include <zjunix/vm.h>

sys_fn syscalls[256];

void init_syscall() {
    register_exception_handler(8, syscall);

    // register all syscalls here
    register_syscall(4, syscall4);

    // page share
    register_syscall(50, syscall50);
    register_syscall(51, syscall51);

    // malloc & free
    register_syscall(60, syscall60);
    register_syscall(61, syscall61);

    // semaphore create / wait / signal
    register_syscall(70, syscall70);
    register_syscall(71, syscall71);
    register_syscall(72, syscall72);
    register_syscall(73, syscall73);

    // may need refactor into below format
    // task & schedule
    register_syscall(15, task_schedule);
    register_syscall(16, task_exit_syscall);
}

void syscall(unsigned int status, unsigned int cause, context* pt_context) {
    unsigned int code;
    code = pt_context->v0;
    pt_context->epc += 4;
    if (syscalls[code]) {
        unsigned int old_ie;
        old_ie = disable_interrupts();
        syscalls[code](status, cause, pt_context);
        if (old_ie) {
            enable_interrupts();
        }
    }
}

void register_syscall(int index, sys_fn fn) {
    index &= 255;
    syscalls[index] = fn;
}

void syscall4(unsigned int status, unsigned int cause, context* pt_context) {
    kernel_puts((unsigned char*)pt_context->a0, 0xfff, 0);
}

void syscall50(unsigned int status, unsigned int cause, context* pt_context) {
    void* virtual_addr =
        shared_page_create(get_current_task(), (unsigned char*)pt_context->a0);
    pt_context->v0 = (unsigned int)virtual_addr;
}

void syscall51(unsigned int status, unsigned int cause, context* pt_context) {
    int ret = shared_page_delete(get_current_task(), (void*)pt_context->a0);
    pt_context->v0 = ret;
}

void syscall60(unsigned int status, unsigned int cause, context* pt_context) {
    void* virtual_addr = memory_alloc(get_current_task(), pt_context->a0);
    pt_context->v0 = (unsigned int)virtual_addr;
}

void syscall61(unsigned int status, unsigned int cause, context* pt_context) {
    int ret = memory_free(get_current_task(), (void*)pt_context->a0);
    pt_context->v0 = ret;
}

void syscall70(unsigned int status, unsigned int cause, context* pt_context) {
    char* name = (char*)pt_context->a0;
    int count = pt_context->a1;
    int ret = semaphore_create(name, count);
    pt_context->v0 = ret;
}

void syscall71(unsigned int status, unsigned int cause, context* pt_context) {
    char* name = (char*)pt_context->a0;
    semaphore_delete(name);
}

void syscall72(unsigned int status, unsigned int cause, context* pt_context) {
    char* name = (char*)pt_context->a0;
    semaphore_wait(name);
}

void syscall73(unsigned int status, unsigned int cause, context* pt_context) {
    char* name = (char*)pt_context->a0;
    semaphore_signal(name);
}
