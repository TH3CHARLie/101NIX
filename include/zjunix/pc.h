// sched.h
// definitions of process schedule related data structures, constants and marcos

#ifndef _101NIX_SCHED_H_
#define _101NIX_SCHED_H_
#include <arch.h>
#include <driver/vga.h>
#include <zjunix/cfs.h>
#include <zjunix/pid.h>

typedef struct _memory_block_struct memory_block_struct;

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

typedef struct task_struct {
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
  struct list_head state_node;
  int user_mode;
  void *vm;
  memory_block_struct *user_pc_memory_blocks[9];
} task_struct;

typedef union task_union {
  struct task_struct task;
  unsigned char kernel_stack[TASK_KERNEL_SIZE];
} task_union;

void init_task_module();

void task_create(char *task_name, void (*entry)(unsigned int argc, void *args),
                 unsigned int argc, void *args, int nice, int user_mode);

void task_tick(unsigned int status, unsigned int cause, context *pt_context);

void task_schedule(unsigned int status, unsigned int cause,
                   context *pt_context);
void task_exit_syscall(unsigned int status, unsigned int cause,
                       context *pc_context);

void task_kill(pid_t pid);

void task_wait(pid_t pid);

void task_wakeup(pid_t pid);

struct task_struct *get_current_task();

int print_proc();

#endif  // _101NIX_SCHED_H_