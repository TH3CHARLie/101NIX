#include <driver/vga.h>
#include <intr.h>
#include <zjunix/fs/fat.h>
#include <zjunix/pid.h>
#include <zjunix/slab.h>
#include <zjunix/syscall.h>
#include <zjunix/time.h>
#include <zjunix/utils.h>
#include <zjunix/vfs/vfs.h>
#include <zjunix/vm.h>
#include <../usr/ps.h>
// global ptr to init process
// currently init process serve no special purpose
// may extend to do task like GC later
struct task_struct *init;

// global ptr to current process
// intend for quick fetch
struct task_struct *current_task;

// global ptr to root namespace
// root namespace is important since all pid operations use
// pid from root namespace
struct pid_namespace *root_pid_namespace;

// global ptr to current namespace
// intend for quick fetch
struct pid_namespace *curr_pid_namespace;

// process list for all existing processes
struct list_head task_all;

// process list for all waiting processes
struct list_head task_waiting;

// process list for all ready processes
struct list_head task_ready;

// cfs run queue
struct cfs_rq cfs_rq;

static const unsigned int CACHE_BLOCK_SIZE = 64;
#define max(a, b) ((a > b) ? (a) : (b))

// save context when doing context switch in interrupt
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

// set a process state
// link it to corresponding state list
static void set_state(struct task_struct *p, struct list_head *state_list) {
  list_add_tail(&(p->state_node), state_list);
}

// unset a process state
// delete it from state list
static void unset_state(struct task_struct *p) {
  list_del_init(&(p->state_node));
}

// add a process to all process list
static void add_task(struct task_struct *p) {
  list_add_tail(&(p->task_node), &task_all);
}

// remove a process
static void delete_task(struct task_struct *p) {
  list_del_init(&(p->task_node));
}

// initlialize task module
void init_task_module() {
  // set pid namespace
  init_pid_module();
  root_pid_namespace = pid_namespace_create(NULL);
  curr_pid_namespace = root_pid_namespace;
  
  // set run queue and lists
  INIT_CFS_RQ(&cfs_rq);
  INIT_LIST_HEAD(&task_all);
  INIT_LIST_HEAD(&task_waiting);
  INIT_LIST_HEAD(&task_ready);

  // setting init process
  union task_union *tmp = (union task_union *)(kernel_sp - TASK_KERNEL_SIZE);
  init = &tmp->task;
  struct task_struct *p = init;
  kernel_strcpy(init->name, "init");
  kernel_memset(&p->context, 0, sizeof(context));
  
  // set prio
  p->nice = 0;
  p->prio = 20;

  // assign pid from namespace
  int assign_pid_res = assign_real_pid_from_ns(p, root_pid_namespace);
  if (assign_pid_res == 1) {
    kernel_printf("[init_task_module]: fatal, init pid assign fail\n");
  }
  p->pid = p->real_pid.numbers[0].nr;
  p->parent = NULL;

  // setting init process se
  struct sched_entity *se = &(init->se);
  se->vruntime = 0;
  se->sum_exec_runtime = 0;
  se->prev_sum_exec_runtime = 0;
  se->load.weight = prio_to_weight[init->prio];
  se->load.inv_weight = prio_to_wmult[init->prio];

  // add task to lists and rq
  add_task(init);
  enqueue_task_fair(&cfs_rq, init);
  set_state(init, &task_ready);

  // init is a kernel process
  // no vm or user_pc_mem created
  init->vm = NULL;
  kernel_memset(init->user_pc_memory_blocks, 0,
                sizeof(init->user_pc_memory_blocks));
  init->user_mode = 0;
  current_task = init;

  // configure cfs_rq
  cfs_rq.curr = &(current_task->se);
  init->state = TASK_RUNNING;

  // enable timer interrupt
  register_interrupt_handler(7, task_tick);

  asm volatile(
      "mtc0 %0, $11\n\t"
      "mtc0 $zero, $9"
      :
      : "r"(sysctl_sched_min_granularity_ns));
}

// called when return from function
// that may cause a schedule event
void ret_from_sched_syscall() {
  if (cfs_rq.NEED_SCHED) {
    asm volatile(
        "li $v0, 15\n\t"
        "syscall\n\t");
  }
}


// timer interrupt
// serve as secondary-scheduler
// do not schedule it self, only calculate time slice
// wait for main-scheduler to finish up
// however, since no nested interrupt is allowed
// we inline the code from main scheduler here
void task_tick(unsigned int status, unsigned int cause, context *pc_context) {
  unsigned int old_ie = disable_interrupts();

  // update current process time slice
  // check whether it needs to schedule
  update_curr(&cfs_rq,
              sysctl_sched_min_granularity_ns / sysctl_sched_time_unit);
  check_preempt_tick(&cfs_rq, &current_task->se);

  // our function can stop here
  // however, no nested interrupt allowed
  // so we add the following code here to do schedule
  // remember, not every timer interrupt causes schedule
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
  if (old_ie) {
    asm volatile("mtc0 $zero, $9\n\t");
    enable_interrupts();
    return;
  }
}

