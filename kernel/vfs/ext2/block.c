#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/ext2.h>
#include <zjunix/slab.h>

// 当flag为0时该函数返回inode号对应的组描述符
// 当flag为1是ino即组号，返回对应的组描述符
struct ext2_group_desc * ext2_get_group_desc(struct ext2_base_information * ext2_BI, u32 ino, int flag) {
    u8 buffer[SECTOR_SIZE];
    struct ext2_group_desc * gdt;

    gdt = (struct ext2_group_desc *)kmalloc(sizeof(struct ext2_group_desc));
    if (gdt == 0) {
        kernel_printf_vfs_errno(-ENOMEM);
        return 0;
    }

    // inode号对应的组号、组扇区号、组扇区内偏移
    u32 group_no = flag ? ino : (ino - 1) / ext2_BI->sb.attr->s_inodes_per_group;
    u32 group_gdt_sect = ext2_BI->ex_first_gdt_sect + group_no / EXT2_GROUPS_PER_SECT;
    u32 group_offset = group_no % EXT2_GROUPS_PER_SECT * EXT2_GROUP_DESC_BYTE;

    // 读组描述符所在扇区
    u32 err = read_block(buffer, group_gdt_sect, 1);
    if (err) {
        kernel_printf_vfs_errno(-EIO);
        return 0;
    }

    // 拷贝inode号对应组的组描述符
    kernel_memcpy(gdt, buffer + group_offset, sizeof(struct ext2_group_desc));
    return gdt;
}

void ext2_write_group_desc(struct ext2_group_desc * gdt, struct ext2_base_information * ext2_BI,
                           u32 ino, int flag) {
    u32 err;
    u8 buffer[SECTOR_SIZE];

    // inode号对应的组号、组扇区号、组扇区内偏移
    u32 group_no = flag ? ino : (ino - 1) / ext2_BI->sb.attr->s_inodes_per_group;
    u32 group_gdt_sect = ext2_BI->ex_first_gdt_sect + group_no / EXT2_GROUPS_PER_SECT;
    u32 group_offset = group_no % EXT2_GROUPS_PER_SECT * EXT2_GROUP_DESC_BYTE;

    // 先读组描述符所在扇区
    err = read_block(buffer, group_gdt_sect, 1);
    if (err)
        kernel_printf_vfs_errno(-EIO);

    kernel_memcpy(buffer + group_offset, gdt, sizeof(struct ext2_group_desc));

    // 写组描述符所在扇区
    err = write_block(buffer, group_gdt_sect, 1);
    if (err)
        kernel_printf_vfs_errno(-EIO);
}

// 通过检查inode位图来检查inode是否已被删除
// 若1表示没有被删除。若0则表示已被删除，或者发生错误。
u32 ext2_check_inode_bitmap(struct inode *inode){
    // WARN: 此处buffer大小等于磁盘块大小，这里写死
    u8 buffer[4096];
    u32 err;
    u32 sect;
    struct ext2_base_information * ext2_BI;

    ext2_BI = (struct ext2_base_information *)inode->i_sb->s_fs_info;
    u32 inodes_per_group = ext2_BI->sb.attr->s_inodes_per_group;

    // 获得inode所在组的组描述符
    struct ext2_group_desc * gdt = ext2_get_group_desc(ext2_BI, inode->i_ino, 0);
    if (gdt == 0)
        return 0;

    // 读取bitmap所在块
    sect = ext2_BI->ex_base + gdt->bg_inode_bitmap * (inode->i_blksize >> SECTOR_SHIFT);
    err = read_block(buffer, sect, 8);
    if (err) {
        kernel_printf_vfs_errno(-EIO);
        return 0;
    }

    // 检测是否被删除
    return get_bit(buffer, (inode->i_ino - 1) % inodes_per_group % (4096 * BITS_PER_BYTE));
}

u32 ext2_set_inode_bitmap(struct inode *inode) {
    // WARN: 此处buffer大小等于磁盘块大小，这里写死
    u8 buffer[4096];
    u32 err;
    u32 sect;
    struct ext2_base_information * ext2_BI;

    ext2_BI = (struct ext2_base_information *)inode->i_sb->s_fs_info;
    u32 inodes_per_group = ext2_BI->sb.attr->s_inodes_per_group;

    // 获得inode所在组的组描述符
    struct ext2_group_desc * gdt = ext2_get_group_desc(ext2_BI, inode->i_ino, 0);
    if (gdt == 0)
        return 0;

    // 读取bitmap所在块
    sect = ext2_BI->ex_base + gdt->bg_inode_bitmap * (inode->i_blksize >> SECTOR_SHIFT);
    err = read_block(buffer, sect, 8);
    if (err) {
        kernel_printf_vfs_errno(-EIO);
        return 0;
    }

    // inode对应位 置1
    set_bit(buffer, (inode->i_ino - 1) % inodes_per_group % (4096 * BITS_PER_BYTE));

    err = write_block(buffer, sect, 8);
    if (err) {
        kernel_printf_vfs_errno(-EIO);
        return 0;
    }
}

// 找到inode所在组的基地址
u32 ext2_group_base_sect(struct inode * inode) {
    struct ext2_base_information * ext2_BI;

    ext2_BI =(struct ext2_base_information *)inode->i_sb->s_fs_info;

    struct ext2_group_desc * gdt = ext2_get_group_desc(ext2_BI, inode->i_ino, 0);
    if (gdt == 0)
        return 0;

    // TODO 此处可以改成bg_inode_table
    u32 group_block_base = gdt->bg_block_bitmap;
    u32 group_sect_base  = ext2_BI->ex_base + group_block_base * (inode->i_blksize >> SECTOR_SHIFT);

    return group_sect_base;
}
