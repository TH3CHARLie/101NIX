#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/ext2.h>
#include <zjunix/slab.h>
#include <driver/sd.h>
#include <driver/vga.h>

// 外部变量
extern struct dentry                    * root_dentry;              // vfs.c
extern struct dentry                    * pwd_dentry;
extern struct vfsmount                  * root_mnt;
extern struct vfsmount                  * pwd_mnt;

extern struct cache                     * dcache;                   // vfscache.c
extern struct cache                     * pcache;
extern struct cache                     * icache;

extern struct vfs_page* tempp;

// VFS的接口函数
struct super_operations ext2_super_operations = {
    .delete_inode = ext2_delete_inode,
    .write_inode = ext2_write_inode,
};

struct inode_operations ext2_inode_operations[2] = {
    {
        .lookup = ext2_inode_lookup,
        .create = ext2_create,
    },
    {
        .create = ext2_create,
    }
};

struct dentry_operations ext2_dentry_operations = {
    .compare    = generic_compare_filename,
};

struct file_operations ext2_file_operations = {
    .read		= generic_file_read,
    .write      = generic_file_write,
    .flush      = generic_file_flush,
    .readdir    = ext2_readdir,
};

struct address_space_operations ext2_address_space_operations = {
    .writepage  = ext2_writepage,
    .readpage   = ext2_readpage,
    .bmap       = ext2_bmap,
};

//struct super_block * ext2_get_sb() {
//    return；
//}

