// #ifndef _ZJUNIX_PC_H
// #define _ZJUNIX_PC_H

// typedef struct {
//     unsigned int epc;
//     unsigned int at;
//     unsigned int v0, v1;
//     unsigned int a0, a1, a2, a3;
//     unsigned int t0, t1, t2, t3, t4, t5, t6, t7;
//     unsigned int s0, s1, s2, s3, s4, s5, s6, s7;
//     unsigned int t8, t9;
//     unsigned int hi, lo;
//     unsigned int gp;
//     unsigned int sp;
//     unsigned int fp;
//     unsigned int ra;
// } context;

// typedef struct {
//     context context;
//     int ASID;
//     unsigned int counter;
//     char name[32];
//     unsigned long start_time;
// } task_struct;

// typedef union {
//     task_struct task;
//     unsigned char kernel_stack[4096];
// } task_union;

// #define PROC_DEFAULT_TIMESLOTS 6

// void init_pc();

// #endif  // !_ZJUNIX_PC_H

// sched.h
// definitions of process schedule related data structures, constants and marcos

#ifndef _101NIX_SCHED_H_
#define _101NIX_SCHED_H_
#include <arch.h>
#include <zjunix/cfs.h>
#include <zjunix/pid.h>
#include <driver/vga.h>
// define the size of each PCB to be 4096
// for memory alignment
#define TASK_KERNEL_SIZE 4096

// struct reg_context is the content of MIPS registers
// extensively used when loading the entry point and args of function
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
typedef reg_context_t context;
// task state enumrations
enum {
  TASK_RUNNING,
  TASK_WAITING,
  TASK_READY,
  TASK_DEAD,
};

struct task_struct {
  // state, ie: TASK_RUNNING
  int state;

  // registers context
  reg_context_t context;

  // global pid and tgid value
  // pid: process identifier
  // tgid: thread group identifier
  // notice due to the existence of namespace
  // actually pid value varies from namespaces
  // use APIs in pid.h to fetch the corrsponding one
  pid_t pid;
  pid_t tgid;

  // pids[PIDTYPE_PID](pid[0]) is the link to PID
  // pids[PIDTYPE_PGID](pid[1]) is the link to PGID
  struct pid_link pids[PIDTYPE_MAX];

  // process relations:

  // parent points the parent process of this process
  // often caused by fork
  struct task_struct *parent;

  // group leader points to the leader of thread group
  // by default, group leader points to task_struct itself
  struct task_struct *group_leader;

  // list of children process
  struct list_head children;

  // cfs schedule:

  // nice value, can be reassigned using syscall
  int nice;
  // static priority, will not change during ex
  int static_prio;
  int prio;

  struct sched_entity se;
  char name[32];

  struct list_head task_node;
  struct list_head running_node;
};

union task_union {
  struct task_struct task;
  unsigned char kernel_stack[TASK_KERNEL_SIZE];
};

void init_task_module();

void task_create(char *task_name, void (*entry)(unsigned int argc, void *args),
                 unsigned int argc, void *args, pid_t *retpid, int nice);

void task_schedule(unsigned int status, unsigned int cause,
                   context *pt_context);

struct task_struct *get_current_task();

void pc_schedule(unsigned int status, unsigned int cause, context *pt_context);
int pc_peek();
void pc_create(int asid, void (*func)(), unsigned int init_sp,
               unsigned int init_gp, char *name);
void pc_kill_syscall(unsigned int status, unsigned int cause,
                     context *pt_context);
int pc_kill(int proc);
int print_proc();
#endif  // _101NIX_SCHED_H_