#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/ext2.h>
#include <zjunix/slab.h>
#include <zjunix/vfs/vfscache.h>

extern struct file_operations ext2_dir_operations;
extern struct inode_operations ext2_dir_inode_operations;
extern struct address_space_operations ext2_address_space_operations;

extern struct cache *pcache;

void ext2_set_de_type(struct ext2_dir_entry_2 * de, struct inode * inode) {
    if (S_ISDIR(inode->i_mode))
        de->file_type = EXT2_FT_DIR;
    else
        de->file_type = EXT2_FT_REG_FILE;
}

void ext2_append_to_end(struct inode * inode, struct ext2_dir_entry_2 *in, u32 len) {
    u8 *data, *end;
    u32 i, j, dirt = 0;
    struct vfs_page * page;
    struct ext2_dir_entry_2 *ex_dir_entry;

#ifdef DEBUG_EXT2
    kernel_printf("%sbegin ext2_append_to_end(%d, %s)\n", quad1, inode->i_ino, in->name);
#endif

    for (i = 0; i < EXT2_FIRST_MAP_INDEX; i++) {
        if (inode->i_data.a_page[i] == 0) {
            inode->i_data.a_page[i] = ext2_new_block(inode);
            inode->i_blocks++;
            inode->i_size += 4096;
            ext2_reset_inode(EXT2_SB(inode->i_sb), 0, inode);
        }
        page = pcache_get_page(pcache, inode, i);

        data = page->p_data;
        end = data + inode->i_blksize;
        dirt = 0;
        while (data < end) {
            ex_dir_entry = (struct ext2_dir_entry_2 *)data;

#ifdef DEBUG_EXT2
            kernel_printf("     show ex_dir_entry: %d\n", ex_dir_entry->ino);
            kernel_printf("               rec_len: %d\n", ex_dir_entry->rec_len);
            kernel_printf("              name_len: %d\n", ex_dir_entry->name_len);
            kernel_printf("                  type: %d\n", ex_dir_entry->file_type);
            kernel_printf("                  name: %s\n", ex_dir_entry->name);
#endif

            if (ex_dir_entry->rec_len == 0) {
                in->rec_len = 4096;
                kernel_memcpy(data, in, 8);
                kernel_memcpy(data + 8, in->name, in->name_len);
                dirt = 1;
#ifdef DEBUG_EXT2
                kernel_printf("            now at end:\n");
#endif
                break;
            }
            else if ((data + ex_dir_entry->rec_len >= end &&
                    end - data - EXT2_DIR_REC_LEN(ex_dir_entry->name_len) >= in->rec_len) ||
                    (ex_dir_entry->rec_len - EXT2_DIR_REC_LEN(ex_dir_entry->name_len) >= in->rec_len)) {
                in->rec_len = ex_dir_entry->rec_len - EXT2_DIR_REC_LEN(ex_dir_entry->name_len);
                ex_dir_entry->rec_len = EXT2_DIR_REC_LEN(ex_dir_entry->name_len);
                kernel_memcpy(data, ex_dir_entry, 8);
                kernel_memcpy(data + 8, ex_dir_entry->name, ex_dir_entry->name_len);
                kernel_memcpy(data + ex_dir_entry->rec_len, in, 8);
                kernel_memcpy(data + ex_dir_entry->rec_len + 8, in->name, in->name_len);
                dirt = 1;
                break;
            }
            else
                data += (ex_dir_entry->rec_len);
        }
//        if (data == page->p_data) {
//            in->rec_len = 4096;
//            kernel_memcpy(data, in, 8);
//            kernel_memcpy(data + 8, in->name, in->name_len);
//            dirt = 1;
//        }
        if (dirt) {
#ifdef DEBUG_EXT2
            kernel_printf("%send ext2_append_to_end: dirt page: %d\n", quad1, i);
#endif
            ext2_writepage(page);
            break;
        }
    }

}

u32 ext2_make_empty(struct inode * inode, struct inode * parent) {
    struct ext2_dir_entry_2 * de;

    de = (struct ext2_dir_entry_2 *)kmalloc(sizeof(struct ext2_dir_entry_2));
    if (de == 0)
        return -ENOMEM;

    // 添加当前目录'.'
    de->ino = inode->i_ino;
    de->name_len = 1;
    de->rec_len = EXT2_DIR_REC_LEN(1);
    kernel_memcpy(de->name, ".\0\0", 4);
    de->name[de->name_len] = '\0';
    ext2_set_de_type(de, inode);
    ext2_append_to_end(inode, de, de->rec_len);

    // 添加父目录'..'
    de->ino = parent->i_ino;
    de->name_len = 2;
    de->rec_len = EXT2_DIR_REC_LEN(2);
    kernel_memcpy(de->name, "..\0", 4);
    de->name[de->name_len] = '\0';
    ext2_set_de_type(de, parent);
    ext2_append_to_end(inode, de, de->rec_len);

    return 0;
}

// 将dentry(对应inode)添加到dentry->d_parent的目录下
u32 ext2_add_link(struct dentry *dentry, struct inode *inode) {
    struct inode *dir = dentry->d_parent->d_inode;
    struct ext2_dir_entry_2 * de;

#ifdef DEBUG_EXT2
    kernel_printf("now in ext2_add_link(%s, %s)\n", dentry->d_parent->d_name.name,
                                                    dentry->d_name.name);
#endif

    de = (struct ext2_dir_entry_2 *)kmalloc(sizeof(struct ext2_dir_entry_2));
    if (de == 0)
        return -ENOMEM;

    // 添加当前目录
    de->ino = inode->i_ino;
    de->name_len = dentry->d_name.len;
    de->rec_len = EXT2_DIR_REC_LEN(dentry->d_name.len);
    kernel_strcpy(de->name, dentry->d_name.name);
    de->name[de->name_len] = '\0';
    ext2_set_de_type(de, inode);
    ext2_append_to_end(dir, de, de->rec_len);

    return 0;
}

// 在dir目录下，创建一个新的目录dentry
u32 ext2_mkdir(struct inode * dir, struct dentry * dentry, u32 mode) {
    struct inode * inode;
    u32 err = 0;

#ifdef DEBUG_EXT2
    kernel_printf("%snow in ext2_mkdir()\n", quad1);
#endif

    mode |= S_IFDIR;

    // 找到硬盘上一块空位，分配完成后，得到对应的VFS inode
    inode = ext2_new_inode(dir, mode);
    err = PTR_ERR(inode);
    if (IS_ERR(inode))
        goto out_dir;

    inode->i_op =  &ext2_dir_inode_operations;
    inode->i_fop = &ext2_dir_operations;
    inode->i_data.a_op = &(ext2_address_space_operations);

    // 清空目录，添加初始化的.和..两个目录
    err = ext2_make_empty(inode, dir);
    if (err)
        goto out_fail;

    // 在父目录后添加该目录
    err = ext2_add_link(dentry, inode);
    if (err)
        goto out_fail;

    // 连接父目录与当前新建目录
    dentry->d_inode = inode;
    inode->i_count++;
    list_add(&(dentry->d_alias), &(inode->i_dentry));

out:
    return err;

out_fail:
    release_inode(inode);
out_dir:
    goto out;
}

