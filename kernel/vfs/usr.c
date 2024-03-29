#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>

#include <driver/vga.h>
#include <zjunix/utils.h>
#include <zjunix/slab.h>

// 外部变量
extern struct cache                     * dcache;
extern struct cache                     * icache;
extern struct cache                     * pcache;
extern struct dentry                    * pwd_dentry;
extern struct vfsmount                  * pwd_mnt;

// 调用挂载的接口
u32 vfs_mount(char *a, char *b, char *c, int mode) {
    do_mount(a, b, c, mode);
}

// 调用卸载的接口
u32 vfs_umount(const u8 *dir_name) {
    do_umount(dir_name);
}

// 调用创建目录的接口，第一个参数是父目录，第二个参数是要新建的目录
u32 vfs_mkdir(struct inode *dir, struct dentry *dentry, int mode) {

    if (!dir->i_op || !dir->i_op->mkdir)
        return -EPERM;

#ifdef DEBUG_VFS
    kernel_printf("now in vfs_mkdir(): dentry: %s\n", dentry->d_name.name);
#endif

    mode &= S_ISVTX;
    u32 err = dir->i_op->mkdir(dir, dentry, mode);

    return err;
}

// 调用创建文件的接口，第一个参数是父目录，第二个参数是要新建的目录
u32 vfs_create(struct inode *dir, struct dentry *dentry, int mode) {

    if (!dir->i_op || !dir->i_op->create)
        return -EPERM;

#ifdef DEBUG_VFS
    kernel_printf("now in vfs_create(): dentry: %s\n", dentry->d_name.name);
#endif

    mode &= S_ISVTX;
    u32 err = dir->i_op->create(dir, dentry, mode);

    return err;
}

// 输出文件的内容
u32 vfs_cat(const u8 *path){
    u8 *buf;
    u32 err;
    u32 base;
    u32 file_size;
    struct file *file;
    
    // 调用VFS提供的打开接口
    file = vfs_open(path, O_RDONLY, 0);
    if (IS_ERR_OR_NULL(file)) {
        kernel_printf_vfs_errno(PTR_ERR(file));
        return PTR_ERR(file);
    }
    
    // 接下来读取文件数据区的内容到buf
    base = 0;
    file_size = file->f_dentry->d_inode->i_size;
    kernel_printf("file_size: %d\n", file_size);
    
    buf = (u8*) kmalloc (file_size + 1);
    if (vfs_read(file, buf, file_size, &base) != file_size)
        return 1;

    // 打印buf里面的内容
    buf[file_size] = 0;
    kernel_printf("%s\n", buf);

    // 关闭文件并释放内存
    err = vfs_close(file);
    if (err)
        return err;
    
    kfree(buf);
    return 0;
}

// 更改当前的工作目录
u32 vfs_cd(const u8 *path) {
    u32 err;
    struct nameidata nd;

    // 调用VFS提供的查找接口
    err = path_lookup(path, LOOKUP_DIRECTORY, &nd);
    if (IS_ERR_VALUE(err)) {
        kernel_printf_vfs_errno(err);
        return 0;
    }

    // 若成功则改变相应的dentry和mnt
    pwd_dentry = nd.dentry;
    pwd_mnt = nd.mnt;

    return 0;
}

// 输出文件夹的内容
u32 vfs_ls(const u8 *path) {
    u32 i;
    u32 err;
    struct file *file;
    struct getdent getdent;

    // 调用VFS提供的打开接口
    if (path[0] == 0)
        file = vfs_open(".", LOOKUP_DIRECTORY, 0);
    else
        file = vfs_open(path, LOOKUP_DIRECTORY, 0);

    if (IS_ERR_OR_NULL(file)) {
        if (PTR_ERR(file) == -ENOENT)
            kernel_printf("Directory not found!\n");
        else
            kernel_printf("Other error: %d\n", -PTR_ERR(file));
        return PTR_ERR(file);
    }
    
    // 调用具体文件系统的readir函数
    err = file->f_op->readdir(file, &getdent);
    if (err)
        return err;
    
    // 接下来往屏幕打印结果
    for (i = 0; i < getdent.count; i++) {
        if (getdent.dirent[i].type == FT_DIR)
            kernel_puts(getdent.dirent[i].name, VGA_YELLOW, VGA_BLACK);
        else if(getdent.dirent[i].type == FT_REG_FILE)
            kernel_puts(getdent.dirent[i].name, VGA_WHITE, VGA_BLACK);
        else
            kernel_puts(getdent.dirent[i].name, VGA_GREEN, VGA_BLACK);
        kernel_printf(" ");
    }
    kernel_printf("\n");

    return 0;
}