// 初始化基地址为base（绝对扇区地址）上的EXT2文件系统
u32 init_ext2(u32 base, u8 * DEV_NAME) {
    u32 i;
    u32 err;
    u32 p_location;
    struct vfs_page                     * curPage;
    struct ext2_base_information        * ext2_BI;
    struct file_system_type             * ext2_fs_type;
    struct super_block                  * ext2_sb;
    struct dentry                       * ext2_root_dentry;
    struct inode                        * ext2_root_inode;
    struct vfsmount                     * ext2_root_mnt;

    // 构建 ext2_basic_information 结构
    ext2_BI = (struct ext2_base_information *) kmalloc ( sizeof(struct ext2_base_information) );
    if (ext2_BI == 0)
        return -ENOMEM;
    ext2_BI->ex_base          = base;
    ext2_BI->ex_first_sb_sect = base + EXT2_BOOT_BLOCK_SECT;

    // 构建 ext2_super 结构
    // ext2_BI->sb.data = (u8 *) kmalloc ( sizeof(u8) * EXT2_SUPER_BLOCK_SECT * SECTOR_SIZE );
    ext2_BI->sb.data = (u8 *)kmalloc(sizeof(struct ext2_super_block));
    if (ext2_BI->sb.data == 0)
        return -ENOMEM;
    err = read_block(ext2_BI->sb.data, ext2_BI->ex_first_sb_sect, sizeof(struct ext2_super_block) / SECSIZE);
    if (err)
        return -EIO;

#ifdef DEBUG_EXT2
    kernel_printf(" sizeof ext2_super_block: %lu\n", sizeof(struct ext2_super_block));
    kernel_printf(" s_inodes_count: %d\n", ext2_BI->sb.attr->s_inodes_count);
    kernel_printf(" s_blocks_count: %d\n", ext2_BI->sb.attr->s_blocks_count);
    kernel_printf(" s_free_inodes_count: %d\n", ext2_BI->sb.attr->s_free_inodes_count);
#endif

    ext2_BI->ex_blksize = EXT2_BASE_BLOCK_SIZE << ext2_BI->sb.attr->s_log_block_size;

    // 找到块描述符表的首地址（绝对扇区地址）
    // 此处非常神奇，当块大于1K的时候（例如2K时），超级块会被和BOOT区一起放入第一块
    if (ext2_BI->sb.attr->s_log_block_size == 0)
        ext2_BI->ex_first_gdt_sect = base + EXT2_BOOT_BLOCK_SECT + 2;
    else
        ext2_BI->ex_first_gdt_sect = base + (ext2_BI->ex_blksize >> SECTOR_SHIFT);

#ifdef DEBUG_EXT2
    struct ext2_group_desc* gdt = ext2_get_group_desc(ext2_BI, 1, 1);
    kernel_printf(" sizeof ext2_group_desc: %lu\n", sizeof(struct ext2_group_desc));
    kernel_printf(" bg_free_blocks_count: %d\n", gdt->bg_free_blocks_count);
    kernel_printf(" bg_free_inodes_count: %d\n", gdt->bg_free_inodes_count);
    kernel_printf(" bg_used_dirs_count: %d\n", gdt->bg_used_dirs_count);
#endif

    struct ext2_super_block * ext2_raw_sb = ext2_BI->sb.attr;

    ext2_BI->s_groups_count = (ext2_raw_sb->s_blocks_count -
                               ext2_raw_sb->s_first_data_block +
                               ext2_raw_sb->s_blocks_per_group - 1) / ext2_raw_sb->s_blocks_per_group;

    ext2_BI->s_desc_per_block = ext2_BI->ex_blksize / sizeof(struct ext2_group_desc);

    // 构建 file_system_type 结构
    ext2_fs_type = (struct file_system_type *)kmalloc(sizeof(struct file_system_type));
    if (ext2_fs_type == 0)
        return -ENOMEM;
    ext2_fs_type->name = "ext2";
    ext2_fs_type->next = NULL;
    // INIT_LIST_HEAD(&(ext2_fs_type->fs_supers));
    ext2_fs_type = register_filesystem(ext2_fs_type);

    // 构建 super_block 结构
    ext2_sb = (struct super_block *)kmalloc(sizeof(struct super_block));
    if (ext2_sb == 0)
        return -ENOMEM;
    ext2_sb->s_dirt    = S_CLEAR;
    ext2_sb->s_blksize = ext2_BI->ex_blksize;
    ext2_sb->s_type    = ext2_fs_type;
    ext2_sb->s_root    = 0;
    ext2_sb->s_fs_info = (void*)ext2_BI;
    ext2_sb->s_op      = &ext2_super_operations;

    ext2_sb->s_name = (u8 *)kmalloc(sizeof(u8) * 10);
    for (i = 0; i < 9; i++)
        ext2_sb->s_name[i] = DEV_NAME[i];
    ext2_sb->s_name[9] = 0;

    INIT_LIST_HEAD(&(ext2_sb->s_instances));
    list_add(&ext2_sb->s_instances, &ext2_fs_type->fs_supers);

    // 构建根目录关联的 dentry 结构
    ext2_root_dentry = alloc_dentry();
    if (IS_ERR(ext2_root_dentry))
        return PTR_ERR(ext2_root_dentry);

    ext2_root_dentry->d_name.name       = "/";
    ext2_root_dentry->d_name.len        = 1;
    ext2_root_dentry->d_sb              = ext2_sb;
    ext2_root_dentry->d_op              = &ext2_dentry_operations;

    ext2_sb->s_root = dget(ext2_root_dentry);

//    ext2_root_dentry = (struct dentry *) kmalloc ( sizeof(struct dentry) );
//    if (ext2_root_dentry == 0)
//        return -ENOMEM;
//    ext2_root_dentry->d_count           = 1;
//    ext2_root_dentry->d_mounted         = 0;
//    ext2_root_dentry->d_inode           = 0;
////    ext2_root_dentry->d_parent          = 0;
//    ext2_root_dentry->d_name.name       = "/";
//    ext2_root_dentry->d_name.len        = 1;
//    ext2_root_dentry->d_sb              = ext2_sb;
//    ext2_root_dentry->d_op              = &ext2_dentry_operations;
//    INIT_LIST_HEAD(&(ext2_root_dentry->d_hash));
//    INIT_LIST_HEAD(&(ext2_root_dentry->d_LRU));
//    INIT_LIST_HEAD(&(ext2_root_dentry->d_subdirs));
//    INIT_LIST_HEAD(&(ext2_root_dentry->d_child));
//    INIT_LIST_HEAD(&(ext2_root_dentry->d_alias));
//    dcache->c_op->add(dcache, (void*)ext2_root_dentry);

//    ext2_sb->s_root = ext2_root_dentry;


    // 构建根目录关联的 inode 结构
//    ext2_root_inode = (struct inode *) kmalloc ( sizeof(struct inode) );
//    if (ext2_root_inode == 0)
//        return -ENOMEM;
//    ext2_root_inode->i_count            = 1;
//    ext2_root_inode->i_ino              = EXT2_ROOT_INO;
//    ext2_root_inode->i_op               = &(ext2_inode_operations[0]);
//    ext2_root_inode->i_fop              = &ext2_file_operations;
//    ext2_root_inode->i_sb               = ext2_sb;
//    ext2_root_inode->i_blksize          = ext2_sb->s_blksize;
//    INIT_LIST_HEAD(&(ext2_root_inode->i_hash));
//    INIT_LIST_HEAD(&(ext2_root_inode->i_LRU));
//    INIT_LIST_HEAD(&(ext2_root_inode->i_dentry));
//
//    switch (ext2_root_inode->i_blksize ){
//        case 1024: ext2_root_inode->i_blkbits = 10; break;
//        case 2048: ext2_root_inode->i_blkbits = 11; break;
//        case 4096: ext2_root_inode->i_blkbits = 12; break;
//        case 8192: ext2_root_inode->i_blkbits = 13; break;
//        default: return -EFAULT;
//    }

    ext2_root_inode = alloc_inode(ext2_sb);
    if (IS_ERR(ext2_root_inode))
        return PTR_ERR(ext2_root_inode);

    ext2_root_inode->i_ino              = EXT2_ROOT_INO;
    ext2_root_inode->i_op               = &(ext2_inode_operations[0]);
    ext2_root_inode->i_fop              = &ext2_file_operations;
    // 构建关联的address_space结构
    ext2_root_inode->i_data.a_host      = ext2_root_inode;
    ext2_root_inode->i_data.a_pagesize  = ext2_sb->s_blksize;
    ext2_root_inode->i_data.a_op        = &(ext2_address_space_operations);
    INIT_LIST_HEAD(&(ext2_root_inode->i_data.a_cache));
    // 完成剩余的填充
    err = ext2_fill_inode(ext2_root_inode);
    if (err)
        return err;

    // 与根目录的dentry关联
    ext2_root_dentry->d_inode = ext2_root_inode;
    ext2_root_inode->i_count++;
    list_add(&(ext2_root_dentry->d_alias), &(ext2_root_inode->i_dentry));

    // 预先读取根目录的数据
    for (i = 0; i < ext2_root_inode->i_blocks; i++) {

        p_location = ext2_root_inode->i_data.a_op->bmap(ext2_root_inode, i);
        if (p_location == 0)
            continue;

        curPage = alloc_vfspage(p_location, &ext2_root_inode->i_data);

//        curPage = (struct vfs_page *)kmalloc(sizeof(struct vfs_page));
//        if (curPage == 0)
//            return -ENOMEM;
//
//        curPage->p_state    = P_CLEAR;
//        curPage->p_location = p_location;
//        curPage->p_mapping  = &(ext2_root_inode->i_data);
//        INIT_LIST_HEAD(&(curPage->p_hash));
//        INIT_LIST_HEAD(&(curPage->p_LRU));
//        INIT_LIST_HEAD(&(curPage->p_list));
//
//        err = curPage->p_mapping->a_op->readpage(curPage);
//        if (IS_ERR_VALUE(err)) {
//            release_page(curPage);
//            return err;
//        }

        pcache->c_op->add(pcache, (void*)curPage);
        list_add(&(curPage->p_list), &(curPage->p_mapping->a_cache));
    }


    // 构建本文件系统关联的 vfsmount 结构
    ext2_root_mnt = (struct vfsmount *) kmalloc ( sizeof(struct vfsmount));
    if (ext2_root_mnt == 0)
        return -ENOMEM;
    ext2_root_mnt->mnt_parent        = ext2_root_mnt;
    ext2_root_mnt->mnt_mountpoint    = ext2_root_dentry;
    ext2_root_mnt->mnt_root          = ext2_root_dentry;
    ext2_root_mnt->mnt_sb            = ext2_sb;
    INIT_LIST_HEAD(&(ext2_root_mnt->mnt_hash));
    INIT_LIST_HEAD(&(ext2_root_mnt->mnt_mounts));
    INIT_LIST_HEAD(&(ext2_root_mnt->mnt_child));

#ifdef DEBUG_MNT
    pwd_mnt = root_mnt = ext2_root_mnt;
    tempp = curPage;
#else
    // 加入mnt列表
    list_add(&(ext2_root_mnt->mnt_hash), &(root_mnt->mnt_hash));
#endif

    return 0;
}

