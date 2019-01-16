
#include <zjunix/vfs/ext2.h>
#include <zjunix/slab.h>

extern struct address_space_operations ext2_address_space_operations;
extern struct file_operations ext2_file_operations;
extern struct inode_operations ext2_file_inode_operations;
extern struct inode_operations ext2_dir_inode_operations;

// inode号转换到数据块的绝对扇区地址，从物理盘上填充inode的其他信息
u32 ext2_fill_inode(struct inode *inode) {
    u8 buffer[SECTOR_SIZE];
    u32 i;
    u32 err;
    u32 sect;
    u32 group_sect_base;
    u32 inodes_per_group;
    struct ext2_inode               * ex_inode;
    struct ext2_base_information    * ext2_BI;

    if (inode->i_ino == 0)
        return -EINVAL;

    // 首先的得到对应组的基地址（块位图所在的块）
    group_sect_base = ext2_group_base_sect(inode);
    if (group_sect_base == 0)
        return -EIO;

    // 接下来找到inode数据（inode表内）所在的绝对扇区，并读入相应的数据
    ext2_BI             = (struct ext2_base_information *) inode->i_sb->s_fs_info;
    inodes_per_group    = ext2_BI->sb.attr->s_inodes_per_group;
    sect                = group_sect_base + 2 * (inode->i_blksize >> SECTOR_SHIFT) + \
                            ((inode->i_ino - 1) % inodes_per_group ) / (SECTOR_SIZE / ext2_BI->sb.attr->s_inode_size);
    err = read_block(buffer, sect, 1);
    if (err)
        return -EIO;

    ex_inode = (struct ext2_inode *)(buffer +
            (inode->i_ino - 1) % (SECTOR_SIZE / ext2_BI->sb.attr->s_inode_size) * ext2_BI->sb.attr->s_inode_size);

    // 然后填充VFS的inode信息
    inode->i_blocks     = ex_inode->i_blocks;
    inode->i_size       = ex_inode->i_size;
    inode->i_mode       = ex_inode->i_mode;
    inode->i_op         = S_ISDIR(ex_inode->i_mode) ? &ext2_dir_inode_operations : &ext2_file_inode_operations;
    inode->i_fop        = &ext2_file_operations;

    // 填充inode.i_data的信息
    inode->i_data.a_page = (u32 *)kmalloc(EXT2_N_BLOCKS * sizeof(u32));
    if (inode->i_data.a_page == 0)
        return -ENOMEM;

    inode->i_data.a_host        = inode;
    inode->i_data.a_pagesize    = inode->i_blksize;
    inode->i_data.a_op          = &(ext2_address_space_operations);
    INIT_LIST_HEAD(&(inode->i_data.a_cache));

    for (i = 0; i < EXT2_N_BLOCKS; i++)
        inode->i_data.a_page[i] = ex_inode->i_block[i];

    return 0;
}

// 重置物理盘上的inode为新inode
void ext2_reset_inode(struct ext2_base_information *sbi, u32 base, struct inode *inode) {
    u8 buffer[SECTOR_SIZE];
    u32 i;
    u32 err;
    u32 sect, sect_new;
    u32 inodes_per_group;
    u32 ino;
    struct ext2_inode  * ex_inode;

    // inode必须合法
    ino = inode->i_ino;
    if (ino == 0) {
        kernel_printf_vfs_errno(-EINVAL);
        return;
    }

    u32 group_sect_base = ext2_group_base_sect(inode);
    if (group_sect_base == 0)
        return;

    // 接下来找到inode数据（inode表内）所在的绝对扇区，并读入相应的数据
    inodes_per_group = sbi->sb.attr->s_inodes_per_group;
    sect = group_sect_base + 2 * (inode->i_blksize >> SECTOR_SHIFT)
           + ((ino - 1) % inodes_per_group) / (SECTOR_SIZE / sbi->sb.attr->s_inode_size);

    // sect_new = fs_info.par_start_address + group_desc.bg_inode_table * 8 + number * 256 / 512;

    // 先读出来
    err = read_block(buffer, sect, 1);
    if (err)
        kernel_printf_vfs_errno(err);

    ex_inode = (struct ext2_inode *)(buffer + \
        ((ino - 1) % inodes_per_group ) % (SECTOR_SIZE / sbi->sb.attr->s_inode_size)
                                                         * sbi->sb.attr->s_inode_size);

    // 然后修改buffer中VFS的inode信息
    ex_inode->i_size    = inode->i_size;
    ex_inode->i_blocks  = inode->i_blocks;
    ex_inode->i_mode    = inode->i_mode;
    for (i = 0; i < EXT2_N_BLOCKS; i++)
        ex_inode->i_block[i] = inode->i_data.a_page[i];

    // 把修改写入外存
    err = write_block(buffer, sect, 1);
    if (err)
        kernel_printf_vfs_errno(err);

}

