#include <driver/vga.h>
#include <intr.h>
// #include <zjunix/pc.h>
#include <zjunix/pid.h>
#include <zjunix/slab.h>
#include <zjunix/syscall.h>
#include <zjunix/time.h>
#include <zjunix/utils.h>
#include <zjunix/vm.h>
struct task_struct *init;
struct task_struct *current_task;

struct list_head task_all;
struct list_head task_waiting;
struct list_head task_ready;
struct list_head task_dead;
struct cfs_rq cfs_rq;
int atomic_pid = 0;
#define max(a, b) ((a > b) ? (a) : (b))
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

static void set_state(struct task_struct *p, struct list_head *state_list) {
  list_add_tail(&(p->state_node), state_list);
}
static void unset_state(struct task_struct *p) {
  list_del_init(&(p->state_node));
}

static void add_task(struct task_struct *p) {
  list_add_tail(&(p->task_node), &task_all);
}

static void delete_task(struct task_struct *p) {
  list_del_init(&(p->task_node));
}

void init_task_module() {
  INIT_CFS_RQ(&cfs_rq);
  INIT_LIST_HEAD(&task_all);
  INIT_LIST_HEAD(&task_waiting);
  INIT_LIST_HEAD(&task_ready);
  INIT_LIST_HEAD(&task_dead);

  // // setting init process
  union task_union *tmp = (union task_union *)(kernel_sp - TASK_KERNEL_SIZE);
  init = &tmp->task;
  struct task_struct *p = init;
  kernel_strcpy(init->name, "init");
  kernel_memset(&p->context, 0, sizeof(context));
  p->nice = 0;
  p->prio = 20;
  p->pid = atomic_pid++;
  p->tgid = init->pid;
  p->parent = NULL;
  p->group_leader = NULL;
  // setting init process se
  struct sched_entity *se = &(init->se);
  se->vruntime = 0;
  se->sum_exec_runtime = 0;
  se->prev_sum_exec_runtime = 0;
  se->load.weight = prio_to_weight[init->prio];
  se->load.inv_weight = prio_to_wmult[init->prio];

  add_task(init);
  enqueue_task_fair(&cfs_rq, init);
  set_state(init, &task_ready);
  init->vm = NULL;
  kernel_memset(init->user_pc_memory_blocks, 0,
                sizeof(init->user_pc_memory_blocks));
  init->user_mode = 0;
  current_task = init;
  cfs_rq.curr = &(current_task->se);
  init->state = TASK_RUNNING;
  register_interrupt_handler(7, task_tick);

  asm volatile(
      "mtc0 %0, $11\n\t"
      "mtc0 $zero, $9"
      :
      : "r"(sysctl_sched_min_granularity_ns));
}

void ret_from_sched_syscall() {
  if (cfs_rq.NEED_SCHED) {
    asm volatile(
        "li $v0, 15\n\t"
        "syscall\n\t");
  }
}

void task_tick(unsigned int status, unsigned int cause, context *pc_context) {
  unsigned int old_ie = disable_interrupts();
  update_curr(&cfs_rq,
              sysctl_sched_min_granularity_ns / sysctl_sched_time_unit);
  check_preempt_tick(&cfs_rq, &current_task->se);
  if (cfs_rq.NEED_SCHED) {
    struct task_struct *next = pick_next_task_fair(&cfs_rq);
    if (current_task == next) {
      goto finish;
    }
    copy_context(pc_context, &(current_task->context));
    copy_context(&(next->context), pc_context);
    current_task->state = TASK_READY;
    current_task->se.prev_sum_exec_runtime = current_task->se.sum_exec_runtime;
    current_task = next;
    current_task->state = TASK_RUNNING;
    cfs_rq.curr = &current_task->se;
    set_active_asid(current_task->pid);
  finish:
    cfs_rq.NEED_SCHED = false;
  }
  //   if (sem)
  //   kernel_printf("schedule name %s pid=%d vruntime=%d\n",
  //   current_task->name,
  //                 current_task->pid, current_task->se.vruntime);
  if (old_ie) {
    asm volatile("mtc0 $zero, $9\n\t");
    enable_interrupts();
    return;
  }
}

