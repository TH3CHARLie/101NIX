#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/slab.h>
#include <driver/vga.h>

// 分配一个新的vfsmount对象
struct vfsmount * alloc_vfsmnt(const u8 *name) {
    struct vfsmount *mnt;

#ifdef DEBUG_VFS
    kernel_printf("now in alloc_vfsmnt(%s)\n", name);
#endif

    mnt = (struct vfsmount *)kmalloc(sizeof(struct vfsmount));
    if (mnt == 0)
        return ERR_PTR(-ENOMEM);

    kernel_memset(mnt, 0, sizeof(struct vfsmount));

    INIT_LIST_HEAD(&mnt->mnt_hash);
    INIT_LIST_HEAD(&mnt->mnt_child);
    INIT_LIST_HEAD(&mnt->mnt_mounts);

    return mnt;
}

// 遍历当前文件系统对象的mount链表，找到挂载点，然后更换文件系统对象
struct vfsmount * lookup_mnt(struct vfsmount *mnt, struct dentry *dentry) {
    struct list_head *head = &(mnt->mnt_hash);
    struct list_head *tmp = head;
    struct vfsmount *p, *found = 0;

#ifdef DEBUG_VFS
    kernel_printf("now_in_lookup_mnt\n");
#endif

    // 在字段为hash的双向链表寻找。这里有所有已安装的文件系统的对象
    // 这里并没有为其实现hash查找，仅普通链表
    for (;;) {
        tmp = tmp->next;
        if (tmp == head)
            break;
        p = list_entry(tmp, struct vfsmount, mnt_hash);
#ifdef DEBUG_VFS
        kernel_printf("    %x %s\n", p, p->mnt_sb->s_name);
#endif
        if (p && p->mnt_parent == mnt && p->mnt_mountpoint && p->mnt_mountpoint->d_inode &&
                p->mnt_mountpoint->d_inode->i_ino == dentry->d_inode->i_ino) {
            found = p;
            break;
        }
    }

#ifdef DEBUG_VFS
    kernel_printf("    found: %x\n", found);
#endif

    return found;
}
