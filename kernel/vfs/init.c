#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/slab.h>

extern struct cache * dcache;

struct dentry * alloc_dentry() {
    struct dentry * dentry;

    dentry = (struct dentry *)kmalloc(sizeof(struct dentry));
    if (dentry == 0)
        return ERR_PTR(-ENOMEM);

    dentry->d_count       = 0;
    dentry->d_pinned      = D_UNPINNED;
    dentry->d_mounted     = 0;
    dentry->d_inode       = 0;
    dentry->d_parent      = 0;
    dentry->d_name.name   = 0;
    dentry->d_name.len    = 0;
    dentry->d_sb          = 0;
    dentry->d_op          = 0;
    INIT_LIST_HEAD(&(dentry->d_hash));
    INIT_LIST_HEAD(&(dentry->d_LRU));
    INIT_LIST_HEAD(&(dentry->d_subdirs));
    INIT_LIST_HEAD(&(dentry->d_child));
    INIT_LIST_HEAD(&(dentry->d_alias));

    dcache->c_op->add(dcache, (void*)dentry);

    return dentry;
}

struct inode * alloc_inode(struct super_block * sb) {
    struct inode * inode;

    inode = (struct inode *)kmalloc(sizeof(struct inode));
    if (inode == 0)
        return ERR_PTR(-ENOMEM);
    kernel_memset(inode, 0, sizeof(struct inode));

    inode->i_count     = 0;
    inode->i_blocks    = 0;
    inode->i_ino       = 0;
    inode->i_op        = 0;
    inode->i_fop       = 0;
    inode->i_sb        = sb;
    inode->i_blksize   = sb->s_blksize;
    inode->i_size      = 0;
    inode->i_mode      = 0;
    INIT_LIST_HEAD(&(inode->i_hash));
    INIT_LIST_HEAD(&(inode->i_LRU));
    INIT_LIST_HEAD(&(inode->i_dentry));

    switch (inode->i_blksize) {
        case 1024: inode->i_blkbits = 10; break;
        case 2048: inode->i_blkbits = 11; break;
        case 4096: inode->i_blkbits = 12; break;
        case 8192: inode->i_blkbits = 13; break;
        default: return ERR_PTR(-EFAULT);
    }

    return inode;
}

struct vfs_page * alloc_vfspage(u32 location, struct address_space *mapping) {
    struct vfs_page *page;

    page = (struct vfs_page *)kmalloc(sizeof(struct vfs_page));
    if (page == 0)
        return ERR_PTR(-ENOMEM);
    kernel_memset(page, 0, sizeof(struct vfs_page));

    page->p_state    = P_CLEAR;
    page->p_location = location;
    page->p_mapping  = mapping;
    INIT_LIST_HEAD(&(page->p_hash));
    INIT_LIST_HEAD(&(page->p_LRU));
    INIT_LIST_HEAD(&(page->p_list));

    u32 err = page->p_mapping->a_op->readpage(page);
    if (IS_ERR_VALUE(err)) {
        release_page(page);
        return ERR_PTR(err);
    }

    return page;
}