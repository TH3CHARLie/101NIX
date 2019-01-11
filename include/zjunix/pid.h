#ifndef _101NIX_PID_H_
#define _101NIX_PID_H_
#include <zjunix/list.h>

#define MAX_PID_NAMESPACE_CNT 5
// ignore session id
// since session is often used as login
// no login happens in our OS
enum pid_type { PIDTYPE_PID, PIDTYPE_PGID, PIDTYPE_MAX };

// pid_t is actullay a int
// for quick hash
typedef int pid_t;

struct upid {
    int nr;
    struct pid_namespace *ns;
    struct hlist_node pid_chain;
};


// tasks[PIDTYPE_PID] points to the process own it
// tasks[PIDTYPE_PGID] points to process group's plink[]
struct pid {
    // int count;
    int level;
    struct task_struct* task_ptr;
    struct upid numbers[MAX_PID_NAMESPACE_CNT];
};

// struct pid_link {
//     struct hlist_node node;
//     struct pid *pid;
// };

struct task_struct;

#define PIDMAP_MAX_ENTRY 16
#define PIDMAP_BYTE ((PIDMAP_MAX_ENTRY + 7) >> 3)
struct pidmap {
    int nr_free;
    char data[PIDMAP_BYTE];
};

struct pid_namespace {
    unsigned int level;
    pid_t last_pid;
    struct pidmap pidmap;
    struct pid_namespace* parent;
};

void init_pid_module();

pid_t get_pid_val_by_task_ns(struct task_struct *task, struct pid_namespace* ns);

pid_t get_tgid_val_by_task_ns(struct task_struct *task, struct pid_namespace* ns);

pid_t get_pgid_val_by_task_ns(struct task_struct *task, struct pid_namespace* ns);

struct pid *get_pid_by_pid_val_ns(pid_t pid_val, struct pid_namespace *ns);

struct task_struct* get_task_by_pid(struct pid *pid, enum pid_type type);

unsigned int pid_hash_function(pid_t nr, struct pid_namespace* ns);

bool pid_namespace_empty(struct pid_namespace* ns);

bool pid_namespace_full(struct pid_namespace* ns);

void init_pidmap(struct pidmap* pidmap);

pid_t alloc_pid_nr_from_ns(struct pid_namespace* ns);

struct pid_namespace* pid_namespace_create(struct pid_namespace *ns);

int assign_real_pid_from_ns(struct task_struct *task,
                            struct pid_namespace *ns);

int free_real_pid(struct task_struct *task);

#endif