#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/ext2.h>
#include <zjunix/slab.h>

// 该函数返回inode号对应的组描述符
struct ext2_group_desc * ext2_get_group_gdt(struct ext2_base_information * ext2_BI, u32 ino) {
    u8 buffer[SECTOR_SIZE];
    struct ext2_group_desc * gdt;

    gdt = (struct ext2_group_desc *)kmalloc(sizeof(struct ext2_group_desc));
    if (gdt == 0)
        return ERR_PTR(-ENOMEM);

    // inode号对应的组号、组扇区号、组扇区内偏移
    u32 group_no = (ino - 1) / ext2_BI->sb.attr->s_inodes_per_group;
    u32 group_gdt_sect = ext2_BI->ex_first_gdt_sect + group_no / EXT2_GROUPS_PER_SECT;
    u32 group_offset = group_no % EXT2_GROUPS_PER_SECT * EXT2_GROUP_DESC_BYTE;

    // 读组描述符所在扇区
    u32 err = read_block(buffer, group_gdt_sect, 1);
    if (err)
        return 0;

    // 拷贝inode号对应组的组描述符
    kernel_memcpy(gdt, buffer + group_offset, sizeof(struct ext2_group_desc));
    return gdt;
}

// 通过检查inode位图来检查inode是否已被删除
// 若1表示没有被删除。若0则表示已被删除，或者发生错误。
u32 ext2_check_inode_bitmap(struct inode *inode){
    u8 buffer[SECTOR_SIZE];
    u32 err;
    u32 sect;
    struct ext2_base_information * ext2_BI;

    ext2_BI = (struct ext2_base_information *)inode->i_sb->s_fs_info;
    u32 inodes_per_group = ext2_BI->sb.attr->s_inodes_per_group;

    // 获得inode所在组的组描述符
    struct ext2_group_desc * gdt = ext2_get_group_gdt(ext2_BI, inode->i_ino);
    if (gdt == 0)
        return 0;

    // 读取bitmap所在块
    sect = ext2_BI->ex_base + gdt->bg_inode_bitmap * (inode->i_blksize >> SECTOR_SHIFT);
    err = read_block(buffer, sect, 1);
    if (err)
        return 0;

    // 检测是否被删除
    return get_bit(buffer, (inode->i_ino - 1) % inodes_per_group % (SECTOR_SIZE * BITS_PER_BYTE));
}


