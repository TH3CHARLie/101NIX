#ifndef _ZJUNIX_VFS_VFSCACHE_H
#define _ZJUNIX_VFS_VFSCACHE_H

#include <zjunix/vfs/vfs.h>

#define DCACHE_CAPACITY                 16
#define DCACHE_HASHTABLE_SIZE          16

#define ICACHE_CAPACITY                 16
#define ICACHE_HASHTABLE_SIZE          16

#define PCACHE_CAPACITY                 64
#define PCACHE_HASHTABLE_SIZE           16

#define P_CLEAR                         0
#define P_DIRTY                         1

// 文件缓存页
struct vfs_page {
    u8                          *p_data;                    // 数据
    u32                         p_state;                    // 状态
    u32                         p_location;                 // 对应文件系统定义的块地址
    struct list_head            p_hash;                     // 哈希表链表
    struct list_head            p_LRU;                      // LRU链表
    struct list_head            p_list;                     // 同一文件已缓冲页的链表
    struct address_space        *p_mapping;                 // 所属的address_space结构
};

// 缓存
struct cache {
    u32                         c_size;                     // 现有项数
    u32                         c_capacity;                 // 最大项数
    u32                         c_tablesize;                // 哈希值数
    struct list_head            c_LRU;                      // 指向LRU链表的指针
    struct list_head            *c_hashtable;               // 指向哈希表的指针
    struct cache_operations     *c_op;                      // 指向缓冲区的操作函数指针
};

// 储存查找条件，用于cache的查找
struct condition {
    void    *cond1;
    void    *cond2;
    void    *cond3;
};

// 缓存的操作函数
struct cache_operations {
    // 根据某一条件，用哈希表在缓冲区中查找对应元素是否存在
    void* (*look_up)(struct cache*, struct condition*);
    // 往缓冲区加入一个元素。如果发生缓冲区满，执行使用LRU算法的替换操作
    void (*add)(struct cache*, void*);
    // 判断缓冲区是否满
    u32 (*is_full)(struct cache*);
    // 当一个缓冲页被替换出去时，写回外存以保持数据协调
    void (*write_back)(void*);
};

// 下面是函数声明
// vfscache.c
u32 init_cache();
void cache_init(struct cache *, u32, u32);
u32 cache_is_full(struct cache *);

void* dcache_look_up(struct cache *, struct condition *);
void dcache_add(struct cache *, void *);
void dcache_put_LRU(struct cache *);

// void* icache_look_up(struct cache *, struct condition *);
// void icache_add(struct cache *, void *);
// void icache_put_LRU(struct cache *);
// void icache_write_back(void *);

void* pcache_look_up(struct cache *, struct condition *);
void pcache_add(struct cache *, void *);
void pcache_put_LRU(struct cache *);
void pcache_write_back(void *);

void dget(struct dentry *);
void dput(struct dentry *);

void release_dentry(struct dentry *);
void release_inode(struct inode *);
void release_page(struct vfs_page *);

u32 __intHash(u32, u32);
u32 __stringHash(struct qstr *, u32);

// utils.c
u32 read_block(u8 *, u32, u32);
u32 write_block(u8 *, u32, u32);

#endif