#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/errno.h>
#include <zjunix/lock.h>
#include <zjunix/list.h>

#include <zjunix/utils.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/log.h>

// 指向注册过的`file_system_type`单向链表表头
extern struct file_system_type * file_systems;

// 查找`file_systems`链表中是否已经存在该文件系统
static struct file_system_type ** find_filesystem(const u8 *name) {
    struct file_system_type ** p;

    for (p = &file_systems; *p; p = &(*p)->next)
        if (kernel_strcmp((*p)->name, name) == 0)
            break;
    
    return p;
}

// 注册文件系统函数。将其链接在全局`file_systems`的链表中
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

    // 找到`file_systems`中第一个空指针位置
    p = find_filesystem(fs->name);

    // 如果已经注册，则返回错误代码；否则存入
    if (*p)
        res = -EBUSY;
    else
        *p = fs;
    
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

struct file_system_type *get_fs_type(const u8 *name) {
    return *(find_filesystem(name));
}

void print_file_systems() {
    struct file_system_type ** p;

    kernel_printf("file_systems:\n");
    for (p = &file_systems; *p; p = &(*p)->next) {
        kernel_printf("    %x: %s\n", p, (*p)->name);
    }
}
