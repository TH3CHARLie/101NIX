#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/slab.h>

void path_release(struct nameidata *nd) {
    dput(nd->dentry);
//    mntput(nd->mnt);
}

static void detach_mnt(struct vfsmount *mnt, struct nameidata *old_nd) {
    old_nd->dentry = mnt->mnt_mountpoint;
    old_nd->mnt = mnt->mnt_parent;
    mnt->mnt_parent = mnt;
    mnt->mnt_mountpoint = mnt->mnt_root;
    //list_del_init(&mnt->mnt_child);
    list_del_init(&mnt->mnt_hash);
    old_nd->dentry->d_mounted--;
}

static void attach_mnt(struct vfsmount *mnt, struct nameidata *nd) {
    mnt->mnt_parent = mntget(nd->mnt);
    mnt->mnt_mountpoint = dget(nd->dentry);
    // TODO 弄清mnt_hash
    list_add(&mnt->mnt_hash, &nd->mnt->mnt_hash);
//    list_add_tail(&mnt->mnt_child, &nd->mnt->mnt_mounts);
    nd->dentry->d_mounted++;
}

u32 do_mount(const u8 *dev_name, const u8 *dir_name,
             const u8 *type_page, u32 flags) {
    u32 err;
    struct nameidata nd;

    // 目标目录不可为空
    if (!dir_name || !*dir_name)
        return -EINVAL;

    // 找到目标路径
    err = path_lookup(dir_name, LOOKUP_FOLLOW, &nd);
    if (err)
        return err;

    // 根据mount选项选择挂载模式
    if (flags & MS_MOVE)
        err = do_move_mount(&nd, dev_name);
    else
        err = do_new_mount(&nd, type_page, dev_name);

    // 释放路径
    path_release(&nd);
//    dput(nd.dentry);
    return err;
}

u32 do_move_mount(struct nameidata *nd, const u8 *old_name) {
    u32 err;
    struct nameidata old_nd, parent_nd;
    struct vfsmount *p;

    if (!old_name || !*old_name)
        return -EINVAL;

    err = path_lookup(old_name, LOOKUP_FOLLOW, &old_nd);
    if (err)
        return err;

    follow_mount(&nd->mnt, &nd->dentry);

//    while(d_mountpoint(nd->dentry) && follow_down(&nd->mnt, &nd->dentry))
//        ;
    // 命名空间检查
//    err = -EINVAL;
//    if (!check_mnt(nd->mnt) || !check_mnt(old_nd.mnt))
//        goto out;

//    err = -ENOENT;
//    down(&nd->dentry->d_inode->i_sem);
//    if (IS_DEADDIR(nd->dentry->d_inode))
//        goto out1;
//
//    spin_lock(&vfsmount_lock);
//    if (!IS_ROOT(nd->dentry) && d_unhashed(nd->dentry))
//        goto out2;

    err = -EINVAL;
    // 必须得是某个文件系统的根才能被移动
    if (old_nd.dentry != old_nd.mnt->mnt_root)
        goto out;
    // 全局root文件系统不能被移动
    if (old_nd.mnt == old_nd.mnt->mnt_parent)
        goto out;

    // 新挂载点的文件系统不可以是旧挂载点的子文件系统
    err = -ELOOP;
    for (p = nd->mnt; p->mnt_parent != p; p = p->mnt_parent)
        if (p == old_nd.mnt)
            goto out;

    err = 0;
    detach_mnt(old_nd.mnt, &parent_nd);
    attach_mnt(old_nd.mnt, nd);

    /* if the mount is moved, it should no longer be expire
     * automatically */
    //list_del_init(&old_nd.mnt->mnt_fslink);
    //out2:
    //spin_unlock(&vfsmount_lock);
    //out1:
    //up(&nd->dentry->d_inode->i_sem);
out:
    //up_write(&current->namespace->sem);
    if (!err)
        path_release(&parent_nd);
    path_release(&old_nd);

    return err;
}