// 下面是为ext2专门实现的 super_operations
// 删除内存中的VFS索引节点和磁盘上文件数据及元数据
u32 ext2_delete_inode(struct dentry *dentry){
    u8 *data;
    u8 *start;
    u8 *end;
    u8 *copy_end;
    u8 *copy_start;
    u8 *paste_start;
    u8 *paste_end;
    u8 buffer[SECTOR_SIZE];
    u32 i;
    u32 len;
    u32 err;
    u32 flag;
    u32 sect;
    u32 found;
    u32 block;
    u32 group;
    u32 rec_len;
    u32 curPageNo;
    u32 group_base;       
    u32 blocks_per_group;
    u32 inodes_per_group;
    u32 sectors_per_block;
    u32 block_bitmap_base;              // block bitmap 所属于的组和inode所属于的不一定一样！！
    u32 inode_bitmap_base;
    u32 inode_table_base;
    struct inode                    *dir;
    struct inode                    *inode;
    struct inode                    *dummy;
    struct qstr                     qstr;
    struct condition                cond;
    struct address_space            *mapping;
    struct vfs_page                 *curPage;
    struct ext2_dir_entry_2           *ex_dir_entry;
    struct ext2_base_information    *ext2_BI;

    inode = dentry->d_inode;

    // 找到inode所在组的基地址(绝对扇区)，及下面要用到的基地址
    group_base                          = ext2_group_base_sect(inode);
    sectors_per_block                   = inode->i_blksize >> SECTOR_SHIFT;
    inode_bitmap_base                   = group_base + sectors_per_block;
    inode_table_base                    = group_base + (sectors_per_block << 1);

    // 清除块位图上所有相关的位
    ext2_BI             = (struct ext2_base_information *) inode->i_sb->s_fs_info;
    inodes_per_group    = ext2_BI->sb.attr->s_inodes_per_group;
    blocks_per_group    = ext2_BI->sb.attr->s_blocks_per_group;

    dummy = (struct inode*) kmalloc(sizeof(struct inode));          // 构造一个虚inode以获得block_bitmap_base
    dummy->i_blksize    = inode->i_blksize;
    dummy->i_sb         = inode->i_sb;

    for ( i = 0; i < inode->i_blocks; i++ ){
        block = ext2_bmap(inode, i);
        if  (block == 0)
            continue;

        group = block / blocks_per_group;
        dummy->i_ino = group * inodes_per_group + 1;
        block_bitmap_base = ext2_group_base_sect(dummy);

        sect = block_bitmap_base + ( block % blocks_per_group) / SECTOR_SIZE / BITS_PER_BYTE;

        err = read_block(buffer, sect, 1);
        if (err)
            return -EIO;

        reset_bit(buffer, (block % blocks_per_group) % (SECTOR_SIZE * BITS_PER_BYTE));

        err = write_block(buffer, sect, 1);
        if (err)
            return -EIO;

        err = read_block(buffer, sect, 1);
        if (err)
            return -EIO;

    }

    // 清除inode位图上相关的位，记住 没有 零号inode的bitmap
    sect                = inode_bitmap_base + ((inode->i_ino - 1) % inodes_per_group) / SECTOR_SIZE / BITS_PER_BYTE;
    err = read_block(buffer, sect, 1);
    if (err)
        return -EIO;

    reset_bit(buffer, ((inode->i_ino - 1) % inodes_per_group) % (SECTOR_SIZE * BITS_PER_BYTE));

    err = write_block(buffer, sect, 1);
    if (err)
        return -EIO;

    err = read_block(buffer, sect, 1);
    if (err)
        return -EIO;
    

    // 清除inode表上的数据，记住 没有 零号inode的table item
    sect = inode_table_base + (inode->i_ino - 1) % inodes_per_group / ( SECTOR_SIZE / ext2_BI->sb.attr->s_inode_size);

    err = read_block(buffer, sect, 1);
    if (err)
        return -EIO;
    
    kernel_memset(buffer + ((inode->i_ino - 1) % inodes_per_group % ( SECTOR_SIZE / ext2_BI->sb.attr->s_inode_size)) * ext2_BI->sb.attr->s_inode_size,
                    0, ext2_BI->sb.attr->s_inode_size);
    
    err = write_block(buffer, sect, 1);
    if (err)
        return -EIO;

    // 修改sb, 并使所有sb和gdt一致略

    // 清除父目录数据块中相应的目录项，并把后面的目录项向前移动
    flag = 0;
    found = 0;
    rec_len = 0;
    copy_start = 0;
    dir = dentry->d_parent->d_inode;
    mapping = &(dir->i_data);
    for ( i = 0; i < dir->i_blocks; i++){      // 对父目录关联的每一页
        curPageNo = mapping->a_op->bmap(dir, i);
        if (curPageNo == 0)
            return -ENOENT;

        // 首先在页高速缓存中寻找
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = pcache->c_op->look_up(pcache, &cond);

        // 如果页高速缓存中没有，则需要在外存中寻找（一定能够找到，因为不是创建文件）
        if (curPage == 0) {

            curPage = alloc_vfspage(curPageNo, mapping);
//
//            curPage = (struct vfs_page *) kmalloc ( sizeof(struct vfs_page) );
//            if (!curPage)
//                return -ENOMEM;
//
//            curPage->p_state    = P_CLEAR;
//            curPage->p_location = curPageNo;
//            curPage->p_mapping  = mapping;
//            INIT_LIST_HEAD(&(curPage->p_hash));
//            INIT_LIST_HEAD(&(curPage->p_LRU));
//            INIT_LIST_HEAD(&(curPage->p_list));
//
//            err = mapping->a_op->readpage(curPage);
//            if (IS_ERR_VALUE(err)) {
//                release_page(curPage);
//                return 0;
//            }
//
//            curPage->p_state = P_CLEAR;

            pcache->c_op->add(pcache, (void*)curPage);
            list_add(&(curPage->p_list), &(mapping->a_cache));
        }

        //现在data指向的数据就是页的数据。对每一个目录项
        data = curPage->p_data;
        start = data;
        end = data + inode->i_blksize;
        while ( *data != 0 && data != end) {
            ex_dir_entry = (struct ext2_dir_entry_2 *)data;

            if (found){                                     // 确定需要前移的目录项组的始末位置
                if (flag == 0) {          
                    copy_start = data;
                    flag = 1;
                }
                copy_end = data + ex_dir_entry->rec_len;
            }
            else {
                qstr.len  = ex_dir_entry->name_len;
                qstr.name = ex_dir_entry->name;
                if (generic_compare_filename(&qstr, &(dentry->d_name)) == 0) {  // 如果找到相应的目录项
                    paste_start = data;
                    found = 1;
                    rec_len = ex_dir_entry->rec_len;
                }
            }  
            data += (ex_dir_entry->rec_len);
        }

        if (found)
            break;                              // 跳出的是对每一页的循环
    }

    // 如果没找到相应的inode
    if (!found)
        return -ENOENT;

    // 抹掉原来目录项的信息
    for ( i = 0; i < rec_len; i++ )
        *(paste_start+i) = 0;

    // 如果被删除的目录项后面有目录项，需要前移
    if (copy_start != 0) {
        len = (u32)copy_end - (u32)copy_start;              // 前移
        kernel_memcpy(paste_start, copy_start, len);

        paste_end = paste_start + len;
        len = (u32)copy_end - (u32)paste_end;               // 清理尾巴
        for ( i = 0; i < len; i++ )
            *(paste_end+i) = 0;

        err = ext2_writepage(curPage);                      // 写回内存
        if (err)
            return err;
    }

    return 0;
}

