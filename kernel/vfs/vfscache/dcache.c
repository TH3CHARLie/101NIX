#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>

extern struct cache * dcache;

// dentry被引用时计数器加一
struct dentry * dget(struct dentry *dentry) {
    dentry->d_count += 1;
    return dentry;
}

// 此时不需要释放dentry，等需要替换的时候cache会自动替换
void dput(struct dentry *dentry) {
    dentry->d_count -= 1;
}

// 接下来为目录项的缓存 dcache 相关函数
void* dcache_look_up(struct cache *this, struct condition *cond) {
    u32 hash;
    struct qstr         *name;
    struct qstr         *qstr;
    struct dentry       *parent;
    struct dentry       *tested;
    struct list_head    *p;
    struct list_head    *start;

    parent = (struct dentry *) (cond->cond1);
    name   = (struct qstr*)    (cond->cond2);

#ifdef DEBUG_VFS
    kernel_printf("[dcache]: dcache_look_up(%s, %s) len:(%d, %d)\n", parent->d_name.name, name->name,
                  parent->d_name.len, name->len);
#endif

    // 计算名字对应的哈希值，找到那个哈希值对应页面的链表头
    hash = __stringHash(name, this->c_tablesize);
    start = &(this->c_hashtable[hash]);

    // 遍历这个链表搜索，需要父目录及名字匹配
    for (p = start->next; p != start; p = p->next) {
        tested = container_of(p, struct dentry, d_hash);
        qstr = &(tested->d_name);
        if (!parent->d_op->compare(qstr, name) && tested->d_parent == parent)
            goto found;
    }
    return 0;

    // 找到，更新链表和哈希表（提至最前），并返回
found:
    list_del(&(tested->d_hash));
    list_add(&(tested->d_hash), &(this->c_hashtable[hash]));
    list_del(&(tested->d_LRU));
    list_add(&(tested->d_LRU), &(this->c_LRU));
    return (void*)tested;
}

// 往目录项缓存中添加一个项（创建已在其他地方完成）
void dcache_add(struct cache *this, void *object) {
    u32 hash;
    struct dentry* addend;

    // 计算目录项名字对应的哈希值
    addend = (struct dentry *) object;
    hash = __stringHash(&addend->d_name, this->c_tablesize);

    // 如果整个目录项缓冲已满，替换一项出去
    if (cache_is_full(this))
        dcache_put_LRU(this);

    // 插入hash表和LRU链表
    list_add(&(addend->d_hash), &(this->c_hashtable[hash]));
    list_add(&(addend->d_LRU), &(this->c_LRU));

    this->c_size += 1;
}

// 如果目录项缓存已满，释放一个最近最少使用的目录项
void dcache_put_LRU(struct cache *this) {
    struct list_head        *put;
    struct list_head        *start;
    struct dentry           *least_ref;
    struct dentry           *put_dentry;

    // 从LRU的链表尾开始搜索
    start = &(this->c_LRU);
    for (put = start->prev; put != start; put = put->prev) {
        put_dentry = container_of(put, struct dentry, d_LRU);
        if (put_dentry->d_count <= 0)
            break;
    }

    // 若引用数都不为0，找最近最少使用（除了被锁定的根目录外）
    if (put == start) {
        least_ref = 0;
        for (put = start->prev; put != start; put = put->prev) {
            put_dentry = container_of(put, struct dentry, d_LRU);
            if (put_dentry->d_pinned & D_PINNED)
                continue;

            if (least_ref == 0)
                least_ref = put_dentry;
            else if(least_ref->d_count > put_dentry->d_count)
                least_ref = put_dentry;
        }
        put_dentry = least_ref;
    }

    // 内存清理
    if (put_dentry)
        release_dentry(put_dentry);
    else
        kernel_printf("[VFS ERROR]: the dcache is full and frozen");
}