// 删除一个文件（不能是文件夹）
u32 vfs_rm(const u8 *path){
    u32 err;
    struct inode        *inode;
    struct dentry       *dentry;
    struct nameidata    nd;

    // 调用VFS提供的查找接口
    err = path_lookup(path, 0, &nd);
    if (IS_ERR_VALUE(err)) {
        if (err == -ENOENT)
            kernel_printf("File not found!\n");
        return err;
    }
    
    // 先删除inode对应文件在外存上的相关信息
    dentry = nd.dentry;
    err = dentry->d_inode->i_sb->s_op->delete_inode(dentry);
    if (err)
        return err;

    // 最后只需要在缓存中删去inode即可，page和dentry都允许保留
    dentry->d_inode = 0;

    return 0;
}

// 调用namei里的__lookup_hash
struct dentry * lookup_hash(struct qstr *name, struct dentry * base) {
    return __lookup_hash(name, base, NULL);
}

// 根据nd的结果创建一个新的dentry对象
struct dentry * lookup_create(struct nameidata *nd, int is_dir) {
    struct dentry *dentry;

    // 检查nd参数是否正确
    dentry = ERR_PTR(-EEXIST);
    if (nd->last_type != LAST_NORM)
        goto fail;

    // 去掉nd的flag，尝试找一下dentry；如果找不到，lookup_hash函数会创建一个新的dentry
    nd->flags &= ~LOOKUP_PARENT;
    dentry = lookup_hash(&nd->last, nd->dentry);

#ifdef DEBUG_VFS
    kernel_printf("now in lookup_create(%s, %s) %x\n", nd->last.name, nd->dentry->d_name.name, dentry);
#endif

    if (IS_ERR(dentry))
        goto fail;

    // 目录或文件不存在的情况
    if (!is_dir && nd->last.name[nd->last.len] && !dentry->d_inode)
        goto enoent;

#ifdef DEBUG_VFS
    kernel_printf("now in lookup_create(%s, %s)\n", nd->last.name, nd->dentry->d_name.name);
#endif

    return dentry;

enoent:
    dput(dentry);
    dentry = ERR_PTR(-ENOENT);
fail:
    return dentry;
}

// 创建目录的用户接口
// 首先会解析路径，然后生成新的dentry，交给vfs_mkdir处理
u32 sys_mkdir(const u8* path, u32 mode) {
    u32 err;

    struct dentry *dentry;
    struct nameidata nd;

    // 首先解析目录，使用LOOKUP_PARENT模式，会储存倒数第二层目录
    err = path_lookup(path, LOOKUP_PARENT, &nd);
    if (err) {
        kernel_printf_vfs_errno(err);
        return 0;
    }

#ifdef DEBUG_VFS
    kernel_printf("[sys_mkdir]: now get path:\n");
    kernel_printf("     nd_dentry (father): %s\n", nd.dentry->d_name.name);
    kernel_printf("     nd_last   (newdir): %s\n\n", nd.last.name);
#endif

    // 创建新dentry
    dentry = lookup_create(&nd, 1);
    if (!IS_ERR(dentry)) {
        // 在磁盘上创建目录项
        err = vfs_mkdir(nd.dentry->d_inode, dentry, mode);
        if (err)
            kernel_printf_vfs_errno(err);
        dput(dentry);
    }
    else {
        kernel_printf_vfs_errno(PTR_ERR(dentry));
    }

    // 注意释放目录
    path_release(&nd);
    return 0;
}

// 创建文件的用户接口
// 首先会解析路径，然后生成新的dentry，交给vfs_touch处理
u32 sys_create(const u8* path, u32 mode) {
    u32 err;

    struct dentry *dentry;
    struct nameidata nd;

    err = path_lookup(path, LOOKUP_PARENT, &nd);
    if (err) {
        kernel_printf_vfs_errno(err);
        return 0;
    }

#ifdef DEBUG_VFS
    kernel_printf("[sys_create]: now get path:\n");
    kernel_printf("     nd_dentry (father): %s\n", nd.dentry->d_name.name);
    kernel_printf("     nd_last   (newdir): %s\n\n", nd.last.name);
#endif

    dentry = lookup_create(&nd, 1);
    if (!IS_ERR(dentry)) {
        err = vfs_create(nd.dentry->d_inode, dentry, mode);
        if (err)
            kernel_printf_vfs_errno(err);
        dput(dentry);
    }
    else {
        kernel_printf_vfs_errno(PTR_ERR(dentry));
    }

    path_release(&nd);
    return 0;
}


