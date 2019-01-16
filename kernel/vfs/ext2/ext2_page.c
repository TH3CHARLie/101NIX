#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/ext2.h>
#include <zjunix/slab.h>

// 从外存读入一页
u32 ext2_readpage(struct vfs_page * page) {
    u32 err;
    u32 base;
    u32 abs_sect_addr;
    struct inode *inode;

    if (!page->p_location || !page->p_mapping || !page->p_mapping->a_host)
        return -EINVAL;

    // 计算绝对扇区地址
    inode = page->p_mapping->a_host;
    base = ((struct ext2_base_information *)(inode->i_sb->s_fs_info))->ex_base;
    abs_sect_addr = base + page->p_location * (inode->i_blksize >> SECTOR_SHIFT);

    // 分配数据区，从外存读入
    page->p_data = (u8 *)kmalloc(sizeof(u8) * inode->i_blksize);
    if (page->p_data == 0)
        return -ENOMEM;
    kernel_memset(page->p_data, 0, sizeof(u8) * inode->i_blksize);

    err = read_block(page->p_data, abs_sect_addr, inode->i_blksize >> SECTOR_SHIFT);
    if (err)
        return -EIO;

    return 0;
}

// 把一页写回外存
u32 ext2_writepage(struct vfs_page * page) {
    u32 err;
    u32 base;
    u32 abs_sect_addr;
    struct inode *inode;

    if (!page->p_location || !page->p_mapping || !page->p_mapping->a_host)
        return -EINVAL;

    // 计算绝对扇区地址
    inode = page->p_mapping->a_host;
    base = ((struct ext2_base_information *)(inode->i_sb->s_fs_info))->ex_base;
    abs_sect_addr = base + page->p_location * (inode->i_blksize >> SECTOR_SHIFT);

    // 写到外存
    err = write_block(page->p_data, abs_sect_addr, inode->i_blksize >> SECTOR_SHIFT);
    if (err)
        return -EIO;

    return 0;
}

// 根据由相对文件页号得到相对物理页号
u32 ext2_bmap(struct inode * inode, u32 page_no) {
    u8  *data;
    u32 addr;
    u32 retval;
    u32 entry_num;
    u32 sect_cnt;
    u32 *page = inode->i_data.a_page;

    // 直接映射
    if (page_no < EXT2_FIRST_MAP_INDEX) {
        retval = page[page_no];
        goto out;
    }

    entry_num = inode->i_blksize >> EXT2_BLOCK_ADDR_SHIFT;
    sect_cnt  = inode->i_blksize >> SECTOR_SHIFT;

    data = (u8 *)kmalloc(inode->i_blksize * sizeof(u8));
    if (data == 0)
        return 0;

    // 一次间接块号
    page_no -= EXT2_FIRST_MAP_INDEX;
    if (page_no < entry_num) {
        read_block(data, page[EXT2_FIRST_MAP_INDEX], sect_cnt);
        retval = get_u32(data + (page_no << EXT2_BLOCK_ADDR_SHIFT));
        goto ok;
    }

    // 二次间接块号
    page_no -= entry_num;
    if (page_no < entry_num * entry_num) {
        read_block(data, page[EXT2_SECOND_MAP_INDEX], sect_cnt);
        addr = get_u32(data + ((page_no / entry_num) << EXT2_BLOCK_ADDR_SHIFT));

        read_block(data, addr, sect_cnt);
        retval = get_u32(data + ((page_no % entry_num) << EXT2_BLOCK_ADDR_SHIFT));
        goto ok;
    }

    // 三次间接块号
    page_no -= entry_num * entry_num;
    if (page_no < entry_num * entry_num * entry_num) {
        read_block(data, page[EXT2_THIRD_MAP_INDEX], sect_cnt);
        addr = get_u32(data + ((page_no / (entry_num * entry_num)) << EXT2_BLOCK_ADDR_SHIFT));

        read_block(data, addr, sect_cnt);
        page_no = page_no % (entry_num * entry_num);
        addr = get_u32(data + ((page_no / entry_num) << EXT2_BLOCK_ADDR_SHIFT));

        read_block(data, addr, sect_cnt);
        retval = get_u32(data + ((page_no % entry_num) << EXT2_BLOCK_ADDR_SHIFT));
        goto ok;
    }

ok:
    kfree(data);
out:

    kernel_printf("      ext2_bmap: %d %d -> %d\n", inode->i_ino, page_no, retval);

    return retval;
}