void task_schedule(unsigned int status, unsigned int cause,
                   context *pc_context) {
  unsigned int old_ie = disable_interrupts();

  struct task_struct *next = pick_next_task_fair(&cfs_rq);
  if (current_task == next) {
    goto finish;
  }

  copy_context(pc_context, &(current_task->context));
  copy_context(&(next->context), pc_context);
  current_task->state = TASK_READY;
  current_task->se.prev_sum_exec_runtime = current_task->se.sum_exec_runtime;
  current_task = next;
  current_task->state = TASK_RUNNING;
  cfs_rq.curr = &current_task->se;
  set_active_asid(current_task->pid);
finish:
  cfs_rq.NEED_SCHED = false;
  if (old_ie) {
    enable_interrupts();
  }
}
void task_create(char *task_name, void (*entry)(unsigned int argc, void *args),
                 unsigned int argc, void *args, int nice, int user_mode) {
  unsigned int old_ie = disable_interrupts();
  union task_union *tmp = (union task_union *)kmalloc(sizeof(union task_union));
  if (tmp == 0) {
    kernel_printf("allocate fail, return\n");
    return;
  }
  struct task_struct *new_task = &(tmp->task);
  kernel_strcpy(new_task->name, task_name);
  new_task->nice = 0;
  new_task->static_prio = new_task->nice + 20;
  new_task->prio = new_task->nice + 20;
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
  add_task(new_task);
  set_state(new_task, &task_ready);
  new_task->state = TASK_READY;
  if (user_mode != 0) {
    new_task->vm = vm_create();
    memory_pool_create(new_task);
  } else {
    new_task->vm = NULL;
    kernel_memset(new_task->user_pc_memory_blocks, 0,
                  sizeof(new_task->user_pc_memory_blocks));
  }
  new_task->user_mode = user_mode;
  update_min_vruntime(&cfs_rq);
  kernel_printf("task create %s %d vm=%x\n", new_task->name, new_task->pid, (unsigned int)new_task->vm);
  if (old_ie) {
    enable_interrupts();
  }
}

void task_kill(pid_t pid) {
  if (pid == 0 || pid == 1) {
    kernel_printf("task_kill: operation not permitted\n");
    return;
  }
  unsigned int old_ie = disable_interrupts();
  struct list_head *pos;
  struct task_struct *p, *to_be_freed = NULL;
  bool is_cur = false;
  list_for_each(pos, &task_all) {
    p = container_of(pos, struct task_struct, task_node);
    if (p && p->pid == pid) {
      kernel_printf("kill name %s vm=%x \n", p->name, (unsigned int)p->vm);
      to_be_freed = p;
      delete_task(p);
      unset_state(p);
      dequeue_task_fair(&cfs_rq, p);
      p->state = TASK_DEAD;
      if (p->user_mode != 0) {
        vm_delete(p);
        memory_pool_delete(p);
      }
      break;
    }
  }
  update_min_vruntime(&cfs_rq);
  if (to_be_freed) {
    kfree(to_be_freed);
  }
  if (old_ie) {
    enable_interrupts();
  }
}

void task_wait(pid_t pid) {
  unsigned int current_clock;
  asm volatile(
      "mfc0 $t0, $9\n\t"
      "move %0, $t0\n\t"
      : "=r"(current_clock));
  u32 delta = current_clock / sysctl_sched_time_unit;

  if (pid == 1 || pid == 2) {
    kernel_printf("task_wait: operation not permitted\n");
    return;
  }
  if (pid == current_task->pid) {
    return;
  }

  unsigned int old_ie = disable_interrupts();
  struct list_head *pos;
  struct task_struct *p;
  bool is_cur = false;
  list_for_each(pos, &task_all) {
    p = container_of(pos, struct task_struct, task_node);
    if (p && p->pid == pid) {
      if (p == current_task) {
        update_curr(&cfs_rq, delta);
        is_cur = true;
        cfs_rq.NEED_SCHED = true;
      }
      unset_state(p);
      set_state(p, &task_waiting);
      dequeue_task_fair(&cfs_rq, p);
      p->state = TASK_WAITING;
      break;
    }
  }
  update_min_vruntime(&cfs_rq);
  if (old_ie) {
    enable_interrupts();
    if (is_cur) {
      ret_from_sched_syscall();
    }
  }
}