u32 may_delete(struct inode *dir, struct dentry * victim, int isdir) {
    u32 error;

    if (!victim->d_inode)
        return -ENOENT;

//    BUG_ON(victim->d_parent->d_inode != dir);
//
//    error = permission(dir,MAY_WRITE | MAY_EXEC, NULL);
//    if (error)
//        return error;
//    if (IS_APPEND(dir))
//        return -EPERM;
//    if (check_sticky(dir, victim->d_inode)||IS_APPEND(victim->d_inode)||
//        IS_IMMUTABLE(victim->d_inode))
//        return -EPERM;
//    if (isdir) {
//        if (!S_ISDIR(victim->d_inode->i_mode))
//            return -ENOTDIR;
//        if (IS_ROOT(victim))
//            return -EBUSY;
//    } else if (S_ISDIR(victim->d_inode->i_mode))
//        return -EISDIR;
//    if (IS_DEADDIR(dir))
//        return -ENOENT;
//    if (victim->d_flags & DCACHE_NFSFS_RENAMED)
//        return -EBUSY;
    return 0;
}

u32 vfs_rmdir(struct inode *dir, struct dentry *dentry) {
    u32 err;

    err = may_delete(dir, dentry, 1);
    if (err)
        return err;

    if (!dir->i_op || !dir->i_op->rmdir)
        return -EPERM;

//    dentry_unhash(dentry);
//    if (d_mountpoint(dentry))
//        error = -EBUSY;
//    else {
//        error = dir->i_op->rmdir(dir, dentry);
//        if (!error)
//            dentry->d_inode->i_flags |= S_DEAD;
//    }
//
//    if (!error) {
//        inode_dir_notify(dir, DN_DELETE);
//        d_delete(dentry);
//    }
//    dput(dentry);

    return err;
}

u32 sys_rmdir(const u8 * pathname) {
    u32 error = 0;
    const u8 * name;
    struct dentry *dentry;
    struct nameidata nd;

    name = pathname;
    if(IS_ERR(name))
        return PTR_ERR(name);

    error = path_lookup(name, LOOKUP_PARENT, &nd);
    if (error)
        goto exit;

    switch(nd.last_type) {
    case LAST_DOTDOT:
        error = -EINVAL;
        goto exit1;
    case LAST_DOT:
        error = -EINVAL;
        goto exit1;
    case LAST_ROOT:
        error = -EBUSY;
        goto exit1;
    default:
        break;
    }

    dentry = lookup_hash(&nd.last, nd.dentry);
    error = PTR_ERR(dentry);
    if (!IS_ERR(dentry)) {
        error = vfs_rmdir(nd.dentry->d_inode, dentry);
        dput(dentry);
    }

exit1:
    path_release(&nd);
exit:
    return error;
}

// 文件关闭入口
u32 vfs_close(struct file *filp) {
    u32 err;

    // 把页高速缓存的数据（如果需要）全部写回磁盘
    err = filp->f_op->flush(filp);
    if (!err)
        kfree(filp);

    return err;
}

// 虚拟文件系统读接口
u32 vfs_read(struct file *file, char *buf, u32 count, u32 *pos) {
    u32 ret;

    if (!(file->f_mode & FMODE_READ))
        return -EBADF;
    if (!file->f_op || (!file->f_op->read))
        return -EINVAL;

    ret = file->f_op->read(file, buf, count, pos);

    return ret;
}

// 虚拟文件系统写接口
u32 vfs_write(struct file *file, char *buf, u32 count, u32 *pos) {
    u32 ret;

    if (!(file->f_mode & FMODE_WRITE))
        return -EBADF;
    if (!file->f_op || (!file->f_op->write))
        return -EINVAL;

    ret = file->f_op->write(file, buf, count, pos);
    return ret;
}

// 文件打开函数
struct file* vfs_open(const u8 *filename, u32 flags, u32 mode){
    u32 namei_flags;
    u32 err;
    struct file         *f;
    struct nameidata    nd;

    // 重置flags，因为内外部定义有些地方不一致
    namei_flags = flags;
    if ((namei_flags + 1) & O_ACCMODE)
        namei_flags ++;

    // 逐层解析路径，并把文件关联的dentry和vfsmount对象保存在nd结构中
    err = open_namei(filename, namei_flags, mode, &nd);

    // 用得到的nd结构初始化file对象，并返回
    if (!err)
        return dentry_open(nd.dentry, nd.mnt, flags);

    return ERR_PTR(err);
}