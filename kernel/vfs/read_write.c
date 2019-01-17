#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>

extern struct cache * pcache;

// 通用读文件方法
u32 generic_file_read(struct file *file, u8 *buf, u32 count, u32 *ppos) {
    u32 cur;
    u32 page_no, start_page_no, end_page_no;
    u32 start_page_cur, end_page_cur, read_count;
    struct vfs_page *cur_page;

    struct inode *inode = file->f_dentry->d_inode;
    u32 pos = *ppos;
    u32 blksize = inode->i_blksize;

    // 计算字节对应的起始和终止页号、页偏移
    start_page_no  = pos / blksize;
    start_page_cur = pos % blksize;

    if (pos + count < inode->i_size) {
        end_page_no  = (pos + count) / blksize;
        end_page_cur = (pos + count) % blksize;
    } else {
        end_page_no  = inode->i_size / blksize;
        end_page_cur = inode->i_size % blksize;
    }

    // 读取每一文件页
    cur = 0;
    for (page_no = start_page_no; page_no <= end_page_no; page_no++) {

        cur_page = pcache_get_page(pcache, inode, page_no);
        if (IS_ERR(cur_page)) {
            kernel_printf("[[VFS READ ERROR]]：generic_file_read() cannot get page %d\n", page_no);
            break;
        }

        // 拷贝数据，需要考虑在最后一页的情况，此时不会全部读完
        if (page_no == start_page_no) {
            read_count = page_no == end_page_no ?
                         end_page_cur - start_page_cur : blksize - start_page_cur;
            kernel_memcpy(buf, cur_page->p_data + start_page_cur, read_count);
        } else {
            read_count = page_no == end_page_no ? end_page_cur : blksize;
            kernel_memcpy(buf + cur, cur_page->p_data, read_count);
        }

        // cur既是当前buf的光标地址，也代表已经读了多少个字节
        cur += read_count;
        *ppos += read_count;
    }

out:
    file->f_pos = *ppos;
    return cur;
}


// 通用写文件方法
u32 generic_file_write(struct file *file, u8 *buf, u32 count, u32 *ppos){
    u32 pos;
    u32 blksize;
    u32 cur;
    u32 page_no, start_page_no, end_page_no;
    u32 start_page_cur, end_page_cur;
    u32 write_count;
    struct inode         *inode;
    struct dentry        *parent;
    struct vfs_page      *cur_page;
    struct address_space *mapping;

    inode = file->f_dentry->d_inode;
    mapping = &(inode->i_data);

    pos = *ppos;
    blksize = inode->i_blksize;

    // 计算字节对应的起始和终止页号、页偏移
    start_page_no  = pos / blksize;
    start_page_cur = pos % blksize;
    end_page_no  = (pos + count) / blksize;
    end_page_cur = (pos + count) % blksize;

    // 读取每一文件页
    cur = 0;
    for (page_no = start_page_no; page_no <= end_page_no; page_no++) {

        cur_page = pcache_get_page(pcache, inode, page_no);
        if (IS_ERR(cur_page)) {
            kernel_printf("[[VFS WRITE ERROR]]：generic_file_write() cannot get page %d\n", page_no);
            break;
        }

        // 拷贝数据
        if (page_no == start_page_no) {
            write_count = page_no == end_page_no ?
                          end_page_cur - start_page_cur : blksize - start_page_cur;
            kernel_memcpy(cur_page->p_data + start_page_cur, buf + cur, write_count);
        } else {
            write_count = page_no == end_page_no ? end_page_cur : blksize;
            kernel_memcpy(cur_page->p_data, buf + cur, write_count);
        }

        // 最后写回内存
        mapping->a_op->writepage(cur_page);

        cur += write_count;
        *ppos += write_count;

    }

    // 最后改变文件大小
    if (pos + count > inode->i_size) {
        inode->i_size = pos + count;
        parent = file->f_dentry->d_parent;
        inode->i_sb->s_op->write_inode(inode, parent);
    }

    // cur既是当前buf的光标地址，也代表已经写了多少个字节
out:
    file->f_pos = *ppos;
    return cur;
}

// 通用冲洗方法
// 把文件关联的已缓冲页逐页写回
u32 generic_file_flush(struct file * file) {
    struct vfs_page *page;
    struct inode *inode;
    struct list_head *a, *begin;
    struct address_space *mapping;

    inode = file->f_dentry->d_inode;
    mapping = &(inode->i_data);
    begin = &(mapping->a_cache);

    for (a = begin->next; a != begin; a = a->next) {
        page = container_of(a, struct vfs_page, p_list);
        if (page->p_state & P_DIRTY)
            mapping->a_op->writepage(page);
    }

    return 0;
}