// main scheduler 
// called when mark on cfs_rq is true
void task_schedule(unsigned int status, unsigned int cause,
                   context *pc_context) {
  unsigned int old_ie = disable_interrupts();
  
  // choose the next task to run
  struct task_struct *next = pick_next_task_fair(&cfs_rq);
  if (current_task == next) {
    goto finish;
  }

  // context save and switch
  copy_context(pc_context, &(current_task->context));
  copy_context(&(next->context), pc_context);
  current_task->state = TASK_READY;

  // update time info
  current_task->se.prev_sum_exec_runtime = current_task->se.sum_exec_runtime;
  current_task = next;
  current_task->state = TASK_RUNNING;
  cfs_rq.curr = &current_task->se;
  
  // active tlb
  set_active_asid(current_task->pid);

finish:
  cfs_rq.NEED_SCHED = false;
  asm volatile("mtc0 $zero, $9\n\t");
  if (old_ie) {
    enable_interrupts();
  }
}

// create a new process
// task_name: process name
// entry: process code address
// argc: argument count
// argv: arguments
// nice: nice value
// user_mode: if the process is a user process
struct task_struct *task_create(char *task_name,
                                void (*entry)(unsigned int argc, void *args),
                                unsigned int argc, void *args, int nice,
                                int user_mode) {
  // malloc memory
  union task_union *tmp = (union task_union *)kmalloc(sizeof(union task_union));
  if (tmp == 0) {
    kernel_printf("allocate fail, return\n");
    return NULL;
  }

  struct task_struct *new_task = &(tmp->task);
  kernel_strcpy(new_task->name, task_name);
  
  // set prio
  new_task->nice = nice;
  new_task->static_prio = new_task->nice + 20;
  new_task->prio = new_task->nice + 20;
  
  // assign pid from current namespace
  int assign_pid_res = assign_real_pid_from_ns(new_task, curr_pid_namespace);
  if (assign_pid_res == -1) {
    kernel_printf("[task_create]: fatal, init pid assign fail\n");
    return NULL;
  }
  new_task->pid = new_task->real_pid.numbers[0].nr;
  new_task->parent = NULL;

  // setting new_task process se
  struct sched_entity *se = &(new_task->se);
  se->vruntime = current_task->se.vruntime;
  se->sum_exec_runtime = 0;
  se->prev_sum_exec_runtime = 0;
  se->load.weight = prio_to_weight[new_task->prio];
  se->load.inv_weight = prio_to_wmult[new_task->prio];

  // set context, sp and pc
  kernel_memset(&(new_task->context), 0, sizeof(context));
  new_task->context.epc = (unsigned int)entry;
  new_task->context.sp = (unsigned int)tmp + TASK_KERNEL_SIZE;
  unsigned int init_gp;
  asm volatile("la %0, _gp\n\t" : "=r"(init_gp));
  new_task->context.gp = init_gp;
  new_task->context.a0 = argc;
  new_task->context.a1 = (unsigned int)args;

  // add task and set its state
  enqueue_task_fair(&cfs_rq, new_task);
  add_task(new_task);
  set_state(new_task, &task_ready);
  new_task->state = TASK_READY;

  // if user mode
  // allocate vm and user mem
  if (user_mode != 0) {
    new_task->vm = vm_create();
    memory_pool_create(new_task);
  } else {
    new_task->vm = NULL;
    kernel_memset(new_task->user_pc_memory_blocks, 0,
                  sizeof(new_task->user_pc_memory_blocks));
  }

  // update cfs_rq
  new_task->user_mode = user_mode;
  update_min_vruntime(&cfs_rq);
  kernel_printf("[task_create]: name=%s pid=%d nice=%d\n", new_task->name,
                new_task->pid, new_task->nice);
  return new_task;
}

// kill a process using pid
void task_kill(pid_t pid) {
  if (pid == 0) {
    kernel_printf("task_kill: operation not permitted\n");
    return;
  }
  unsigned int old_ie = disable_interrupts();
  struct list_head *pos;
  struct task_struct *p, *to_be_freed = NULL;
  bool is_cur = false;

  // go through all tasks
  list_for_each(pos, &task_all) {
    p = container_of(pos, struct task_struct, task_node);
    if (p && p->pid == pid) {
      // if (kernel_strcmp(p->name, "powershell") == 0) {
      //   powershell_killed = 1;
      // }
      // if match
      // delete task from cfs_rq and list
      // also reset vm and mem pool
      to_be_freed = p;
      delete_task(p);
      unset_state(p);
      dequeue_task_fair(&cfs_rq, p);
      p->state = TASK_DEAD;
      if (p->user_mode != 0) {
        vm_delete(p);
        memory_pool_delete(p);
      }
      // free pid
      free_real_pid(p);
      break;
    }
  }
  update_min_vruntime(&cfs_rq);
  kernel_printf("[task_kill] kill process %d\n", p->pid);
  if (to_be_freed) {
    // kfree(to_be_freed);
  }
  // if (powershell_killed) {
  //   task_create("powershell", ps, 0, 0, 0, 0);
  //   powershell_killed = 0;
  // }
  if (old_ie) {
    enable_interrupts();
  }
}

