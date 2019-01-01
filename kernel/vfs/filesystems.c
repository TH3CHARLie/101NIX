#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/errno.h>
#include <zjunix/lock.h>
#include <zjunix/list.h>

#include <zjunix/utils.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/log.h>

extern struct file_system_type * file_systems;

static struct file_system_type **find_filesystem(const char *name) {
    struct file_system_type ** p;

    for (p = &file_systems; *p; p = &(*p)->next)
        if (kernel_strcmp((*p)->name, name) == 0)
            break;
    
    return p;
}

u32 register_filesystem(struct file_system_type * fs) {
    u32 res = 0;
    struct file_system_type ** p;

    if (!fs)
        return -EINVAL;
    if (fs->next)
        return -EBUSY;

    INIT_LIST_HEAD(&fs->fs_supers);
    // TODO: lock, 
    // lockup(&file_systems_lock);
    p = find_filesystem(fs->name);
    if (*p) {
        res = -EBUSY;
    } else {
        *p = fs;
    }
    
    // TODO: unlock
    // unlock(&file_systems_lock);
    return res;
}

u32 unregister_filesystem(struct file_system_type * fs) {
    struct file_system_type ** tmp;

    // TODO: lock, 
    // lockup(&file_systems_lock);
    for (tmp = &file_systems; *tmp; tmp = &(*tmp)->next) {
        if (fs == *tmp) {
            *tmp = fs->next;
            fs->next = NULL;
            // TODO: unlock
            // unlock(&file_systems_lock);
            return 0;
        }
    }
    
    // TODO: unlock
    // unlock(&file_systems_lock);
    return -EINVAL;
}