#include <zjunix/vfs/vfscache.h>

#include <zjunix/slab.h>

// 公用缓存
struct cache * dcache;
struct cache * pcache;

// 缓存操作函数
struct cache_operations dentry_cache_operations = {
    .look_up    = dcache_look_up,
    .add        = dcache_add,
    .is_full    = cache_is_full,
};

struct cache_operations page_cache_operations = {
    .look_up    = pcache_look_up,
    .add        = pcache_add,
    .is_full    = cache_is_full,
    .write_back = pcache_write_back,
};

// 初始化公用缓存区域
u32 init_cache() {

    // 初始化dcache
    dcache = (struct cache *)kmalloc(sizeof(struct cache));
    if (dcache == 0)
        goto init_cache_err;

    cache_init(dcache, DCACHE_CAPACITY, DCACHE_HASHTABLE_SIZE);
    dcache->c_op = &dentry_cache_operations;

    // 初始化pcache
    pcache = (struct cache *)kmalloc(sizeof(struct cache));
    if (pcache == 0)
        goto init_cache_err;

    cache_init(pcache, PCACHE_CAPACITY, PCACHE_HASHTABLE_SIZE);
    pcache->c_op = &page_cache_operations;

    return 0;

init_cache_err:
    if (dcache) {
        kfree(dcache->c_hashtable);
        kfree(dcache);
    }
    return -ENOMEM;
}

// 通用的高速缓存初始化方法
void cache_init(struct cache* this, u32 capacity, u32 tablesize) {
    u32 i;

    this->c_size = 0;
    this->c_capacity = capacity;
    this->c_tablesize = tablesize;
    INIT_LIST_HEAD(&(this->c_LRU));
    this->c_hashtable = (struct list_head *)kmalloc(tablesize * sizeof(struct list_head));
    for (i = 0; i < tablesize; i++)
        INIT_LIST_HEAD(this->c_hashtable + i);
    this->c_op = 0;
}

// 通用的高速缓存判断满方法
u32 cache_is_full(struct cache* this) {
    return this->c_size == this->c_capacity ? 1 : 0;
}

// 以下为内存清理函数
void release_dentry(struct dentry *dentry) {
    if (dentry) {
        list_del(&(dentry->d_LRU));
        list_del(&(dentry->d_hash));
        list_del(&(dentry->d_child));
        list_del(&(dentry->d_alias));
        list_del(&(dentry->d_subdirs));
        kfree(dentry);
        dcache->c_size -= 1;
    }
}

// 安全释放page，需要先释放页中存储的data，然后再释放页
void release_page(struct vfs_page* page) {
    kfree(page->p_data);
    kfree(page);
}

// 安全释放inode，需要从各种hash表中删除，然后释放inode中mapping信息，最后释放inode
void release_inode(struct inode * inode) {
    list_del(&(inode->i_hash));
    list_del(&(inode->i_LRU));
    list_del(&(inode->i_dentry));
    kfree(inode->i_data.a_page);
    kfree(inode);
}

// 输出当前存在缓存中的内容
void show_cache() {
    int i;
    struct list_head    *p;
    struct list_head    *start;
    struct dentry       *dentry;
    struct vfs_page     *page;

    kernel_printf("%s[VFS Cache]\n", quad1);

    kernel_printf("%s[dcache]\n", quad2);
    kernel_printf("%ssize: %d\n", quad3, dcache->c_size);

    start = &dcache->c_LRU;
    for (p = start->next; p != start; p = p->next) {
        dentry = container_of(p, struct dentry, d_LRU);
        kernel_printf("%s%s %d\n%sinode %x: sb: %s ino: %d\n", quad4, dentry->d_name.name, dentry->d_name.len,
                      quad5, dentry->d_inode, dentry->d_inode->i_sb->s_name, dentry->d_inode->i_ino);
    }

    kernel_printf("%s[pcache]\n", quad2);
    kernel_printf("%ssize: %d\n", quad3, pcache->c_size);

    start = &pcache->c_LRU;
    for (p = start->next; p != start; p = p->next) {
        page = container_of(p, struct vfs_page, p_LRU);
        kernel_printf("%sinode: %d read_page_no: %d\n", quad4,
                      page->p_mapping->a_host->i_ino, page->p_location);
    }

}