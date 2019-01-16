#include <zjunix/pc.h>
#include <zjunix/pid.h>
#include <zjunix/slab.h>
#define PID_MAX_HASH_ENTRY 4

struct hlist_head pid_hashtable[PID_MAX_HASH_ENTRY];

// our hash function
unsigned int pid_hash_function(pid_t nr, struct pid_namespace *ns) {
  return (nr + ns->level) % PID_MAX_HASH_ENTRY;
}

void init_pid_module() {
  int i = 0;
  for (i = 0; i < PID_MAX_HASH_ENTRY; ++i) {
    INIT_HLIST_HEAD(&pid_hashtable[i]);
  }
}
// static inline struct pid *task_pid(struct task_struct *task) {
//   return task->pids[PIDTYPE_PID].pid;
// }

// static inline struct pid *task_tgid(struct task_struct *task) {
//   return task->group_leader->pids[PIDTYPE_PID].pid;
// }

// static inline struct pid *task_pgid(struct task_struct *task) {
//   return task->group_leader->pids[PIDTYPE_PGID].pid;
// }

// static inline pid_t pid_nr_ns(struct pid *pid, struct pid_namespace *ns) {
//   struct upid *upid;
//   pid_t nr = 0;
//   if (pid && ns->level <= pid->level) {
//     upid = &pid->numbers[ns->level];
//     if (upid->ns == ns) {
//       nr = upid->nr;
//     }
//   }
//   return nr;
// }

// pid_t get_pid_val_by_task_ns(struct task_struct *task,
//                              struct pid_namespace *ns) {
//   return pid_nr_ns(task_pid(task), ns);
// }

// pid_t get_tgid_val_by_task_ns(struct task_struct *task,
//                               struct pid_namespace *ns) {
//   return pid_nr_ns(task_tgid(task), ns);
// }

// pid_t get_pgid_val_by_task_ns(struct task_struct *task,
//                               struct pid_namespace *ns) {
//   return pid_nr_ns(task_pgid(task), ns);
// }

// struct pid *get_pid_by_pid_val_ns(pid_t nr, struct pid_namespace *ns) {
//   struct hlist_node *elem;
//   struct upid *upid;
//   struct hlist_head *head = &(pid_hashtable[pid_hash_function(nr, ns)]);
//   hlist_for_each_entry(upid, head, pid_chain) {
//     if (upid && upid->nr == nr && upid->ns == ns) {
//       return container_of(upid, struct pid, numbers[ns->level]);
//     }
//   }
//   return NULL;
// }

// struct task_struct *get_task_by_pid(struct pid *pid, enum pid_type type) {
//   struct task_struct *result = NULL;
//   if (pid) {
//     struct hlist_node *first;
//     first = pid->tasks[type].first;
//     if (first) {
//       result = hlist_entry(first, struct task_struct, pids[(type)].node);
//     }
//   }
//   return result;
// }

bool pid_namespace_empty(struct pid_namespace *ns) {
  return ns->pidmap.nr_free == PIDMAP_MAX_ENTRY;
}

bool pid_namespace_full(struct pid_namespace *ns) {
  return ns->pidmap.nr_free == 0;
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
    return false;
  else
    return true;
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
    if (res >= PIDMAP_MAX_ENTRY) {
      res %= PIDMAP_MAX_ENTRY;
    }
    index = res >> 3;
    offset = res & 0x07;
    kernel_printf("index=%d, offset=%d\n", index, offset);
    if (pidmap_id_is_valid(index, offset, map)) {
      pidmap_id_alloc(index, offset, map);
      kernel_printf("[pid_alloc] res=%d\n", res);
      ns->last_pid = res;
      return res;
    } else {
      ++res;
    }
  }
}

static void free_pid_val_from_ns(pid_t val, struct pid_namespace *ns) {
  struct pidmap *map = &(ns->pidmap);
  if (pid_namespace_empty(ns) || !map) {
    return;
  }
  int index = val >> 3;
  int offset = val & 0x07;
  if (!pidmap_id_is_valid(index, offset, map)) {
    pidmap_id_free(index, offset, map);
  }
}

struct pid_namespace *pid_namespace_create(struct pid_namespace *parent) {
  struct pid_namespace *res_namespace =
      (struct pid_namespace *)kmalloc(sizeof(struct pid_namespace));
  if (parent == NULL) {
    res_namespace->level = 0;
  } else {
    res_namespace->level = parent->level + 1;
  }
  if (res_namespace->level >= MAX_PID_NAMESPACE_CNT) {
    return NULL;
  }
  res_namespace->parent = parent;
  init_pidmap(&res_namespace->pidmap);
  res_namespace->last_pid = -1;
  return res_namespace;
}

// pid_t alloc_pid_nr_from_ns(struct pid_namespace *ns) {
//   enum pid_type type;
//   int i, nr;
//   struct pid_namespace *tmp;
//   struct upid *upid;

//   tmp = ns;
//   pid->level = ns->level;

//   for (i = ns->level; i >= 0; --i) {
//     nr = alloc_pidmap(tmp);
//     pid->numbers[i].nr = nr;
//     pid->numbers[i].ns = tmp;
//     tmp = tmp->parent;
//   }

//   for (type = 0; type < PIDTYPE_MAX; ++type)
//   INIT_HLIST_HEAD(&pid->tasks[type]);

//   upid = pid->numbers + ns->level;
//   for (; upid >= pid->numbers; --upid) {
//     hlist_add_head(&upid->pid_chain,
//                    &pid_hashtable[pid_hashfn(upid->nr, upid->ns)]);
//     upid->ns->nr_hashed++;
//   }
//   return pid;
// }

// assign a process with new pid from ns
// it will pick next avaliable pid value from ns's bitmap as this level's pid_val
int assign_real_pid_from_ns(struct task_struct *task,
                            struct pid_namespace *ns) {
  if (!ns) {
    kernel_printf(
        "[pid][assign_real_pid_from_ns]: fatal, ns should not be NULL\n");
    return 1;
  }
  int i;
  struct pid_namespace *tmp_ns = ns;
  task->real_pid.level = ns->level;
  task->real_pid.task_ptr = task;
  for (i = ns->level; tmp_ns != NULL && i >= 0; --i) {
    task->real_pid.numbers[i].nr = alloc_pid_val_from_ns(tmp_ns);
    kernel_printf(
        "[pid][assign_real_pid_from_ns]: alloc id %d in namespace %d\n",
        task->real_pid.numbers[i].nr, tmp_ns->level);
    if (task->real_pid.numbers[i].nr == -1) {
      kernel_printf(
          "[pid][assign_real_pid_from_ns]: cannot assign new pid in namespace "
          "%d\n",
          tmp_ns->level);
      return 1;
    }
    task->real_pid.numbers[i].ns = tmp_ns;
    hlist_add_head(&task->real_pid.numbers[i].pid_chain,
                   &pid_hashtable[pid_hash_function(
                       task->real_pid.numbers[i].nr, tmp_ns)]);
    tmp_ns = ns;
  }
  return 0;
}

// free a real_pid
// used in task_kill
int free_real_pid(struct task_struct *task) {
  int level = task->real_pid.level;
  struct pid_namespace *ns = NULL;
  for (; level >= 0; --level) {
    ns = task->real_pid.numbers[level].ns;
    if (ns == NULL) {
      kernel_printf("[pid][free_real_pid]: ns should not be NULL\n");
    }
    free_pid_val_from_ns(task->real_pid.numbers[level].nr, ns);
    hlist_del_init(&(task->real_pid.numbers[level].pid_chain));
  }
}