// 用通过传递参数指定的索引节点对象的内容更新一个文件系统的索引节点
u32 ext2_write_inode(struct inode * inode, struct dentry * parent){
    u8 buffer[SECTOR_SIZE];
    u32 i;
    u32 err;
    u32 sect;
    u32 group_sect_base;
    u32 inodes_per_group;
    struct ext2_inode               * ex_inode;
    struct ext2_base_information    * ext2_BI;

    // 首先的得到对应组的基地址（块位图所在的块）
    group_sect_base = ext2_group_base_sect(inode);
    if (group_sect_base == 0)
        return -EIO;

    // 接下来找到inode数据（inode表内）所在的绝对扇区，并读入相应的数据
    ext2_BI             = (struct ext2_base_information *) inode->i_sb->s_fs_info;
    inodes_per_group    = ext2_BI->sb.attr->s_inodes_per_group;
    sect                = group_sect_base + 2 * (inode->i_blksize >> SECTOR_SHIFT) + \
                            ( (inode->i_ino - 1) % inodes_per_group ) / ( SECTOR_SIZE / ext2_BI->sb.attr->s_inode_size);
    err = read_block(buffer, sect, 1);
    if (err)
        return -EIO;
    
    ex_inode            = (struct ext2_inode *)(buffer + \
        (inode->i_ino - 1) % ( SECTOR_SIZE / ext2_BI->sb.attr->s_inode_size) * ext2_BI->sb.attr->s_inode_size );

    // 然后修改buffer中VFS的inode信息
    ex_inode->i_size                   = inode->i_size;

    // 把修改写入外存
    err = write_block(buffer, sect, 1);
    if (err)
        return -EIO;

    return 0;
}

