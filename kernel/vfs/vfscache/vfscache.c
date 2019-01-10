#include <zjunix/vfs/vfscache.h>

#include <zjunix/slab.h>

// 公用缓存
struct cache * dcache;
struct cache * pcache;
struct cache * icache;

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

//    // 初始化icache
//    icache = (struct cache *)kmalloc(sizeof(struct cache));
//    if (icache == 0)
//        goto init_cache_err;
//
//    cache_init(icache, ICACHE_CAPACITY, ICACHE_HASHTABLE_SIZE);
//    icache->c_op = &inode_cache_operations;

    // 初始化pcache
    pcache = (struct cache *)kmalloc(sizeof(struct cache));
    if (pcache == 0)
        goto init_cache_err;

    cache_init(pcache, PCACHE_CAPACITY, PCACHE_HASHTABLE_SIZE);
    pcache->c_op = &page_cache_operations;

    return 0;

init_cache_err:
    if (icache) {
        kfree(icache->c_hashtable);
        kfree(icache);
    }
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

// 接下来为文件页的缓存 pcache 相关函数
// 在文件数据缓存中，根据一个页地址查找对应的是否存在
void* pcache_look_up(struct cache *this, struct condition *cond) {
    u32 found = 0;
    u32 hash;
    u32 page_num;
    struct inode        *inode;
    struct vfs_page     *tested;
    struct list_head    *current;
    struct list_head    *start;

    page_num = *((u32*)(cond->cond1));
    inode = (struct inode *)(cond->cond2);

    // 计算页地址对应的哈希值，找到那个哈希值对应页面的链表头
    hash = __intHash(page_num, this->c_tablesize);
    current = &(this->c_hashtable[hash]);
    start = current;

    // 遍历这个链表，精确比较页地址以寻找
    while (current->next != start) {
        current = current->next;
        tested = container_of(current, struct vfs_page, p_hash);

        if (tested->p_location == page_num && tested->p_mapping->a_host == inode) {
            found = 1;
            break;
        }
    }

    // 找到的话返回指向对应页的指针,同时更新哈希链表、LRU链表状态
    if (found) {
        list_del(&(tested->p_hash));
        list_add(&(tested->p_hash), start);
        list_del(&(tested->p_LRU));
        list_add(&(tested->p_LRU), &(this->c_LRU));
        return (void*)tested;
    }
    else
        return 0;
}

// 往文件数据缓存中添加一个已分配的页面（创建已在其他地方完成）
void pcache_add(struct cache *this, void *object) {
    u32 hash;
    struct vfs_page *addend;

    // 计算页地址对应的哈希值
    addend = (struct vfs_page *) object;
    hash = __intHash(addend->p_location, this->c_tablesize);

    // 如果整个文件数据缓冲已满，替换一页出去
    if (cache_is_full(this)) 
        pcache_put_LRU(this);

    // 插入hash表和LRU链表
    list_add(&(addend->p_hash), &(this->c_hashtable[hash]));
    list_add(&(addend->p_LRU), &(this->c_LRU));

    this->c_size += 1;
}

// 如果文件数据缓存已满，释放一个最近最少使用的页面
void pcache_put_LRU(struct cache *this) {
    struct list_head    *put;
    struct vfs_page     *put_page;

    // 找到LRU的链表尾对应的页面，这代表着它最近最少使用
    put = this->c_LRU.prev;
    put_page = container_of(put, struct vfs_page, p_LRU);

    // 如果这个页面被修改过，先写回磁盘
    if(put_page->p_state & P_DIRTY)
        this->c_op->write_back((void *)put_page);

    // 分别从LRU链表、哈希表、对应的地址空间中删除
    list_del(&(put_page->p_LRU));
    list_del(&(put_page->p_hash));
    list_del(&(put_page->p_list));
    this->c_size -= 1;

    // 释放这个页结构所占的物理内存
    release_page(put_page);
}

// 把页高速缓存中的某页写回外存
void pcache_write_back(void *object) {
    struct vfs_page *current;
    current = (struct vfs_page *) object;
    current->p_mapping->a_op->writepage(current);
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

void release_page(struct vfs_page* page) {
    kfree(page->p_data);
    kfree(page);
}

// 以下为哈希函数
// 为整数值计算哈希值
u32 __intHash(u32 key, u32 size) {
    u32 mask = size - 1;
    return key & mask;
}

// 为字符串计算哈希值
u32 __stringHash(struct qstr * qstr, u32 size) {
    u32 i = 0;
    u32 value = 0;
    u32 mask = size - 1;

    for (i = 0; i < qstr->len; i++) {
        value = value * 31 + (u32)(qstr->name[i]);
        value = value & mask;
    }

    return value;
}
