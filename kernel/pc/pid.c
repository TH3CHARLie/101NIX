#include "pid.h"
#include "sched.h"

#define PID_MAX_HASH_ENTRY 4

struct hlist_head pid_hashtable[PID_MAX_HASH_ENTRY];

struct pid_namespace root_namespace;

// our hash function
unsigned int pid_hash_function(pid_t nr, struct pid_namespace *ns) {
  return (nr + ns->level) % PID_MAX_HASH_ENTRY;
}

static inline struct pid *task_pid(struct task_struct *task) {
  return task->pids[PIDTYPE_PID].pid;
}

static inline struct pid *task_tgid(struct task_struct *task) {
  return task->group_leader->pids[PIDTYPE_PID].pid;
}

static inline struct pid *task_pgid(struct task_struct *task) {
  return task->group_leader->pids[PIDTYPE_PGID].pid;
}

static inline pid_t pid_nr_ns(struct pid *pid, struct pid_namespace *ns) {
  struct upid *upid;
  pid_t nr = 0;
  if (pid && ns->level <= pid->level) {
    upid = &pid->numbers[ns->level];
    if (upid->ns == ns) {
      nr = upid->nr;
    }
  }
  return nr;
}

pid_t get_pid_val_by_task_ns(struct task_struct *task,
                             struct pid_namespace *ns) {
  return pid_nr_ns(task_pid(task), ns);
}

pid_t get_tgid_val_by_task_ns(struct task_struct *task,
                              struct pid_namespace *ns) {
  return pid_nr_ns(task_tgid(task), ns);
}

pid_t get_pgid_val_by_task_ns(struct task_struct *task,
                              struct pid_namespace *ns) {
  return pid_nr_ns(task_pgid(task), ns);
}

struct pid *get_pid_by_pid_val_ns(pid_t nr, struct pid_namespace *ns) {
  struct hlist_node *elem;
  struct upid *upid;
  struct hlist_head *head = &(pid_hashtable[pid_hash_function(nr, ns)]);
  hlist_for_each_entry(upid, head, pid_chain) {
    if (upid && upid->nr == nr && upid->ns == ns) {
      return container_of(upid, struct pid, numbers[ns->level]);
    }
  }
  return NULL;
}

struct task_struct *get_task_by_pid(struct pid *pid, enum pid_type type) {
  struct task_struct *result = NULL;
  if (pid) {
    struct hlist_node *first;
    first = pid->tasks[type].first;
    if (first) {
      result = hlist_entry(first, struct task_struct, pids[(type)].node);
    }
  }
  return result;
}


bool pid_namespace_empty(struct pid_namespace *ns) {
  return ns->pidmap.nr_free == PIDMAP_MAX_ENTRY;
}

bool pid_namespace_full(struct pid_namespace *ns) {
  return ns->pidmap.nr_free == 0;
}

void init_pid_module() {
  // initialize root_namespace
  root_namespace.level = 0;
  root_namespace.last_pid = 0;
  init_pidmap(&(root_namespace.pidmap));
  root_namespace.parent = NULL;
}

void init_pidmap(struct pidmap *pidmap) {
  pidmap->nr_free = PIDMAP_MAX_ENTRY;
  int i;
  for (i = 0; i < PIDMAP_BYTE; ++i) {
    pidmap->data[i] = 0;
  }
}



static bool pidmap_id_is_valid(int index, int offset, struct pidmap *map) {
  if (map->data[index] & (1 << offset))
    return true;
  else
    return false;
}

static void pidmap_id_alloc(int index, int offset, struct pidmap *map) {
  map->data[index] |= (1 << offset);
  map->nr_free--;
}

static void pidmap_id_free(int index, int offset, struct pidmap *map) {
  map->data[index] &= (~(1 << offset));
  map->nr_free++;
}

static pid_t alloc_pid_val_from_ns(struct pid_namespace *ns) {
  struct pidmap *map = &(ns->pidmap);
  if (pid_namespace_full(ns) || !map) {
    return -1;
  }
  pid_t res = ns->last_pid + 1;
  int index, offset;
  while (true) {
    index = res >> 3;
    offset = res & 0x07;
    if (pidmap_id_is_valid(index, offset, map)) {
      pidmap_id_alloc(index, offset, map);
      ns->last_pid = res;
      return res;
    } else {
      res = res + 1 % PIDMAP_MAX_ENTRY;
    }
  }
}


#ifdef DEBUG
struct pid *alloc_pid_from_ns(struct pid_namespace *ns) {
  struct pid *pid = ();
  enum pid_type type;
  int i, nr;
  struct pid_namespace *tmp;
  struct upid *upid;

  tmp = ns;
  pid->level = ns->level;

  for (i = ns->level; i >= 0; --i) {
    nr = alloc_pidmap(tmp);
    pid->numbers[i].nr = nr;
    pid->numbers[i].ns = tmp;
    tmp = tmp->parent;
  }

  for (type = 0; type < PIDTYPE_MAX; ++type) INIT_HLIST_HEAD(&pid->tasks[type]);

  upid = pid->numbers + ns->level;
  for (; upid >= pid->numbers; --upid) {
    hlist_add_head(&upid->pid_chain,
                   &pid_hashtable[pid_hashfn(upid->nr, upid->ns)]);
    upid->ns->nr_hashed++;
  }

  return pid;
}

#endif