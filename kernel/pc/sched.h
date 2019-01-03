#ifndef _101NIX_SCHED_H_
#define _101NIX_SCHED_H_
#include <pid.h>


#define TASK_KERNEL_SIZE 4096

typedef struct reg_context {
    unsigned int epc;
    unsigned int at;
    unsigned int v0, v1;
    unsigned int a0, a1, a2, a3;
    unsigned int t0, t1, t2, t3, t4, t5, t6, t7;
    unsigned int s0, s1, s2, s3, s4, s5, s6, s7;
    unsigned int t8, t9;
    unsigned int hi, lo;
    unsigned int gp;
    unsigned int sp;
    unsigned int fp;
    unsigned int ra;
} reg_context_t;

struct load_weight{
	unsigned long weight;
    unsigned long inv_weight;
};

struct sched_entity {
    struct rb_node rb_node;
    unsigned long vruntime;
    struct load_weight load;
	unsigned long exec_start_time;
	unsigned long sum_exec_runtime;

};

struct task_struct {
    // state, ie: running
    int state;
    // registers context
    reg_context_t context;
    // int for hashing
    pid_t pid;
    pid_t tpid;
    struct pid_link pids[PIDTYPE_MAX];
    // relations
    struct task_struct* parent;
    struct task_struct* group_leader;
    struct list_node* children;

    int nice;
    int static_prio;
    int prio;
    char name[32];
};

union task_union {
    struct task_struct task;
    unsigned char kernel_stack[TASK_KERNEL_SIZE];
};


void init_process_module();


#endif // _101NIX_SCHED_H_