// 二次散列法：找到一个有空位的组，返回组号
u32 find_group_other(struct ext2_base_information *sbi, struct inode *parent) {
    u32 parent_group;      // 父节点的组号
    u32 ngroups;           // 组数量
    u32 group, i;
    struct ext2_group_desc *desc;

    parent_group = (parent->i_ino - 1) / sbi->sb.attr->s_inodes_per_group;
    ngroups = sbi->s_groups_count;

    // 先尝试可不可以把新节点放到父目录里
    // 如果父节点的组有空闲的inode和block就可以
    group = parent_group;
    desc = ext2_get_group_desc(sbi, group, 1);
    if (desc && desc->bg_free_inodes_count &&
                desc->bg_free_blocks_count)
        goto found;

    // 随机找一个和父目录不同的组
    // 使用二次哈希找一个有充足的空闲inode和block的组
    // 按照依次乘以二的偏移来寻找
    group = (group + parent->i_ino) % ngroups;
    for (i = 1; i < ngroups; i <<= 1) {
        group += i;
        if (group >= ngroups)
            group -= ngroups;

        desc = ext2_get_group_desc(sbi, group, 1);
        if (desc && desc->bg_free_inodes_count &&
                    desc->bg_free_blocks_count)
            goto found;
    }

    // 运行到此，说明查找失败
    // 则从父节点开始，依次就近寻找一个空闲inode，即使这个组没有充足的inode和block
    group = parent_group;
    for (i = 0; i < ngroups; i++) {
        if (++group >= ngroups)
            group = 0;

        // 只要有空闲的inode就可以
        desc = ext2_get_group_desc(sbi, group, 1);
        if (desc && desc->bg_free_inodes_count)
            goto found;
    }

    return -1;
found:
    return group;
}

// 获得一个物理盘上一个可用的新inode
struct inode *ext2_new_inode(struct inode *dir, u32 mode) {
    u32 i;
    u32 err;
    u32 sect;
    u32 group;
    u32 ino_pos;
    u32 ino = 0;
    u8 buffer[4096];
    struct inode                    *inode;
    struct ext2_group_desc          *desc;
    struct super_block              *sb;
    struct ext2_super_block         *es;
    struct ext2_base_information    *sbi;

    sb = dir->i_sb;
    sbi = EXT2_SB(sb);
    es = sbi->sb.attr;

    group = find_group_other(sbi, dir);

    // TODO: 创建目录使用orlov方法分配组
//    if (S_ISDIR(mode)) {
//        if (test_opt(sb, OLDALLOC))
//            group = find_group_dir(sb, dir);
//        else
//            group = find_group_orlov(sb, dir);
//    } else
//        group = find_group_other(sb, dir);
    if (group == -1) {
        err = -ENOSPC;
        goto fail;
    }

#ifdef DEBUG_EXT2
    kernel_printf("now in ext2_new_inode(%d), and found group %d\n", dir->i_ino, group);
#endif

