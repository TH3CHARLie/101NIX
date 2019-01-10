
#include <zjunix/vfs/ext2.h>

//static inline struct ext2_inode_info *EXT2_I(struct inode *inode)
//{
//    return container_of(inode, struct ext2_inode_info, vfs_inode);
//}

struct inode *ext2_new_inode(struct inode *dir, int mode)
{
    u32 i;
    u32 group;
    u32 ino = 0;
    struct super_block      *sb;
    struct buffer_head      *bitmap_bh = NULL;
    struct buffer_head      *bh2;
    struct inode            *inode;
    struct ext2_group_desc  *gdp;
    struct ext2_super       *es;
    //struct ext2_inode_info  *ei;
    //struct ext2_sb_info     *sbi;
    int err;

    sb = dir->i_sb;
    inode = new_inode(sb);
    if (!inode)
        return ERR_PTR(-ENOMEM);

    ei = EXT2_I(inode);
    sbi = EXT2_SB(sb);
    es = sbi->s_es;
    if (S_ISDIR(mode)) {
        if (test_opt(sb, OLDALLOC))
            group = find_group_dir(sb, dir);
        else
            group = find_group_orlov(sb, dir);
    } else
        group = find_group_other(sb, dir);

    if (group == -1) {
        err = -ENOSPC;
        goto fail;
    }

    for (i = 0; i < sbi->s_groups_count; i++) {
        gdp = ext2_get_group_desc(sb, group, &bh2);
        brelse(bitmap_bh);
        bitmap_bh = read_inode_bitmap(sb, group);
        if (!bitmap_bh) {
            err = -EIO;
            goto fail;
        }
        ino = 0;

        repeat_in_this_group:
        ino = ext2_find_next_zero_bit((unsigned long *)bitmap_bh->b_data,
                                      EXT2_INODES_PER_GROUP(sb), ino);
        if (ino >= EXT2_INODES_PER_GROUP(sb)) {
            /*
             * Rare race: find_group_xx() decided that there were
             * free inodes in this group, but by the time we tried
             * to allocate one, they're all gone.  This can also
             * occur because the counters which find_group_orlov()
             * uses are approximate.  So just go and search the
             * next block group.
             */
            if (++group == sbi->s_groups_count)
                group = 0;
            continue;
        }
        if (ext2_set_bit_atomic(sb_bgl_lock(sbi, group),
                                ino, bitmap_bh->b_data)) {
            /* we lost this inode */
            if (++ino >= EXT2_INODES_PER_GROUP(sb)) {
                /* this group is exhausted, try next group */
                if (++group == sbi->s_groups_count)
                    group = 0;
                continue;
            }
            /* try to find free inode in the same group */
            goto repeat_in_this_group;
        }
        goto got;
    }

    /*
     * Scanned all blockgroups.
     */
    err = -ENOSPC;
    goto fail;
    got:
    mark_buffer_dirty(bitmap_bh);
    if (sb->s_flags & MS_SYNCHRONOUS)
        sync_dirty_buffer(bitmap_bh);
    brelse(bitmap_bh);

    ino += group * EXT2_INODES_PER_GROUP(sb) + 1;
    if (ino < EXT2_FIRST_INO(sb) || ino > le32_to_cpu(es->s_inodes_count)) {
        ext2_error (sb, "ext2_new_inode",
                    "reserved inode or inode > inodes count - "
                            "block_group = %d,inode=%lu", group,
                    (unsigned long) ino);
        err = -EIO;
        goto fail;
    }

    percpu_counter_mod(&sbi->s_freeinodes_counter, -1);
    if (S_ISDIR(mode))
        percpu_counter_inc(&sbi->s_dirs_counter);

    spin_lock(sb_bgl_lock(sbi, group));
    gdp->bg_free_inodes_count =
            cpu_to_le16(le16_to_cpu(gdp->bg_free_inodes_count) - 1);
    if (S_ISDIR(mode)) {
        if (sbi->s_debts[group] < 255)
            sbi->s_debts[group]++;
        gdp->bg_used_dirs_count =
                cpu_to_le16(le16_to_cpu(gdp->bg_used_dirs_count) + 1);
    } else {
        if (sbi->s_debts[group])
            sbi->s_debts[group]--;
    }
    spin_unlock(sb_bgl_lock(sbi, group));

    sb->s_dirt = 1;
    mark_buffer_dirty(bh2);
    inode->i_uid = current->fsuid;
    if (test_opt (sb, GRPID))
        inode->i_gid = dir->i_gid;
    else if (dir->i_mode & S_ISGID) {
        inode->i_gid = dir->i_gid;
        if (S_ISDIR(mode))
            mode |= S_ISGID;
    } else
        inode->i_gid = current->fsgid;
    inode->i_mode = mode;

    inode->i_ino = ino;
    inode->i_blksize = PAGE_SIZE;	/* This is the optimal IO size (for stat), not the fs block size */
    inode->i_blocks = 0;
    inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
    memset(ei->i_data, 0, sizeof(ei->i_data));
    ei->i_flags = EXT2_I(dir)->i_flags & ~EXT2_BTREE_FL;
    if (S_ISLNK(mode))
        ei->i_flags &= ~(EXT2_IMMUTABLE_FL|EXT2_APPEND_FL);
    /* dirsync is only applied to directories */
    if (!S_ISDIR(mode))
        ei->i_flags &= ~EXT2_DIRSYNC_FL;
    ei->i_faddr = 0;
    ei->i_frag_no = 0;
    ei->i_frag_size = 0;
    ei->i_file_acl = 0;
    ei->i_dir_acl = 0;
    ei->i_dtime = 0;
    ei->i_block_group = group;
    ei->i_next_alloc_block = 0;
    ei->i_next_alloc_goal = 0;
    ei->i_prealloc_block = 0;
    ei->i_prealloc_count = 0;
    ei->i_dir_start_lookup = 0;
    ei->i_state = EXT2_STATE_NEW;
    ext2_set_inode_flags(inode);
    spin_lock(&sbi->s_next_gen_lock);
    inode->i_generation = sbi->s_next_generation++;
    spin_unlock(&sbi->s_next_gen_lock);
    insert_inode_hash(inode);

    if (DQUOT_ALLOC_INODE(inode)) {
        DQUOT_DROP(inode);
        err = -ENOSPC;
        goto fail2;
    }
    err = ext2_init_acl(inode, dir);
    if (err) {
        DQUOT_FREE_INODE(inode);
        goto fail2;
    }
    mark_inode_dirty(inode);
    ext2_debug("allocating inode %lu\n", inode->i_ino);
    ext2_preread_inode(inode);
    return inode;

    fail2:
    inode->i_flags |= S_NOQUOTA;
    inode->i_nlink = 0;
    iput(inode);
    return ERR_PTR(err);

    fail:
    make_bad_inode(inode);
    iput(inode);
    return ERR_PTR(err);
}