void task_wakeup(pid_t pid) {
  unsigned int current_clock;
  asm volatile(
      "mfc0 $t0, $9\n\t"
      "move %0, $t0\n\t"
      : "=r"(current_clock));
  u32 delta = current_clock / sysctl_sched_time_unit;
  if (pid == 1 || pid == 2) {
    return;
  }
  unsigned int old_ie = disable_interrupts();
  update_curr(&cfs_rq, delta);
  struct list_head *pos;
  struct task_struct *p;
  bool is_cur = false;
  list_for_each(pos, &task_waiting) {
    p = container_of(pos, struct task_struct, state_node);
    if (p && p->pid == pid) {
      unset_state(p);
      set_state(p, &task_ready);
      enqueue_task_fair(&cfs_rq, p);
      p->state = TASK_READY;
      p->se.vruntime =
          max(p->se.vruntime, cfs_rq.min_vruntime - NICE_0_LOAD * 8);
      check_preempt_wakeup(&cfs_rq, p);
      break;
    }
  }
  update_min_vruntime(&cfs_rq);
  if (old_ie) {
    enable_interrupts();
    ret_from_sched_syscall();
  }
}

void task_kill_syscall(unsigned int status, unsigned int cause,
                       context *pc_context) {}

void print_rbtree(struct rb_node *tree, struct rb_node *parent, int direction) {
  if (tree != NULL) {
    struct sched_entity *entity = rb_entry(tree, struct sched_entity, rb_node);
    task_struct *task = container_of(entity, struct task_struct, se);

    if (direction == 0) {
      // tree is root
      kernel_printf("  %s(PID : %d)(B) is root\n", task->name, (int)task->pid);
    } else {
      // tree is not root
      struct sched_entity *parent_entity =
          rb_entry(parent, struct sched_entity, rb_node);
      task_struct *parent_task =
          container_of(parent_entity, struct task_struct, se);
      kernel_printf("  %s(PID : %d)(%s) is %s's %s child vruntime is %d\n",
                    task->name, (int)task->pid, rb_is_black(tree) ? "B" : "R",
                    parent_task->name, direction == 1 ? "right" : "left",
                    entity->vruntime);
    }

    if (tree->rb_left) print_rbtree(tree->rb_left, tree, -1);
    if (tree->rb_right) print_rbtree(tree->rb_right, tree, 1);
  }
}

void task_exit_syscall(unsigned int status, unsigned int cause,
                       context *pc_context) {
  if (current_task->pid == 0 || current_task->pid == 1) {
    return;
  }
  unsigned int current_clock;
  asm volatile(
      "mfc0 $t0, $9\n\t"
      "move %0, $t0\n\t"
      : "=r"(current_clock));
  u32 delta = current_clock / sysctl_sched_time_unit;
  update_curr(&cfs_rq, 100);
  pid_t pid_to_kill = current_task->pid;
  kernel_printf("task_kill_syscall: kill process %s pid=%d\n",
                current_task->name, current_task->pid);
  struct task_struct *next = pick_next_task_fair(&cfs_rq);
  if (next == current_task) {
    kernel_printf("task_kill: fatal, OS should not kill itself here\n");
    return;
  }
  copy_context(&(next->context), pc_context);
  current_task = next;
  current_task->state = TASK_RUNNING;
  cfs_rq.curr = &current_task->se;
  set_active_asid(current_task->pid);
  task_kill(pid_to_kill);
}

struct task_struct *get_current_task() {
  return current_task;
}

void pc_schedule(unsigned int status, unsigned int cause, context *pc_context) {

}

int pc_kill(int proc) {}

int print_proc() {
  struct list_head *pos;
  struct task_struct *p;
  list_for_each(pos, &task_all) {
    p = container_of(pos, struct task_struct, task_node);
    kernel_printf("%s %d %d\n", p->name, p->pid, p->state);
  }
}