// 下面是为ext2专门实现的 inode_operations
// 尝试在外存中查找需要的dentry对应的inode。若找到，相应的inode会被新建并加入高速缓存，dentry与之的联系也会被建立
struct dentry * ext2_inode_lookup(struct inode * dir, struct dentry * dentry, struct nameidata * nd) {
    u8 *data;
    u8 *end;
    u32 i;
    u32 err;
    u32 addr;
    u32 found;
    u32 curPageNo;
    struct condition                        cond;
    struct qstr                             qstr;
    struct vfs_page                         * curPage;
    struct address_space                    * mapping;
    struct inode                            * new_inode;
    struct ext2_dir_entry_2                   * ex_dir_entry;

    found = 0;
    new_inode = 0;
    mapping = &(dir->i_data);

    // 对目录关联的每一页
    for ( i = 0; i < dir->i_blocks; i++){
        curPageNo = mapping->a_op->bmap(dir, i);
        if (curPageNo == 0)
            return ERR_PTR(-ENOENT);

        // 首先在页高速缓存中寻找
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = (struct vfs_page *) pcache->c_op->look_up(pcache, &cond);

        // 如果页高速缓存中没有，则需要在外存中寻找（一定能够找到，因为不是创建文件）
        if ( curPage == 0 ){
            curPage = (struct vfs_page *) kmalloc ( sizeof(struct vfs_page) );
            if (!curPage)
                return ERR_PTR(-ENOMEM);

            curPage->p_state    = P_CLEAR;
            curPage->p_location = curPageNo;
            curPage->p_mapping  = mapping;
            INIT_LIST_HEAD(&(curPage->p_hash));
            INIT_LIST_HEAD(&(curPage->p_LRU));
            INIT_LIST_HEAD(&(curPage->p_list));

            err = mapping->a_op->readpage(curPage);
            if ( IS_ERR_VALUE(err) ){
                release_page(curPage);
                return 0;
            }

            curPage->p_state = P_CLEAR;
            pcache->c_op->add(pcache, (void*)curPage);
            list_add(&(curPage->p_list), &(mapping->a_cache));
        }

        //现在data指向的数据就是页的数据。对每一个目录项
        data = curPage->p_data;
        end = data + dir->i_blksize;
        while ( *data != 0 && data != end ) {
            ex_dir_entry = (struct ext2_dir_entry_2 *)data;
            qstr.len = ex_dir_entry->name_len;
            qstr.name = ex_dir_entry->name;

            if ( generic_compare_filename( &qstr, &(dentry->d_name) ) == 0 ){
                // 初步填充inode的相应信息
                new_inode = (struct inode*) kmalloc ( sizeof(struct inode) );
                new_inode->i_ino            = ex_dir_entry->ino;
                new_inode->i_blksize        = dir->i_blksize;
                new_inode->i_sb             = dir->i_sb;

                // 通过检查inode位图来检查inode是否已被删除
                if ( 0 == ext2_check_inode_bitmap(new_inode)){
                    kfree(new_inode);
                    data += (ex_dir_entry->rec_len);
                    continue;
                }

                new_inode->i_count          = 1;
                new_inode->i_blkbits        = dir->i_blkbits;
                new_inode->i_fop            = &(ext2_file_operations);
                INIT_LIST_HEAD(&(new_inode->i_dentry));

                ext2_fill_inode(new_inode);

                if ( ex_dir_entry->file_type == EXT2_FT_DIR )                      // 目录和非目录有不同的inode方法
                    new_inode->i_op         = &(ext2_inode_operations[0]);
                else
                    new_inode->i_op         = &(ext2_inode_operations[1]);

                // 填充关联的address_space结构
                new_inode->i_data.a_host        = new_inode;
                new_inode->i_data.a_pagesize    = new_inode->i_blksize;
                new_inode->i_data.a_op          = &(ext2_address_space_operations);
                INIT_LIST_HEAD(&(new_inode->i_data.a_cache));

                // 把inode放入高速缓存
                // icache->c_op->add(icache, (void*)new_inode);
                found = 1;
                break;
            }
            data += (ex_dir_entry->rec_len);
        }
        if (found)
            break;                              // 跳出的是对每一页的循环
    }