u32 do_new_mount(struct nameidata *nd, const u8 *type_page, const u8 *dev_name) {
    struct vfsmount *mnt;

    if (!type_page)
        return -EINVAL;

    // 此处应检查进程权限，省略

    mnt = do_kern_mount(type_page, dev_name);
    if (IS_ERR(mnt))
        return PTR_ERR(mnt);

    return do_add_mount(mnt, nd);
}

u32 do_add_mount(struct vfsmount *newmnt, struct nameidata *nd) {
    u32 err;

    // 确保被挂载点切换到合法的dentry(此时可能已经挂载了其他文件系统)
    // 此处的操作实际上导致了父文件系统中该dentry上只有最后一次被挂载的子文件系统能被访问到
    follow_mount(&nd->mnt, &nd->dentry);

    // 此处应检查命名空间，省略

    // 某个根挂载根本身的情况
    if (nd->mnt->mnt_sb == newmnt->mnt_sb && nd->mnt->mnt_root == nd->dentry)
        return -EBUSY;

    // 此处应检查链接类型文件，省略

    return graft_tree(newmnt, nd);
}

// TODO 挂载计数
static inline struct vfsmount *mntget(struct vfsmount *mnt) {
//    if (mnt)
//        mnt->mnt_count++;
    return mnt;
}

static u32 graft_tree(struct vfsmount *mnt, struct nameidata *nd) {
    u32 err;
    struct list_head head;

//    if (S_ISDIR(nd->dentry->d_inode->i_mode) !=
//        S_ISDIR(mnt->mnt_root->d_inode->i_mode))
//        return -ENOTDIR;

//    err = -ENOENT;
//    down(&nd->dentry->d_inode->i_sem);
//    if (IS_DEADDIR(nd->dentry->d_inode))
//        goto out_unlock;
//
//    err = security_sb_check_sb(mnt, nd);
//    if (err)
//        goto out_unlock;

    err = -ENOENT;
//    spin_lock(&vfsmount_lock);
    //if (IS_ROOT(nd->dentry)) {

    attach_mnt(mnt, nd);

    // mamespace处理
    //list_add_tail(&head, &mnt->mnt_list);
    //list_splice(&head, current->namespace->list.prev);

    mntget(mnt);
    err = 0;

   // }
//    spin_unlock(&vfsmount_lock);
//    out_unlock:
//    up(&nd->dentry->d_inode->i_sem);
//    if (!err)
//        security_sb_post_addmount(mnt, nd);
    return err;
}

// 此函数可以整合
struct vfsmount * alloc_vfsmnt(const u8 *name) {
    struct vfsmount *mnt;

    mnt = (struct vfsmount *)kmalloc(sizeof(struct vfsmount));
    if (mnt == 0)
        return ERR_PTR(-ENOMEM);

    kernel_memset(mnt, 0, sizeof(struct vfsmount));
    INIT_LIST_HEAD(&mnt->mnt_hash);

    return mnt;
}

// 获得fstype这个文件系统下dev_name这个设备的一个新的挂载信息
struct vfsmount * do_kern_mount(const u8 *fstype, const u8 *dev_name) {
    u32 err;
    struct vfsmount         *mnt;
    struct super_block      *sb;
    struct file_system_type *type;

    type = get_fs_type(fstype);
    sb = ERR_PTR(-ENOMEM);

    if (!type)
        return ERR_PTR(-ENODEV);

    // 初始化一个vfsmount
    mnt = alloc_vfsmnt(dev_name);
    if (IS_ERR(mnt))
        return mnt;

    // 此时需要获得该文件系统对应的超级块
    // TODO get_sb 可以扩展成带初始化的函数
    sb = get_sb(type, dev_name);
    if (IS_ERR(sb)) {
        kfree(mnt);
        return (struct vfsmount *)sb;
    }

    // 填充对应的信息
    mnt->mnt_sb = sb;
    mnt->mnt_parent = mnt;
    mnt->mnt_root = dget(sb->s_root);
    mnt->mnt_mountpoint = sb->s_root;

    return mnt;
}