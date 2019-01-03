#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/slab.h>

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
    dput(nd.dentry);
    return err;
}

u32 do_move_mount(struct nameidata *nd, const u8 *dev_name) {
    u32 err;

    return 0;
}

u32 do_new_mount(struct nameidata *nd, const u8 *type_page, const u8 *dev_name) {
    struct vfsmount *mnt;

    if (!type_page)
        return -EINVAL;

    // 此处应检查进程权限，省略

    mnt = do_kern_mount(type_page, dev_name);
    if (IS_ERR(mnt))
        return PTR_ERR(mnt);

    return do_add_mount(mnt, nd, NULL);
}

u32 do_add_mount(struct vfsmount *newmnt, struct nameidata *nd, struct list_head *fslist) {

}

struct vfsmount * alloc_vfsmnt(const u8 *name) {
    struct vfsmount *mnt;

    mnt = (struct vfsmount *)kmalloc(sizeof(struct vfsmount));
    if (mnt == 0)
        return ERR_PTR(-ENOMEM);

    
}

struct vfsmount * do_kern_mount(const u8 *fstype, const u8 *dev_name) {
    u32 err;
    struct vfsmount         *mnt;
    struct super_block      *sb;
    struct file_system_type *type;

    type = get_fs_type(fstype);
    sb = ERR_PTR(-ENOMEM);

    if (!type)
        return ERR_PTR(-ENODEV);

    mnt = alloc_vfsmnt(dev_name);
    if (IS_ERR(mnt))
        return mnt;



}