    // 如果没找到相应的inode
    if (!found)
        return 0;

    // 完善dentry的信息
    dentry->d_inode = new_inode;
    dentry->d_op = &ext2_dentry_operations;
    list_add(&dentry->d_alias, &new_inode->i_dentry);
    
    
    return dentry;
}

u32 ext2_create(struct inode *dir, struct dentry *dentry, u32 mode, struct nameidata *nd) {
    return 0;
};

// 下面是为fat32专门实现的file operations
u32 ext2_readdir(struct file * file, struct getdent * getdent){
    u8 *data;
    u8 *name;
    u8 *end;
    u32 i;
    u32 j;
    u32 err;
    u32 pagesize;
    u32 curPageNo;
    struct inode                    *dir;
    struct inode                    *new_inode;
    struct qstr                     qstr;
    struct condition                cond;
    struct vfs_page                 *curPage;
    struct address_space            *mapping;
    struct ext2_dir_entry_2           *ex_dir_entry;

    dir = file->f_dentry->d_inode;
    mapping = &(dir->i_data);
    pagesize = dir->i_blksize;
    getdent->count = 0;
    getdent->dirent = (struct dirent *) kmalloc ( sizeof(struct dirent) * (MAX_DIRENT_NUM));
    if (getdent->dirent == 0)
        return -ENOMEM;

    // 对目录关联的每一页
    for ( i = 0; i < dir->i_blocks; i++){
        curPageNo = mapping->a_op->bmap(dir, i);
        if (curPageNo == 0)
            continue;

        // 首先在页高速缓存中寻找
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = (struct vfs_page *) pcache->c_op->look_up(pcache, &cond);

        // 如果页高速缓存中没有，则需要在外存中寻找（一定能够找到，因为不是创建文件）
        if ( curPage == 0 ){
            curPage = (struct vfs_page *) kmalloc ( sizeof(struct vfs_page) );
            if (!curPage)
                return -ENOMEM;

            curPage->p_state    = P_CLEAR;
            curPage->p_location = curPageNo;
            curPage->p_mapping  = mapping;
            INIT_LIST_HEAD(&(curPage->p_hash));
            INIT_LIST_HEAD(&(curPage->p_LRU));
            INIT_LIST_HEAD(&(curPage->p_list));

            err = mapping->a_op->readpage(curPage);
            if ( IS_ERR_VALUE(err) ){
                release_page(curPage);
                return 0;
            }

            curPage->p_state = P_CLEAR;
            pcache->c_op->add(pcache, (void*)curPage);
            list_add(&(curPage->p_list), &(mapping->a_cache));
        }

        //现在data指向的数据就是页的数据。对每一个目录项
        data = curPage->p_data;
        end = data + dir->i_blksize;
        while ( *data != 0 && data != end ) {
            ex_dir_entry = (struct ext2_dir_entry_2 *)data;
            new_inode = (struct inode*) kmalloc ( sizeof(struct inode) );
            new_inode->i_ino            = ex_dir_entry->ino;
            new_inode->i_blksize        = dir->i_blksize;
            new_inode->i_sb             = dir->i_sb;

            // 通过检查inode位图来检查inode是否已被删除
            if ( 0 == ext2_check_inode_bitmap(new_inode)){
                kfree(new_inode);
                data += (ex_dir_entry->rec_len);
                continue;
            }

            qstr.len    = ex_dir_entry->name_len;
            qstr.name   = ex_dir_entry->name;

            name = 0;
            name = (u8 *) kmalloc ( sizeof(u8) * ( ex_dir_entry->name_len + 1 ));
            if (name == 0)
                return -ENOMEM;
            for ( j = 0; j < ex_dir_entry->name_len; j++)
                name[j] = qstr.name[j];
            name[j] = 0;

            getdent->dirent[getdent->count].ino     = ex_dir_entry->ino;
            getdent->dirent[getdent->count].name    = name;
            getdent->dirent[getdent->count].type    = ex_dir_entry->file_type;
            getdent->count += 1;
            
            data += (ex_dir_entry->rec_len);
        }   // 页内循环
    }   // 页际循环

    return 0;
}
//
//// 找到inode所在组的基地址
//u32 ext2_group_base_sect(struct inode * inode) {
//    struct ext2_base_information * ext2_BI;
//
//    ext2_BI =(struct ext2_base_information *)inode->i_sb->s_fs_info;
//
//    struct ext2_group_desc * gdt = ext2_get_group_gdt(ext2_BI, inode->i_ino);
//    if (gdt == 0)
//        return 0;
//
//    u32 group_block_base = gdt->bg_block_bitmap;
//    u32 group_sect_base  = ext2_BI->ex_base + group_block_base * (inode->i_blksize >> SECTOR_SHIFT);
//
//    return group_sect_base;
//}