// block a process using pid
void task_wait(pid_t pid) {
  unsigned int current_clock;
  asm volatile(
      "mfc0 $t0, $9\n\t"
      "move %0, $t0\n\t"
      : "=r"(current_clock));
  u32 delta = current_clock / sysctl_sched_time_unit;
  if (delta == 0) {
    delta = 1;
  }
  if (pid == 0) {
    kernel_printf("task_wait: operation not permitted\n");
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
      // remove from running queue and list
      unset_state(p);
      set_state(p, &task_waiting);
      dequeue_task_fair(&cfs_rq, p);
      p->state = TASK_WAITING;
      break;
    }
  }
  // update time info
  update_min_vruntime(&cfs_rq);
  if (old_ie) {
    enable_interrupts();
  }
  if (is_cur) {
    ret_from_sched_syscall();
  }
}

// wake a process
void task_wakeup(pid_t pid) {
  unsigned int current_clock;
  asm volatile(
      "mfc0 $t0, $9\n\t"
      "move %0, $t0\n\t"
      : "=r"(current_clock));
  u32 delta = current_clock / sysctl_sched_time_unit;
  if (pid == 0) {
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
      // check whether the wake up process needs schedule
      check_preempt_wakeup(&cfs_rq, p);
      break;
    }
  }
  update_min_vruntime(&cfs_rq);
  kernel_printf("[task_wakeup]: wake process %d\n", pid);
  if (old_ie) {
    enable_interrupts();
  }
  ret_from_sched_syscall();
}

void task_kill_syscall(unsigned int status, unsigned int cause,
                       context *pc_context) {}


// process exit syscall
// call kill to kill itself
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
  update_curr(&cfs_rq, 10000);
  pid_t pid_to_kill = current_task->pid;
  kernel_printf("[task_kill]: kill process %s pid=%d\n", current_task->name,
                current_task->pid);
  struct task_struct *next = pick_next_task_fair(&cfs_rq);
  if (next == current_task) {
    kernel_printf("next_name=%s, vtime=%d, cur_name=%s, vtime=%d\n", next->name,
                  next->se.vruntime, current_task->name,
                  current_task->se.vruntime);
    kernel_printf("task_kill: fatal\n");
    kernel_printf("cfs vruntime=%d\n", cfs_rq.min_vruntime);
    next = init;
    return;
  }
  copy_context(&(next->context), pc_context);
  current_task = next;
  current_task->state = TASK_RUNNING;
  cfs_rq.curr = &current_task->se;
  set_active_asid(current_task->pid);
  task_kill(pid_to_kill);
}

// get current process
struct task_struct *get_current_task() {
  return current_task;
}



// exec a program from file
// create a new process
int task_exec_from_file(char *filename) {
  unsigned int old_ie = disable_interrupts();
  struct file *file;
  file = vfs_open(filename, O_RDONLY, 0);
  if (IS_ERR_OR_NULL(file)) {
    kernel_printf("[exec]: File %s not exist\n", filename);
    if (old_ie) {
      enable_interrupts();
    }
    return 1;
  }

  // load program text
  unsigned int size = file->f_dentry->d_inode->i_size;
  unsigned int n = size / CACHE_BLOCK_SIZE + 1;
  unsigned int i = 0;
  unsigned int j = 0;
  void *user_proc_entry = (void *)kmalloc(size);
  u32 base = 0;

  
  if (vfs_read(file, (char *)user_proc_entry, size, &base) != size) {
    kernel_printf("[exec]:File %s read failed\n", filename);
    if (old_ie) {
      enable_interrupts();
    }
    return 1;
  }
  // create process
  task_struct *pcb = task_create(filename, (void *)0, 0, 0, 0, 1);
  
  // set vma mapping
  // important for address tranlation
  vma_set_mapping(pcb, 0, user_proc_entry);
  kernel_printf("[exec]: success, run user program %s\n", filename);
  if (old_ie) {
    enable_interrupts();
  }
  return 0;
}

// print helper function
char *state_to_string(int state) {
  static char *str[4] = {"RUNNING", "WAIT", "READY", "DEAD"};
  if (state == TASK_RUNNING) {
    return str[TASK_RUNNING];
  } else if (state == TASK_READY) {
    return str[TASK_READY];
  } else if (state == TASK_WAITING) {
    return str[TASK_WAITING];
  } else {
    return str[TASK_DEAD];
  }
}

// print all processes
int print_proc() {
  struct list_head *pos;
  struct task_struct *p;

  kernel_printf("name pid namespace-level vruntime state\n");
  list_for_each(pos, &task_all) {
    p = container_of(pos, struct task_struct, task_node);
    kernel_printf("%s %d %d %d %s\n", p->name, p->pid, p->real_pid.level,
                  p->se.vruntime, state_to_string(p->state));
  }
}
