#include <zjunix/cfs.h>
#include <zjunix/pc.h>
extern struct list_head task_ready;
static inline u32 max_vruntime(u32 max_vruntime, u32 vruntime) {
  s32 delta = (s32)(vruntime - max_vruntime);
  if (delta > 0) {
    max_vruntime = vruntime;
  }
  return max_vruntime;
}

static inline u32 min_vruntime(u32 min_vruntime, u32 vruntime) {
  s32 delta = (s32)(vruntime - min_vruntime);
  if (delta < 0) {
    min_vruntime = vruntime;
  }
  return min_vruntime;
}

static inline int entity_before(struct sched_entity* a,
                                struct sched_entity* b) {
  return (s32)(a->vruntime - b->vruntime) < 0;
}

static inline u32 __calc_delta(u32 delta, unsigned long weight,
                               struct load_weight* lw) {
  return delta * weight / lw->weight;
}

static inline u32 calc_delta_fair(u32 delta, struct sched_entity* se) {
  if (se->load.weight != NICE_0_LOAD) {
    return __calc_delta(delta, NICE_0_LOAD, &se->load);
  } else {
    return delta * prio_to_wmult[20];
  }
}

static void update_load_add(struct load_weight* lw, unsigned int weight) {
  lw->weight += weight;
}

static void update_load_sub(struct load_weight* lw, unsigned int weight) {
  lw->weight -= weight;
}

static inline void add_nr_running(struct cfs_rq* cfs_rq, unsigned int count) {
  unsigned int prev_nr = cfs_rq->nr_running;
  cfs_rq->nr_running = prev_nr + count;
}

static void enqueue_entity(struct cfs_rq* cfs_rq, struct sched_entity* se) {
  struct rb_node** link = &cfs_rq->tasks_timeline.rb_node;
  struct rb_node* parent = NULL;
  struct sched_entity* entry;
  bool leftmost = true;

  while (*link) {
    parent = *link;
    entry = rb_entry(parent, struct sched_entity, rb_node);
    if (entity_before(se, entry)) {
      link = &parent->rb_left;
    } else {
      link = &parent->rb_right;
      leftmost = false;
    }
  }
  if (leftmost) {
    cfs_rq->rb_leftmost = &se->rb_node;
  }
  rb_link_node(&se->rb_node, parent, link);
  rb_insert_color(&se->rb_node, &cfs_rq->tasks_timeline);
}

static inline void sub_nr_running(struct cfs_rq* cfs_rq, unsigned int count) {
  unsigned int prev_nr = cfs_rq->nr_running;
  cfs_rq->nr_running = prev_nr - count;
}

static void dequeue_entity(struct cfs_rq* cfs_rq, struct sched_entity* se) {
  if (cfs_rq->rb_leftmost == &se->rb_node) {
    struct rb_node* next_node;
    next_node = rb_next(&se->rb_node);
    cfs_rq->rb_leftmost = next_node;
  }
  rb_erase(&se->rb_node, &cfs_rq->tasks_timeline);
}

static void reassign_vruntime(struct cfs_rq* cfs_rq) {
  struct list_head* pos;
  struct task_struct* p;
  list_for_each(pos, &task_ready) {
    p = container_of(pos, struct task_struct, task_node);
    p->se.vruntime = 0;
  }
  cfs_rq->rb_leftmost = rb_next(cfs_rq->rb_leftmost);
  cfs_rq->min_vruntime = 0;
}

void INIT_CFS_RQ(struct cfs_rq* cfs_rq) {
  cfs_rq->load.inv_weight = 0;
  cfs_rq->load.weight = 0;
  cfs_rq->nr_running = 0;
  cfs_rq->exec_clock = 0;
  cfs_rq->rb_leftmost = NULL;
  cfs_rq->curr = NULL;
  cfs_rq->tasks_timeline.rb_node = NULL;
}

void update_min_vruntime(struct cfs_rq* cfs_rq) {
  struct sched_entity* curr = cfs_rq->curr;

  struct rb_node* leftmost = rb_first(&cfs_rq->tasks_timeline);
  cfs_rq->rb_leftmost = leftmost;

  u32 vruntime = cfs_rq->min_vruntime;

  if (curr) {
    if (curr->on_cfs_rq) {
      vruntime = curr->vruntime;
    } else {
      curr = NULL;
    }
  }
  if (leftmost) {
    struct sched_entity* se;
    se = rb_entry(leftmost, struct sched_entity, rb_node);
    if (!curr) {
      vruntime = se->vruntime;
    } else {
      vruntime = min_vruntime(vruntime, se->vruntime);
    }
  }
  cfs_rq->min_vruntime = max_vruntime(cfs_rq->min_vruntime, vruntime);
}