    for (i = 0; i < sbi->s_groups_count; i++) {
        // 获得group组的组描述符
        desc = ext2_get_group_desc(sbi, group, 1);
        if (desc == 0)
            goto next_group;

#ifdef DEBUG_EXT2
        kernel_printf("now in ext2_new_inode(%d), group %d: desc->bg_inode_bitmap %d\n",
                      dir->i_ino, group, desc->bg_inode_bitmap);
#endif

        // 读取bitmap所在块
        sect = sbi->ex_base + desc->bg_inode_bitmap * (sbi->ex_blksize >> SECTOR_SHIFT);
        err = read_block(buffer, sect, 8);
        if (err) {
            kernel_printf_vfs_errno(-EIO);
            goto next_group;
        }

        // 查找是否有空位
        ino_pos = find_first_zero_bit(buffer, sbi->ex_blksize);
        kernel_printf("now in ext2_new_inode(%d), group %d: ino_pos %d\n",
                      dir->i_ino, group, ino_pos);
        if (ino_pos == -EINVAL)
            goto next_group;

        // inode从1开始编号，必须得是合法inode号
        ino = group * es->s_inodes_per_group + ino_pos + 1;
        if (ino < EXT2_FIRST_INO || ino > es->s_inodes_count)
            goto next_group;

        goto got_inode;

        next_group:
        if (++group == sbi->s_groups_count)
            group = 0;
    }

    // 全部扫完，都没有发现可用的inode
    err = -ENOSPC;
    goto fail;

got_inode:

    kernel_printf("now in ext2_new_inode(%d), group %d: ino_pos %d, get ino: %d\n",
                  dir->i_ino, group, ino_pos, ino);

    inode = alloc_inode(sb);
    if (IS_ERR(inode))
        return inode;

    // ext2超级块更新
    es->s_free_inodes_count--;
    ext2_write_super(sbi);

    // ext2组描述符更新
    desc->bg_free_inodes_count--;
    if (S_ISDIR(mode))
        desc->bg_used_dirs_count++;
    ext2_write_group_desc(desc, sbi, group, 0);

    kernel_printf("now in ext2_new_inode(%d), es: %d, desc: %d\n",
                  dir->i_ino, es->s_free_inodes_count, desc->bg_free_inodes_count);

    // TODO 莫名其妙的bug
    // inode位图更新
//    set_bit(buffer, ino_pos);
//    err = write_block(buffer, sect, 8);

// TODO 完善底层信息
//    spin_lock(sb_bgl_lock(sbi, group));
//    if (S_ISDIR(mode)) {
//        if (sbi->s_debts[group] < 255)
//            sbi->s_debts[group]++;
//        gdp->bg_used_dirs_count =
//                cpu_to_le16(le16_to_cpu(gdp->bg_used_dirs_count) + 1);
//    } else {
//        if (sbi->s_debts[group])
//            sbi->s_debts[group]--;
//    }
//    spin_unlock(sb_bgl_lock(sbi, group));


    // 此处可以有文件所有者和文件权限的填写
//    inode->i_uid = current->fsuid;
//    if (test_opt (sb, GRPID))
//        inode->i_gid = dir->i_gid;
//    else if (dir->i_mode & S_ISGID) {
//        inode->i_gid = dir->i_gid;
//        if (S_ISDIR(mode))
//            mode |= S_ISGID;
//    } else
//        inode->i_gid = current->fsgid;

    // 完善当前VFS inode信息

    inode->i_mode    = mode;
    inode->i_ino     = ino;
    inode->i_blocks  = 0;
    inode->i_op      = S_ISDIR(mode) ? (&ext2_dir_inode_operations) : (&ext2_file_inode_operations);
    inode->i_fop     = (&ext2_file_operations);

    inode->i_data.a_host      = inode;
    inode->i_data.a_pagesize  = sb->s_blksize;
    inode->i_data.a_op        = &(ext2_address_space_operations);
    INIT_LIST_HEAD(&(inode->i_data.a_cache));

    inode->i_data.a_page = (u32 *)kmalloc(EXT2_N_BLOCKS * sizeof(u32));
    for (i = 0; i < EXT2_N_BLOCKS; i++)
        inode->i_data.a_page[i] = 0;

    // inode更新
    ext2_reset_inode(sbi, desc->bg_inode_table * 8 + sbi->ex_base, inode);

    // 暴力设置bitmap
    ext2_set_inode_bitmap(inode);

#ifdef DEBUG_EXT2
    kernel_printf("    check bitmap: %d\n", ext2_check_inode_bitmap(inode));
#endif

    return inode;

fail:
    return ERR_PTR(err);
}