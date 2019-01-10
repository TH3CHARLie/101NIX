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
struct file_system_type * register_filesystem(struct file_system_type * fs) {
    struct file_system_type * res;
    struct file_system_type ** p;
    u32 err;

    if (!fs) {
        err = -EINVAL;
        goto fail;
    }
    if (fs->next) {
        err = -EBUSY;
        goto fail;
    }

    INIT_LIST_HEAD(&fs->fs_supers);
    // TODO: lock,
    // lockup(&file_systems_lock);

    // 找到`file_systems`中第一个空指针位置
    p = find_filesystem(fs->name);

    // 如果已经注册，则返回错误代码；否则存入
    if (*p) {
        kernel_printf("    [WARN]: File system has already registered\n");
    }
    else {
        kernel_printf("    [ OK ]: Successfully registered\n");
        *p = fs;
    }
    res = *p;
    // TODO: unlock
    // unlock(&file_systems_lock);
    return res;

    fail:
    kernel_printf_vfs_errno(err);
    return fs;
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

// TODO 可以用此函数替换get_sb
struct super_block *get_sb_bdev(struct file_system_type *fs_type,
                                int flags, const char *dev_name, void *data,
                                int (*fill_super)(struct super_block *, void *, int)) {

    return NULL;
}

struct super_block * get_sb(struct file_system_type * fs, const u8 * name) {
    struct list_head    *p;
    struct list_head    *start;
    struct super_block  *sb;

    start = &fs->fs_supers;
    for (p = start->next; p != start; p = p->next) {
        sb = container_of(p, struct super_block, s_instances);
        if (kernel_strcmp(sb->s_name, name) == 0)
            return sb;
    }

    return ERR_PTR(-EINVAL);
}

struct file_system_type *get_fs_type(const u8 *name) {
    return *(find_filesystem(name));
}

void print_file_systems() {
    struct list_head        *  start, * tmp;
    struct super_block      *  sb;
    struct file_system_type ** p;

    kernel_printf("file_systems:\n");
    for (p = &file_systems; *p; p = &(*p)->next) {
        kernel_printf("    %x: %s ", p, (*p)->name);
        start = &((*p)->fs_supers);
        for (tmp = start->next; tmp != start; tmp = tmp->next) {
            sb = container_of(tmp, struct super_block, s_instances);
            if (sb)
                kernel_printf(" %s ", sb->s_name);
            else
                kernel_printf(" wrong! ");
        }
        kernel_printf("\n");
    }
}
