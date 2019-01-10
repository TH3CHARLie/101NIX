#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/ext2.h>
#include <driver/sd.h>

// 依次读取每一组的组描述符，然后得到整个盘的空闲block总数
u32 ext2_count_free_blocks(struct super_block *sb) {
    int i;
    u32 desc_count = 0;
    struct ext2_group_desc *desc;

    for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
        desc = ext2_get_group_desc(EXT2_SB(sb), i, 1);
        if (!desc)
            continue;
        desc_count += desc->bg_free_blocks_count;
    }

    return desc_count;
}

// 依次读取每一组的组描述符，然后得到整个盘的空闲inode总数
u32 ext2_count_free_inodes(struct super_block *sb) {
    int i;
    u32 desc_count = 0;
    struct ext2_group_desc *desc;

    for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
        desc = ext2_get_group_desc(EXT2_SB(sb), i, 1);
        if (!desc)
            continue;
        desc_count += desc->bg_free_inodes_count;
    }

    return desc_count;
}

void ext2_sync_super(struct super_block *sb) {

    struct ext2_base_information *sbi = EXT2_SB(sb);
    struct ext2_super_block *es = sbi->sb.attr;

    es->s_free_blocks_count = ext2_count_free_blocks(sb);
    es->s_free_inodes_count = ext2_count_free_inodes(sb);
    // es->s_wtime = cpu_to_le32(get_seconds());

    u32 err = write_block(es, sbi->ex_first_sb_sect, sizeof(struct ext2_super_block) / SECSIZE);
    if (err)
        kernel_printf_vfs_errno(err);

    sb->s_dirt = 0;
}

void ext2_write_super(struct ext2_base_information *sbi) {
    u32 err = write_block(sbi->sb.data, sbi->ex_first_sb_sect, sizeof(struct ext2_super_block) / SECSIZE);
    if (err)
        kernel_printf_vfs_errno(err);
}