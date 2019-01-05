// #include "pc.h"

// #include <driver/vga.h>
// #include <intr.h>
// #include <zjunix/syscall.h>
// #include <zjunix/utils.h>

// task_struct pcb[8];
// int curr_proc;

// static void copy_context(context* src, context* dest) {
//     dest->epc = src->epc;
//     dest->at = src->at;
//     dest->v0 = src->v0;
//     dest->v1 = src->v1;
//     dest->a0 = src->a0;
//     dest->a1 = src->a1;
//     dest->a2 = src->a2;
//     dest->a3 = src->a3;
//     dest->t0 = src->t0;
//     dest->t1 = src->t1;
//     dest->t2 = src->t2;
//     dest->t3 = src->t3;
//     dest->t4 = src->t4;
//     dest->t5 = src->t5;
//     dest->t6 = src->t6;
//     dest->t7 = src->t7;
//     dest->s0 = src->s0;
//     dest->s1 = src->s1;
//     dest->s2 = src->s2;
//     dest->s3 = src->s3;
//     dest->s4 = src->s4;
//     dest->s5 = src->s5;
//     dest->s6 = src->s6;
//     dest->s7 = src->s7;
//     dest->t8 = src->t8;
//     dest->t9 = src->t9;
//     dest->hi = src->hi;
//     dest->lo = src->lo;
//     dest->gp = src->gp;
//     dest->sp = src->sp;
//     dest->fp = src->fp;
//     dest->ra = src->ra;
// }

// void init_pc() {
//     int i;
//     for (i = 1; i < 8; i++)
//         pcb[i].ASID = -1;
//     pcb[0].ASID = 0;
//     pcb[0].counter = PROC_DEFAULT_TIMESLOTS;
//     kernel_strcpy(pcb[0].name, "init");
//     curr_proc = 0;
//     register_syscall(10, pc_kill_syscall);
//     register_interrupt_handler(7, pc_schedule);

//     asm volatile(
//         "li $v0, 1000000\n\t"
//         "mtc0 $v0, $11\n\t"
//         "mtc0 $zero, $9");
// }

// void pc_schedule(unsigned int status, unsigned int cause, context*
// pt_context) {
//     // Save context
//     copy_context(pt_context, &(pcb[curr_proc].context));
//     int i;
//     for (i = 0; i < 8; i++) {
//         curr_proc = (curr_proc + 1) & 7;
//         if (pcb[curr_proc].ASID >= 0)
//             break;
//     }
//     if (i == 8) {
//         kernel_puts("Error: PCB[0] is invalid!\n", 0xfff, 0);
//         while (1)
//             ;
//     }
//     // Load context
//     copy_context(&(pcb[curr_proc].context), pt_context);
//     asm volatile("mtc0 $zero, $9\n\t");
// }

// int pc_peek() {
//     int i = 0;
//     for (i = 0; i < 8; i++)
//         if (pcb[i].ASID < 0)
//             break;
//     if (i == 8)
//         return -1;
//     return i;
// }

// void pc_create(int asid, void (*func)(), unsigned int init_sp, unsigned int
// init_gp, char* name) {
//     pcb[asid].context.epc = (unsigned int)func;
//     pcb[asid].context.sp = init_sp;
//     pcb[asid].context.gp = init_gp;
//     kernel_strcpy(pcb[asid].name, name);
//     pcb[asid].ASID = asid;
// }

// void pc_kill_syscall(unsigned int status, unsigned int cause, context*
// pt_context) {
//     if (curr_proc != 0) {
//         pcb[curr_proc].ASID = -1;
//         pc_schedule(status, cause, pt_context);
//     }
// }

// int pc_kill(int proc) {
//     proc &= 7;
//     if (proc != 0 && pcb[proc].ASID >= 0) {
//         pcb[proc].ASID = -1;
//         return 0;
//     } else if (proc == 0)
//         return 1;
//     else
//         return 2;
// }

// task_struct* get_curr_pcb() {
//     return &pcb[curr_proc];
// }

// int print_proc() {
//     int i;
//     kernel_puts("PID name\n", 0xfff, 0);
//     for (i = 0; i < 8; i++) {
//         if (pcb[i].ASID >= 0)
//             kernel_printf(" %x  %s\n", pcb[i].ASID, pcb[i].name);
//     }
//     return 0;
// }
#include <driver/vga.h>
#include <intr.h>
#include <zjunix/pc.h>
#include <zjunix/pid.h>
#include <zjunix/slab.h>
struct task_struct *idle;
struct task_struct *current_task;

struct list_head task_all;
struct list_head task_waiting;
struct list_head task_ready;
struct list_head task_dead;

