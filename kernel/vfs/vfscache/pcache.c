#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>

// 在pacache中寻找inode下的第page_no页的东西
// 如果不存在，就分配一个新的页然后，加入缓存，并返回
struct vfs_page * pcache_get_page(struct cache * pcache, struct inode * inode, u32 page_no) {
    u32 cur_page_no;
    struct condition cond;
    struct vfs_page *page;

#ifdef DEBUG_VFS
    kernel_printf("  [pcache] begin pcache_get_page(ino: %d, page_no: %d)\n", inode->i_ino, page_no);
#endif

    cur_page_no = inode->i_data.a_op->bmap(inode, page_no);
    if (cur_page_no == 0)
        return ERR_PTR(-EINVAL);

#ifdef DEBUG_VFS
    kernel_printf("                      pcache_get_page cur_page_no: %d\n", cur_page_no);
#endif

    cond.cond1 = (void *)(&cur_page_no);
    cond.cond2 = (void *)(inode);

    page = (struct vfs_page *)pcache->c_op->look_up(pcache, &cond);
    // 如果不存在，则新建一页，从磁盘上读取再返回
    if (page == 0) {
        page = alloc_vfspage(cur_page_no, &inode->i_data);
        pcache->c_op->add(pcache, (void *)page);
        list_add(&(page->p_list), &((&inode->i_data)->a_cache));
    }

    return page;
}

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

    kernel_printf("  [pcache] begin pcache_look_up: %d %d\n", page_num, inode->i_ino);

    // 计算页地址对应的哈希值，找到那个哈希值对应页面的链表头
    hash = __intHash(page_num, this->c_tablesize);
    start = &(this->c_hashtable[hash]);

    for (current = start->next; current != start; current = current->next) {
        tested = container_of(current, struct vfs_page, p_hash);
        if (tested->p_location == page_num && tested->p_mapping->a_host == inode) {
            found = 1;
            break;
        }
    }

    if (found) {
        list_del(&(tested->p_hash));
        list_add(&(tested->p_hash), start);
        list_del(&(tested->p_LRU));
        list_add(&(tested->p_LRU), &(this->c_LRU));
        return (void*)tested;
    }
    else {
        tested = alloc_vfspage(page_num, &inode->i_data);
        this->c_op->add(this, (void*)tested);
        list_add(&(tested->p_list), &((&inode->i_data)->a_cache));
        return (void*)tested;
    }
}

// 往文件数据缓存中添加一个已分配的页面（创建已在其他地方完成）
void pcache_add(struct cache *this, void *object) {
    u32 hash;
    struct vfs_page *addend;

    addend = (struct vfs_page *) object;
    hash = __intHash(addend->p_location, this->c_tablesize);

    if (cache_is_full(this))
        pcache_put_LRU(this);

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

    if(put_page->p_state & P_DIRTY)
        this->c_op->write_back((void *)put_page);

    list_del(&(put_page->p_LRU));
    list_del(&(put_page->p_hash));
    list_del(&(put_page->p_list));
    this->c_size -= 1;

    release_page(put_page);
}

// 把页高速缓存中的某页写回外存
void pcache_write_back(void *object) {
    struct vfs_page *current;
    current = (struct vfs_page *) object;
    current->p_mapping->a_op->writepage(current);
}
