#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/ext2.h>

// 重置物理盘上的block为新block
void ext2_reset_block(struct ext2_base_information *sbi, u32 bno) {
    u8 buffer[4096];
    u32 err;
    u32 sect;

    // 接下来找到block数据所在的起始扇区，并写入相应的数据
    sect = sbi->ex_base + bno * (sbi->ex_blksize >> SECTOR_SHIFT);
    kernel_memset(buffer, 0, 4096);

    // 把修改写入外存
    err = write_block(buffer, sect, 8);
    if (err)
        kernel_printf_vfs_errno(err);
}


u32 ext2_new_block(struct inode *inode) {
    u32 i;
    u32 err;
    u32 sect;
    u32 group;
    u32 bno_pos;
    u32 bno = 0;
    u8 buffer[4096];
    struct ext2_group_desc          *desc;
    struct super_block              *sb;
    struct ext2_super_block         *es;
    struct ext2_base_information    *sbi;

    sb = inode->i_sb;
    sbi = EXT2_SB(sb);
    es = sbi->sb.attr;

#ifdef DEBUG_EXT2
    kernel_printf("now in ext2_new_block(inode: %d)\n", inode->i_ino);
#endif

    group = find_group_other(sbi, inode);
    if (group == -1) {
        err = -ENOSPC;
        goto fail;
    }

    for (i = 0; i < sbi->s_groups_count; i++) {
        // 获得group组的组描述符
        desc = ext2_get_group_desc(sbi, group, 1);
        if (desc == 0)
            goto next_group;

        // 读取bitmap所在块
        sect = sbi->ex_base + desc->bg_block_bitmap * (inode->i_blksize >> SECTOR_SHIFT);
        err = read_block(buffer, sect, 8);
        if (err) {
            kernel_printf_vfs_errno(-EIO);
            goto next_group;
        }

        kernel_printf("      buffer: %d\n", buffer);

        // 查找是否有空位
        bno_pos = find_first_zero_bit(buffer, inode->i_blksize);
        if (bno_pos == -EINVAL)
            goto next_group;

        // inode从1开始编号，必须得是合法inode号
        bno = group * es->s_blocks_per_group + bno_pos;

        goto got_block;

        next_group:
        if (++group == sbi->s_groups_count)
            group = 0;
    }

    // 全部扫完，都没有发现可用的block
    err = -ENOSPC;
    goto fail;

got_block:

    // ext2超级块更新
    es->s_free_blocks_count--;
    ext2_write_super(sbi);

    // ext2组描述符更新
    desc->bg_free_blocks_count--;
    ext2_write_group_desc(desc, sbi, group, 0);

    // block位图和block更新
    kernel_printf("      buffer: %d\n", buffer);
    set_bit(buffer, bno_pos);
    err = write_block(buffer, sect, 8);

    ext2_reset_block(sbi, bno);

#ifdef DEBUG_EXT2
    kernel_printf("now end ext2_new_block(inode: %d) -> bno: %d\n", inode->i_ino, bno);
#endif

out:
    return bno;

fail:
    kernel_printf_vfs_errno(err);
    return -1;
}