struct cfs_rq cfs_rq;
int atomic_pid = 0;
int sysctl_sched_latency = 100000000;
static void copy_context(reg_context_t *src, reg_context_t *dest) {
  dest->epc = src->epc;
  dest->at = src->at;
  dest->v0 = src->v0;
  dest->v1 = src->v1;
  dest->a0 = src->a0;
  dest->a1 = src->a1;
  dest->a2 = src->a2;
  dest->a3 = src->a3;
  dest->t0 = src->t0;
  dest->t1 = src->t1;
  dest->t2 = src->t2;
  dest->t3 = src->t3;
  dest->t4 = src->t4;
  dest->t5 = src->t5;
  dest->t6 = src->t6;
  dest->t7 = src->t7;
  dest->s0 = src->s0;
  dest->s1 = src->s1;
  dest->s2 = src->s2;
  dest->s3 = src->s3;
  dest->s4 = src->s4;
  dest->s5 = src->s5;
  dest->s6 = src->s6;
  dest->s7 = src->s7;
  dest->t8 = src->t8;
  dest->t9 = src->t9;
  dest->hi = src->hi;
  dest->lo = src->lo;
  dest->gp = src->gp;
  dest->sp = src->sp;
  dest->fp = src->fp;
  dest->ra = src->ra;
}

static void add_task(struct task_struct *p) {
  list_add_tail(&(p->running_node), &task_all);
}
void init_task_module() {
  INIT_CFS_RQ(&cfs_rq);
  INIT_LIST_HEAD(&task_all);
  INIT_LIST_HEAD(&task_waiting);
  INIT_LIST_HEAD(&task_ready);
  INIT_LIST_HEAD(&task_dead);

  // // setting idle process
  union task_union *tmp = (union task_union *)(kernel_sp - TASK_KERNEL_SIZE);
  idle = &tmp->task;
  struct task_struct *p = idle;
  kernel_strcpy(idle->name, "idle");
  kernel_memset(&p->context, 0, sizeof(context));
  p->nice = 0;
  p->prio = 20;
  p->pid = atomic_pid++;
  p->tgid = idle->pid;
  p->parent = NULL;
  p->group_leader = NULL;
  // setting idle process se
  struct sched_entity *se = &(idle->se);
  se->vruntime = 100;
  se->sum_exec_runtime = 0;
  se->prev_sum_exec_runtime = 0;
  se->load.weight = prio_to_weight[idle->prio];
  se->load.inv_weight = prio_to_wmult[idle->prio];

  enqueue_task_fair(&cfs_rq, idle);
  current_task = idle;
  cfs_rq.curr = &(current_task->se);
  register_interrupt_handler(7, task_schedule);
  asm volatile(
      "mtc0 %0, $11\n\t"
      "mtc0 $zero, $9"
      :
      : "r"(sysctl_sched_latency));
}


void task_schedule(unsigned int status, unsigned int cause,
                   context *pt_context) {
  disable_interrupts();
  kernel_printf("current task: name %s pid %d, vruntime %d\n",
                current_task->name, current_task->pid,
                current_task->se.vruntime);
  update_curr(&cfs_rq, 1);

  struct task_struct *next = pick_next_task_fair(&cfs_rq);
  if (current_task == next) {
    asm volatile("mtc0 $zero, $9\n\t");
    enable_interrupts();
    return;
  }
  copy_context(pt_context, &(current_task->context));
  copy_context(&(next->context), pt_context);
  current_task = next;
  cfs_rq.curr = &current_task->se;
  kernel_printf("schedule here\n");
  asm volatile("mtc0 $zero, $9\n\t");
  enable_interrupts();
}

// reserve namespace
void task_create(char *task_name, void (*entry)(unsigned int argc, void *args),
                 unsigned int argc, void *args, pid_t *retpid, int nice) {
  union task_union *tmp = (union task_union *)kmalloc(sizeof(union task_union));
  struct task_struct *new_task = &(tmp->task);
  kernel_strcpy(new_task->name, task_name);
  new_task->nice = 0;
  new_task->prio = 20;
  new_task->pid = atomic_pid++;
  new_task->tgid = new_task->pid;
  new_task->parent = NULL;
  new_task->group_leader = NULL;
  // setting new_task process se
  struct sched_entity *se = &(new_task->se);
  se->vruntime = current_task->se.vruntime;
  se->sum_exec_runtime = 0;
  se->prev_sum_exec_runtime = 0;
  se->load.weight = prio_to_weight[new_task->prio];
  se->load.inv_weight = prio_to_wmult[new_task->prio];
  kernel_memset(&(new_task->context), 0, sizeof(context));

  new_task->context.epc = (unsigned int)entry;
  new_task->context.sp = (unsigned int)tmp + TASK_KERNEL_SIZE;
  unsigned int init_gp;
  asm volatile("la %0, _gp\n\t" : "=r"(init_gp));
  new_task->context.gp = init_gp;

  new_task->context.a0 = argc;
  new_task->context.a1 = (unsigned int)args;
  enqueue_task_fair(&cfs_rq, new_task);
  update_min_vruntime(&cfs_rq);
  kernel_printf("task create %s %d %d\n",task_name, new_task->se.vruntime, new_task->pid);
}

void task_kill() {}

void task_wait() {}

struct task_struct *get_current_task() {
  return current_task;
}

void pc_schedule(unsigned int status, unsigned int cause, context *pt_context) {
}
int pc_peek() {}
void pc_create(int asid, void (*func)(), unsigned int init_sp,
               unsigned int init_gp, char *name) {}
void pc_kill_syscall(unsigned int status, unsigned int cause,
                     context *pt_context) {}
int pc_kill(int proc) {}
int print_proc() {}