void update_curr(struct cfs_rq* cfs_rq, u32 delta) {
  struct sched_entity* curr = cfs_rq->curr;
  if (!curr) {
    return;
  }
  curr->sum_exec_runtime += delta;
  u32 vruntime_delta = calc_delta_fair(delta, curr);
  if (curr->vruntime + vruntime_delta >= U32_MAX) {
    // overflow happens
    curr->vruntime = U32_MAX;
  } else {
    curr->vruntime += vruntime_delta;
  }
  dequeue_entity(cfs_rq, curr);
  enqueue_entity(cfs_rq, curr);
  update_min_vruntime(cfs_rq);
  if (cfs_rq->min_vruntime + 10 >= U32_MAX) {
    // all faces overflow
    reassign_vruntime(cfs_rq);
  }
}

void enqueue_task_fair(struct cfs_rq* cfs_rq, struct task_struct* p) {
  struct sched_entity* se = &p->se;
  if (se && cfs_rq) {
    enqueue_entity(cfs_rq, se);
    update_load_add(&cfs_rq->load, se->load.weight);
    add_nr_running(cfs_rq, 1);
    se->on_cfs_rq = true;
  }
}

void dequeue_task_fair(struct cfs_rq* cfs_rq, struct task_struct* p) {
  struct sched_entity* se = &p->se;
  if (se && cfs_rq) {
    dequeue_entity(cfs_rq, se);
    update_load_sub(&cfs_rq->load, se->load.weight);
    sub_nr_running(cfs_rq, 1);
    se->on_cfs_rq = false;
  }
}

static struct sched_entity* pick_next_entity(struct cfs_rq* cfs_rq) {
  struct rb_node* left = cfs_rq->rb_leftmost;
  if (!left) {
    return NULL;
  }
  return rb_entry(left, struct sched_entity, rb_node);
}
static inline struct task_struct* task_of(struct sched_entity* se) {
  return container_of(se, struct task_struct, se);
}

struct task_struct* pick_next_task_fair(struct cfs_rq* cfs_rq) {
  struct sched_entity* se = pick_next_entity(cfs_rq);
  struct task_struct* p = task_of(se);
  return p;
}

static u32 __sched_period(unsigned long nr_running) {
  if (nr_running > sysctl_sched_nr_latency)
    return nr_running * sysctl_sched_min_granularity_ns;
  else
    return sysctl_sched_latency;
}

static u32 __normliaze_ticks(u32 ticks) {
  return ticks / sysctl_sched_time_unit;
}

static u32 sched_slice(struct cfs_rq* cfs_rq, struct sched_entity* se) {
  u32 period = __sched_period(cfs_rq->nr_running + !se->on_cfs_rq);
  period = __normliaze_ticks(period);
  u32 slice;
  struct load_weight* load;
  struct load_weight lw;
  load = &cfs_rq->load;

  if (!se->on_cfs_rq) {
    lw = cfs_rq->load;
    update_load_add(&lw, se->load.weight);
    load = &lw;
  }
  slice = __calc_delta(period, se->load.weight, load);
  return slice;
}

void check_preempt_tick(struct cfs_rq* cfs_rq,
                               struct sched_entity* curr) {
  u32 ideal_runtime, delta_exec;
  ideal_runtime = sched_slice(cfs_rq, curr);
  delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
  if (delta_exec > ideal_runtime) {
    cfs_rq->NEED_SCHED = true;
    return;
  }
  // do we need wakeup check?
}

void check_preempt_wakeup(struct cfs_rq* cfs_rq, struct task_struct *p) {
  struct task_struct *curr = container_of(cfs_rq->curr, struct task_struct, se);
  struct sched_entity *curr_se = &curr->se, *pse = &p->se;
  bool scale = cfs_rq->nr_running >= sysctl_sched_nr_latency;

  if (curr_se == pse) {
    return;
  }
  s32 vdiff = curr_se->vruntime - pse->vruntime;
  if (vdiff <= 0) {
    return;
  }
  u32 gran = calc_delta_fair(sysctl_sched_wakeup_granularity/sysctl_sched_time_unit, pse);
  if (vdiff > gran) {
    cfs_rq->NEED_SCHED = true;  
  }
}

