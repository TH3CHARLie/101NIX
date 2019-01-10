#ifndef _101NIX_CFS_H_
#define _101NIX_CFS_H_
#include <zjunix/list.h>
#include <zjunix/rbtree.h>

#define NICE_0_LOAD 1024
#define U32_MAX 2147483647
static const unsigned int sysctl_sched_latency = 9600000;

static const unsigned int sysctl_sched_min_granularity_ns = 1200000;

// 4:3 to min granularity
static const unsigned int sysctl_sched_wakeup_granularity = 1600000;

static const unsigned int sysctl_sched_time_unit = 20000;

static const unsigned int sysctl_sched_nr_latency = 8;
/*
 * Nice levels are multiplicative, with a gentle 10% change for every
 * nice level changed. I.e. when a CPU-bound task goes from nice 0 to
 * nice 1, it will get ~10% less CPU time than another CPU-bound task
 * that remained on nice 0.
 *
 * The "10% effect" is relative and cumulative: from _any_ nice level,
 * if you go up 1 level, it's -10% CPU usage, if you go down 1 level
 * it's +10% CPU usage. (to achieve that we use a multiplier of 1.25.
 * If a task goes up by ~10% and another task goes down by ~10% then
 * the relative distance between them is ~25%.)
 */
static const int prio_to_weight[40] = {
    /* -20 */ 88761, 71755, 56483, 46273, 36291,
    /* -15 */ 29154, 23254, 18705, 14949, 11916,
    /* -10 */ 9548,  7620,  6100,  4904,  3906,
    /*  -5 */ 3121,  2501,  1991,  1586,  1277,
    /*   0 */ 1024,  820,   655,   526,   423,
    /*   5 */ 335,   272,   215,   172,   137,
    /*  10 */ 110,   87,    70,    56,    45,
    /*  15 */ 36,    29,    23,    18,    15,
};

/*
 * Inverse (2^32/x) values of the prio_to_weight[] array, precalculated.
 *
 * In cases where the weight does not change often, we can use the
 * precalculated inverse to speed up arithmetics by turning divisions
 * into multiplications:
 */
static const u32 prio_to_wmult[40] = {
    /* -20 */ 48388,     59856,     76040,     92818,     118348,
    /* -15 */ 147320,    184698,    229616,    287308,    360437,
    /* -10 */ 449829,    563644,    704093,    875809,    1099582,
    /*  -5 */ 1376151,   1717300,   2157191,   2708050,   3363326,
    /*   0 */ 4194304,   5237765,   6557202,   8165337,   10153587,
    /*   5 */ 12820798,  15790321,  19976592,  24970740,  31350126,
    /*  10 */ 39045157,  49367440,  61356676,  76695844,  95443717,
    /*  15 */ 119304647, 148102320, 186737708, 238609294, 286331153,
};
// struct load_weight
struct load_weight {
  unsigned long weight;
  unsigned long inv_weight;
};

struct cfs_rq;

// struct sched_entity is CFS schedule info
// embedded in struct task_struct
struct sched_entity {
  // load balancing
  struct load_weight load;

  // node on the rbtree
  struct rb_node rb_node;

  // whether the se is on cfs_rq
  bool on_cfs_rq;

  // the cfs_rq the se belongs to
  struct cfs_rq* cfs_rq_ptr;

  // normalized actual runtime
  u32 vruntime;

  // the actual runtime this se has run so far
  u32 sum_exec_runtime;

  // store previous actual runtime when the se
  // temporary leaves the CPU
  u32 prev_sum_exec_runtime;
};

// struct cfs_rq is the Completely Fair Schedule runqueue
struct cfs_rq {
  // total number of running processes in this rq
  unsigned long nr_running;

  // total task weights for balancing
  struct load_weight load;

  // clock count
  u32 exec_clock;

  // minimum virtual runtime
  u32 min_vruntime;

  // root to the rbtree
  struct rb_root tasks_timeline;

  // cache of se of minimum vruntime
  struct rb_node* rb_leftmost;

  struct sched_entity* curr;

  bool NEED_SCHED;
};

struct task_struct;

void INIT_CFS_RQ(struct cfs_rq* rq);

void update_min_vruntime(struct cfs_rq* cfs_rq);

void update_curr(struct cfs_rq* cfs_rq, u32 delta);

void enqueue_task_fair(struct cfs_rq* cfs_rq, struct task_struct* p);

void dequeue_task_fair(struct cfs_rq* cfs_rq, struct task_struct* p);

struct task_struct* pick_next_task_fair(struct cfs_rq* cfs_rq);

void check_preempt_tick(struct cfs_rq* cfs_rq, struct sched_entity* curr);

void check_preempt_wakeup(struct cfs_rq* cfs_rq, struct task_struct* p);

#endif