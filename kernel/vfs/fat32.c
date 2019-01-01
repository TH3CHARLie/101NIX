#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/fat32.h>

#include <zjunix/log.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/utils.h>

// 外部变量
extern struct dentry                    * root_dentry;              // vfs.c
extern struct dentry                    * pwd_dentry;
extern struct vfsmount                  * root_mnt;
extern struct vfsmount                  * pwd_mnt;

extern struct cache                     * dcache;                   // vfscache.c
extern struct cache                     * pcache;
extern struct cache                     * icache;

struct vfs_page * tempp;

// VFS的接口函数
struct super_operations fat32_super_operations = {
    .delete_inode   = fat32_delete_inode,
    .write_inode    = fat32_write_inode,
};

struct inode_operations fat32_inode_operations[2] = {
    {
        .lookup = fat32_inode_lookup,
        .create = fat32_create,
    },
    {
        .create = fat32_create,
    }
};

struct dentry_operations fat32_dentry_operations = {
    .compare    = generic_compare_filename,
};

struct file_operations fat32_file_operations = {
    .read		= generic_file_read,
    .write      = generic_file_write,
    .flush      = generic_file_flush,
    .readdir    = fat32_readdir,
};

struct address_space_operations fat32_address_space_operations = {
    .writepage  = fat32_writepage,
    .readpage   = fat32_readpage,
    .bmap       = fat32_bmap,
};

// 初始化基地址为base（绝对扇区地址）上的FAT32文件系统
u32 init_fat32(u32 base){
    u32 i;
    u32 j;
    u32 k;
    u32 next_clu;
    u32 temp;
    u32 err;
    struct fat32_basic_information  * fat32_BI;
    struct file_system_type         * fat32_fs_type;
    struct super_block              * fat32_sb;
    struct vfs_page                 * curPage;

    // 构建 fat32_basic_information 结构
    fat32_BI = (struct fat32_basic_information *) kmalloc ( sizeof(struct fat32_basic_information) );
    if (fat32_BI == 0)
        return -ENOMEM;
    fat32_BI->fa_DBR    = 0;
    fat32_BI->fa_FSINFO = 0;
    fat32_BI->fa_FAT    = 0;

    // 构建 fat32_dos_boot_record 结构
    fat32_BI->fa_DBR = (struct fat32_dos_boot_record *) kmalloc ( sizeof(struct fat32_dos_boot_record) );
    if (fat32_BI->fa_DBR == 0)
        return -ENOMEM;
    fat32_BI->fa_DBR->base = base;
    kernel_memset(fat32_BI->fa_DBR->data, 0, sizeof(fat32_BI->fa_DBR->data));
    err = read_block(fat32_BI->fa_DBR->data, fat32_BI->fa_DBR->base, 1);        // DBR在基地址所在的一个扇区
    if (err)
        return -EIO;

    fat32_BI->fa_DBR->sec_per_clu   = *(fat32_BI->fa_DBR->data + 0x0D);
    fat32_BI->fa_DBR->reserved      = get_u16 (fat32_BI->fa_DBR->data + 0x0E);
    fat32_BI->fa_DBR->fat_num       = *(fat32_BI->fa_DBR->data + 0x10);
    fat32_BI->fa_DBR->fat_size      = get_u32 (fat32_BI->fa_DBR->data + 0x24);
    fat32_BI->fa_DBR->root_clu      = get_u32 (fat32_BI->fa_DBR->data + 0x2C);

    // 构建 fat32_file_system_information 结构
    fat32_BI->fa_FSINFO = (struct fat32_file_system_information *) kmalloc \
        ( sizeof(struct fat32_file_system_information) );
    if (fat32_BI->fa_FSINFO == 0)
        return -ENOMEM;
    fat32_BI->fa_FSINFO->base = fat32_BI->fa_DBR->base + 1;                     // FSINFO在基地址后一个扇区
    kernel_memset(fat32_BI->fa_FSINFO->data, 0, sizeof(fat32_BI->fa_FSINFO->data));
    err = read_block(fat32_BI->fa_FSINFO->data, fat32_BI->fa_FSINFO->base, 1);
    if (err)
        return -EIO;

    // 构建 fat32_file_allocation_table 结构
    fat32_BI->fa_FAT = (struct fat32_file_allocation_table *) kmalloc \
        ( sizeof(struct fat32_file_allocation_table) );
    if (fat32_BI->fa_FAT == 0)
        return -ENOMEM;
    fat32_BI->fa_FAT->base = base + fat32_BI->fa_DBR->reserved;                 // FAT起始于非保留扇区开始的扇区

    fat32_BI->fa_FAT->data_sec = fat32_BI->fa_FAT->base + fat32_BI->fa_DBR->fat_num * \
        fat32_BI->fa_DBR->fat_size;
    fat32_BI->fa_FAT->root_sec = fat32_BI->fa_FAT->data_sec + \
        ( fat32_BI->fa_DBR->root_clu - 2 ) * fat32_BI->fa_DBR->sec_per_clu;
                                                                                // 因为0、1号簇没有对应任何扇区

    // 构建 file_system_type 结构
    fat32_fs_type = (struct file_system_type *) kmalloc ( sizeof(struct file_system_type) );
    if (fat32_fs_type == 0)
        return -ENOMEM;
    fat32_fs_type->name = "fat32";

    // 构建 super_block 结构
    fat32_sb = (struct super_block *) kmalloc ( sizeof(struct super_block) );
    if (fat32_sb == 0)
        return -ENOMEM;
    fat32_sb->s_dirt    = S_CLEAR;
    fat32_sb->s_blksize = fat32_BI->fa_DBR->sec_per_clu << SECTOR_SHIFT;
    fat32_sb->s_type    = fat32_fs_type;
    fat32_sb->s_root    = 0;
    fat32_sb->s_fs_info = (void*)fat32_BI;
    fat32_sb->s_op      = &fat32_super_operations;

    // 构建根目录关联的 dentry 结构
    root_dentry = (struct dentry *) kmalloc ( sizeof(struct dentry) );
    if (root_dentry == 0)
        return -ENOMEM;
    root_dentry->d_count        = 1;
    root_dentry->d_mounted      = 0;
    root_dentry->d_inode        = 0;
    root_dentry->d_parent       = 0;
    root_dentry->d_name.name    = "/";
    root_dentry->d_name.len     = 1;
    root_dentry->d_sb           = fat32_sb;
    root_dentry->d_op           = &fat32_dentry_operations;
    INIT_LIST_HEAD(&(root_dentry->d_hash));
    INIT_LIST_HEAD(&(root_dentry->d_LRU));
    INIT_LIST_HEAD(&(root_dentry->d_subdirs));
    INIT_LIST_HEAD(&(root_dentry->d_child));
    INIT_LIST_HEAD(&(root_dentry->d_alias));
    dcache->c_op->add(dcache, (void*)root_dentry);

    pwd_dentry = root_dentry;
    fat32_sb->s_root = root_dentry;

    // 构建根目录关联的 inode 结构
    struct inode* root_inode;
    root_inode = (struct inode *) kmalloc ( sizeof(struct inode) );
    if (root_inode == 0)
        return -ENOMEM;
    root_inode->i_count             = 1;
    root_inode->i_ino               = fat32_BI->fa_DBR->root_clu;
    root_inode->i_op                = &(fat32_inode_operations[0]);
    root_inode->i_fop               = &fat32_file_operations;
    root_inode->i_sb                = fat32_sb;
    root_inode->i_blocks            = 0;
    INIT_LIST_HEAD(&(root_inode->i_dentry));
    // root_inode->i_size              = fat32_BI->fa_FAT->fat.table[fat32_BI->fa_DBR->root_clu].size;
    // TODO 得到i_size
    root_inode->i_blksize           = fat32_sb->s_blksize;


    switch (root_inode->i_blksize ){
        case 1024: root_inode->i_blkbits = 10; break;
        case 2048: root_inode->i_blkbits = 11; break;
        case 4096: root_inode->i_blkbits = 12; break;
        case 8192: root_inode->i_blkbits = 13; break;
        default: return -EFAULT;
    }
    INIT_LIST_HEAD(&(root_inode->i_hash));
    INIT_LIST_HEAD(&(root_inode->i_LRU));

    // icache->c_op->add(icache, (void *)root_inode);

    // kernel_printf("here ok1\n");
    // return -1;
    
    // 与root_dentry关联
    root_dentry->d_inode = root_inode;
    list_add(&(root_dentry->d_alias), &(root_inode->i_dentry));


    // 构建根目录inode结构中的address_space结构
    root_inode->i_data.a_host       = root_inode;
    root_inode->i_data.a_pagesize   = fat32_sb->s_blksize;
    root_inode->i_data.a_op         = &(fat32_address_space_operations);
    INIT_LIST_HEAD(&(root_inode->i_data.a_cache));
    
    i = 0;
    next_clu = fat32_BI->fa_DBR->root_clu;
    while ( 0x0FFFFFFF != next_clu ){
        root_inode->i_blocks ++;
        next_clu = read_fat(root_inode, next_clu);          // 读FAT32表
    }

    root_inode->i_data.a_page = (u32 *) kmalloc ( root_inode->i_blocks * sizeof(u32) );
    if (root_inode->i_data.a_page == 0)
            return -ENOMEM;
    kernel_memset(root_inode->i_data.a_page, 0, root_inode->i_blocks);

    next_clu = fat32_BI->fa_DBR->root_clu;
    for (i = 0; i < root_inode->i_blocks; i++){
        root_inode->i_data.a_page[i] = next_clu;
        next_clu = read_fat(root_inode, next_clu);
    }

    // kernel_printf("here ok1\n");
    //     return -1;

    // 预先读取根目录的数据
    for (i = 0; i < root_inode->i_blocks; i++){
        curPage = (struct vfs_page *) kmalloc ( sizeof(struct vfs_page) );
        if (curPage == 0)
            return -ENOMEM;

        curPage->p_state = P_CLEAR;
        curPage->p_location = root_inode->i_data.a_page[i];
        curPage->p_mapping = &(root_inode->i_data);
        INIT_LIST_HEAD(&(curPage->p_hash));
        INIT_LIST_HEAD(&(curPage->p_LRU));
        INIT_LIST_HEAD(&(curPage->p_list));

        err = curPage->p_mapping->a_op->readpage(curPage);
        if ( IS_ERR_VALUE(err) ) {
            release_page(curPage);
            return err;
        }
        
        pcache->c_op->add(pcache, (void*)curPage);
        list_add(&(curPage->p_list), &(curPage->p_mapping->a_cache));
        tempp = curPage;
    }

    // 构建本文件系统关联的 vfsmount 结构
    root_mnt = (struct vfsmount *) kmalloc ( sizeof(struct vfsmount));
    if (root_mnt == 0)
        return -ENOMEM;
    root_mnt->mnt_parent        = root_mnt;
    root_mnt->mnt_mountpoint    = root_dentry;
    root_mnt->mnt_root          = root_dentry;
    root_mnt->mnt_sb            = fat32_sb;
    INIT_LIST_HEAD(&(root_mnt->mnt_hash));

    pwd_mnt = root_mnt;

    tempp = curPage;

    return 0;
}

// 下面是为fat32专门实现的 super_operations
// 删除内存中的VFS索引节点和磁盘上文件数据及元数据
u32 fat32_delete_inode(struct dentry *dentry){
    u8 name[MAX_FAT32_SHORT_FILE_NAME_LEN];
    u32 i;
    u32 j;
    u32 err;
    u32 found;
    u32 begin;
    u32 pagesize;
    u32 curPageNo;
    struct qstr                 qstr;
    struct qstr                 qstr2;
    struct inode                *dir;
    struct inode                *inode;
    struct condition            cond;
    struct vfs_page             *curPage;
    struct address_space        *mapping;
    struct fat_dir_entry        *fat_dir_entry;

    // 对父目录的每一页
    
    found = 0;
    inode = dentry->d_inode;
    dir = dentry->d_parent->d_inode;
    mapping = &(dir->i_data);
    pagesize = inode->i_blksize;

    for ( i = 0; i < dir->i_blocks; i++){
        curPageNo = mapping->a_op->bmap(dir, i);
        
        // 首先在页高速缓存中寻找
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = (struct vfs_page *)pcache->c_op->look_up(pcache, &cond);
        
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

        //现在p_data指向的数据就是页的数据。假定页里面的都是fat32短文件目录项。对每一个目录项
        for ( begin = 0; begin < pagesize; begin += FAT32_DIR_ENTRY_LEN ){
            fat_dir_entry = (struct fat_dir_entry *)(curPage->p_data + begin);

            // 先判断是不是短文件名，如果不是的话跳过（08 卷标、0F长文件名）
            if (fat_dir_entry->attr == 0x08 || fat_dir_entry->attr == 0x0F)
                continue;
            
            // 再判断是不是已删除的文件，是的话跳过
            if (fat_dir_entry->name[0] == 0xE5)
                continue;

            // 再判断是不是没有目录项了
            if (fat_dir_entry->name[0] == '\0')
                break;
            
            // 有目录项的话，提取其名字
            kernel_memset( name, 0, MAX_FAT32_SHORT_FILE_NAME_LEN * sizeof(u8) );
            for ( j = 0; j < MAX_FAT32_SHORT_FILE_NAME_LEN; j++ )
                name[j] = fat_dir_entry->name[j];
            qstr.name = name;
            qstr.len = MAX_FAT32_SHORT_FILE_NAME_LEN;

            // 转换名字
            fat32_convert_filename(&qstr2, &qstr, fat_dir_entry->lcase, FAT32_NAME_SPECIFIC_TO_NORMAL);

            // 如果与待找的名字相同，则标记此目录项为删除
            if ( generic_compare_filename( &qstr2, &(dentry->d_name) ) == 0 ){
                // kernel_printf("%s\n", qstr.name);
                fat_dir_entry->name[0] = 0xE5;
                found = 1;

                fat32_writepage(curPage);
                break;                          // 跳出的是对每一个目录项的循环
            }
        }
        if (found)
            break;                              // 跳出的是对每一页的循环
    }

    // 如果没找inode在外存上相应的数据
    if (!found){
        return -ENOENT;
    }
        

    return 0;
}

// 用通过传递参数指定的索引节点对象的内容更新一个文件系统的索引节点
u32 fat32_write_inode(struct inode * inode, struct dentry * parent){
    u8 name[MAX_FAT32_SHORT_FILE_NAME_LEN];
    u32 i;
    u32 j;
    u32 err;
    u32 found;
    u32 begin;
    u32 curPageNo;
    u32 pagesize;
    struct qstr                             qstr;
    struct qstr                             qstr2;
    struct inode                            * dir;
    struct dentry                           * dentry;
    struct vfs_page                         * curPage;
    struct condition                        cond;
    struct address_space                    * mapping;
    struct fat_dir_entry                    * fat_dir_entry;

    found = 0;
    dir         = parent->d_inode;
    mapping     = &(dir->i_data);
    pagesize    = dir->i_blksize;
    
    dentry      = container_of(inode->i_dentry.next, struct dentry, d_alias);

    // 对目录关联的每一页
    for ( i = 0; i < dir->i_blocks; i++){
        curPageNo = mapping->a_op->bmap(dir, i);
        // 首先在页高速缓存中寻找
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = (struct vfs_page *)pcache->c_op->look_up(pcache, &cond);

        
        // 如果页高速缓存中没有，则需要在外存中寻找（一定能够找到，因为不是创建文件）
        if ( curPage == 0 ){
            // kernel_printf("dcache not found!\n");
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
                return -ENOENT;
            }

            curPage->p_state = P_CLEAR;
            pcache->c_op->add(pcache, (void*)curPage);
            list_add(&(curPage->p_list), &(mapping->a_cache));
        }

        //现在p_data指向的数据就是页的数据。假定页里面的都是fat32短文件目录项。对每一个目录项        
        for ( begin = 0; begin < pagesize; begin += FAT32_DIR_ENTRY_LEN ){
            fat_dir_entry = (struct fat_dir_entry *)(curPage->p_data + begin);

            // 先判断是不是短文件名，如果不是的话跳过（08 卷标、0F长文件名）
            if (fat_dir_entry->attr == 0x08 || fat_dir_entry->attr == 0x0F)
                continue;
            
            // 再判断是不是已删除的文件，是的话跳过
            if (fat_dir_entry->name[0] == 0xE5)
                continue;

            // 再判断是不是没有目录项了
            if (fat_dir_entry->name[0] == '\0')
                break;
            
            // 有目录项的话，提取其名字
            kernel_memset( name, 0, MAX_FAT32_SHORT_FILE_NAME_LEN * sizeof(u8) );
            for ( j = 0; j < MAX_FAT32_SHORT_FILE_NAME_LEN; j++ )
                name[j] = fat_dir_entry->name[j];

            qstr.name = name;
            qstr.len = MAX_FAT32_SHORT_FILE_NAME_LEN;

            // 转换名字
            fat32_convert_filename(&qstr2, &qstr, fat_dir_entry->lcase, FAT32_NAME_SPECIFIC_TO_NORMAL);

            // 如果与待找的名字相同，则修改相应的文件元信息
            if ( generic_compare_filename( &qstr2, &(dentry->d_name) ) == 0 ){
                fat_dir_entry->size         = inode->i_size;
                found = 1;
                break;                          // 跳出的是对每一个目录项的循环
            }
        }
        if (found)
            break;                              // 跳出的是对每一页的循环
    }

    // 如果没找到相应的inode
    if (!found)
        return -ENOENT;

    // 写入外存
    err = mapping->a_op->writepage(curPage);
    if(err)
        return err;
    
    return 0;
}

// 下面是为fat32专门实现的 inode_operations
// 尝试在外存中查找需要的dentry对应的inode。若找到，相应的inode会被新建并加入高速缓存，dentry与之的联系也会被建立
struct dentry* fat32_inode_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd){  
    u8 name[MAX_FAT32_SHORT_FILE_NAME_LEN];
    u16 low, high;
    u32 addr;
    u32 i;
    u32 j;
    u32 k;
    u32 err;
    u32 found;
    u32 begin;
    u32 curPageNo;
    u32 pagesize;
    struct qstr                             qstr;
    struct qstr                             qstr2;
    struct vfs_page                         *curPage;
    struct condition                        cond;
    struct inode                            *new_inode;
    struct address_space                    *mapping;
    struct fat_dir_entry                    *fat_dir_entry;

    found = 0;
    new_inode = 0;
    mapping = &(dir->i_data);
    pagesize = dir->i_blksize;

    // 对目录关联的每一页
    for ( i = 0; i < dir->i_blocks; i++){
        curPageNo = mapping->a_op->bmap(dir, i);
        // 首先在页高速缓存中寻找
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = (struct vfs_page *)pcache->c_op->look_up(pcache, &cond);
        
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

        //现在p_data指向的数据就是页的数据。假定页里面的都是fat32短文件目录项。对每一个目录项        

        for ( begin = 0; begin < pagesize; begin += FAT32_DIR_ENTRY_LEN ){
            
            fat_dir_entry = (struct fat_dir_entry *)(curPage->p_data + begin);

            // 先判断是不是短文件名，如果不是的话跳过（08 卷标、0F长文件名）
            if (fat_dir_entry->attr == 0x08 || fat_dir_entry->attr == 0x0F)
                continue;
            
            // 再判断是不是已删除的文件，是的话跳过
            if (fat_dir_entry->name[0] == 0xE5)
                continue;

            // 再判断是不是没有目录项了
            if (fat_dir_entry->name[0] == '\0')
                break;
            
            // 有目录项的话，提取其名字
            kernel_memset( name, 0, MAX_FAT32_SHORT_FILE_NAME_LEN * sizeof(u8) );
            for ( j = 0; j < MAX_FAT32_SHORT_FILE_NAME_LEN; j++ )
                name[j] = fat_dir_entry->name[j];
            // kernel_printf("here ok1\n");
            qstr.name = name;
            qstr.len = MAX_FAT32_SHORT_FILE_NAME_LEN;

            // 转换名字
            fat32_convert_filename(&qstr2, &qstr, fat_dir_entry->lcase, FAT32_NAME_SPECIFIC_TO_NORMAL);

            // 如果与待找的名字相同，则建立相应的inode
            if ( generic_compare_filename( &qstr2, &(dentry->d_name) ) == 0 ){
                // 获得起始簇号（相对物理地址）
                low     = fat_dir_entry->startlo;
                high    = fat_dir_entry->starthi;
                addr    = (high << 16) + low;

                // 填充inode的相应信息
                new_inode = (struct inode*) kmalloc ( sizeof(struct inode) );
                new_inode->i_count          = 1;
                new_inode->i_ino            = addr;           // 本fat32系统设计inode的ino即为起始簇号
                new_inode->i_blkbits        = dir->i_blkbits;
                new_inode->i_blksize        = dir->i_blksize;
                new_inode->i_sb             = dir->i_sb;
                new_inode->i_size           = fat_dir_entry->size;
                new_inode->i_blocks         = 0;
                new_inode->i_fop            = &(fat32_file_operations);
                INIT_LIST_HEAD(&(new_inode->i_dentry));

                if ( fat_dir_entry->attr & ATTR_DIRECTORY )   // 目录和非目录有不同的inode方法
                    new_inode->i_op         = &(fat32_inode_operations[0]);
                else
                    new_inode->i_op         = &(fat32_inode_operations[1]);

                // 填充关联的address_space结构
                new_inode->i_data.a_host        = new_inode;
                new_inode->i_data.a_pagesize    = new_inode->i_blksize;
                new_inode->i_data.a_op          = &(fat32_address_space_operations);
                INIT_LIST_HEAD(&(new_inode->i_data.a_cache));

                
                while ( 0x0FFFFFFF != addr ){
                    new_inode->i_blocks ++;
                    addr = read_fat(new_inode, addr);          // 读FAT32表
                }

                new_inode->i_data.a_page        = (u32*) kmalloc (new_inode->i_blocks * sizeof(u32) );
                kernel_memset(new_inode->i_data.a_page, 0, new_inode->i_blocks);

                addr = new_inode->i_ino;
                for( k = 0; k < new_inode->i_blocks; k++){
                    new_inode->i_data.a_page[k] = addr;
                    addr = (new_inode, addr);
                }
                
                // 把inode放入高速缓存
                // icache->c_op->add(icache, (void*)new_inode);
                found = 1;
                break;                          // 跳出的是对每一个目录项的循环
            }
        }
        if (found)
            break;                              // 跳出的是对每一页的循环
    }

    // 如果没找到相应的inode
    if (!found)
        return 0;

    // 完善dentry的信息
    dentry->d_inode = new_inode;
    dentry->d_op = &fat32_dentry_operations;
    list_add(&dentry->d_alias, &new_inode->i_dentry);  
    
    return dentry;
}


u32 fat32_create(struct inode *dir, struct dentry *dentry, u32 mode, struct nameidata *nd)
{
    return 0;
}

// 下面是为fat32专门实现的file operations
u32 fat32_readdir(struct file * file, struct getdent * getdent){
    u8 name[MAX_FAT32_SHORT_FILE_NAME_LEN];
    u32 i;
    u32 j;
    u32 err;
    u32 addr;
    u32 low;
    u32 high;
    u32 begin;
    u32 pagesize;
    u32 curPageNo;
    struct inode                    *dir;
    struct qstr                     qstr;
    struct qstr                     qstr2;
    struct condition                cond;
    struct fat_dir_entry            *fat_dir_entry;
    struct vfs_page                 *curPage;
    struct address_space            *mapping;

    dir = file->f_dentry->d_inode;
    mapping = &(dir->i_data);
    pagesize = dir->i_blksize;

    // 对目录关联的每一页
    getdent->count = 0;
    getdent->dirent = (struct dirent *) kmalloc ( sizeof(struct dirent) * (dir->i_blocks * pagesize / FAT32_DIR_ENTRY_LEN));
    if (getdent->dirent == 0)
        return -ENOMEM;

    for ( i = 0; i < dir->i_blocks; i++){
        curPageNo = mapping->a_op->bmap(dir, i);
        if (curPageNo == 0)
            return -ENOENT;

        // 首先在页高速缓存中寻找
        cond.cond1 = (void*)(&curPageNo);
        cond.cond2 = (void*)(dir);
        curPage = (struct vfs_page *)pcache->c_op->look_up(pcache, &cond);
        
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

        //现在p_data指向的数据就是页的数据。假定页里面的都是fat32短文件目录项。对每一个目录项        
        for ( begin = 0; begin < pagesize; begin += FAT32_DIR_ENTRY_LEN ){
            fat_dir_entry = (struct fat_dir_entry *)(curPage->p_data + begin);

            // 先判断是不是短文件名，如果不是的话跳过（08 卷标、0F长文件名）
            if (fat_dir_entry->attr == 0x08 || fat_dir_entry->attr == 0x0F)
                continue;
            
            // 再判断是不是已删除的文件，是的话跳过
            if (fat_dir_entry->name[0] == 0xE5)
                continue;

            // 再判断是不是没有目录项了
            if (fat_dir_entry->name[0] == '\0')
                break;
            
            // 还有目录项的话，提取其名字
            kernel_memset( name, 0, MAX_FAT32_SHORT_FILE_NAME_LEN * sizeof(u8) );
            for ( j = 0; j < MAX_FAT32_SHORT_FILE_NAME_LEN; j++ )
                name[j] = fat_dir_entry->name[j];
            
            qstr.name = name;
            qstr.len = MAX_FAT32_SHORT_FILE_NAME_LEN;

            // 转换名字
            fat32_convert_filename(&qstr2, &qstr, fat_dir_entry->lcase, FAT32_NAME_SPECIFIC_TO_NORMAL);

            // 填充相应信息
            low     = fat_dir_entry->startlo;
            high    = fat_dir_entry->starthi;
            addr    = (high << 16) + low;

            getdent->dirent[getdent->count].ino         = addr;
            getdent->dirent[getdent->count].name        = qstr2.name;  // qstr2 的name每一次conver filename都会变的

            if ( fat_dir_entry->attr & ATTR_DIRECTORY )
                getdent->dirent[getdent->count].type    = FT_DIR;
            else
                getdent->dirent[getdent->count].type    = FT_REG_FILE;

            getdent->count += 1;
        }   // 页内循环
    }       // 页际循环

    return 0;
}

// 下面是为fat32专门实现的其他方法
// 文件名双向转换
void fat32_convert_filename(struct qstr* dest, const struct qstr* src, u8 mode, u32 direction){
    u8* name;
    int i;
    u32 j;
    u32 dot;
    int end;
    u32 null;
    int dot_pos;

    dest->name = 0;
    dest->len = 0;

    // 若src放的是一般的文件名
    if ( direction == FAT32_NAME_NORMAL_TO_SPECIFIC ){
        name = (u8 *) kmalloc ( MAX_FAT32_SHORT_FILE_NAME_LEN * sizeof(u8) );

        // 找到作为拓展名的“.”
        dot = 0;
        dot_pos = INF;
        for ( i = 0; i < src->len; i++ )
            if ( src->name[i] == '.' ){
                dot = 1;
                break;
            }
                
        if (dot)
            dot_pos = i;

        // 先转换“.”前面的部分
        if ( dot_pos > MAX_FAT32_SHORT_FILE_NAME_BASE_LEN )
            end = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN - 1;
        else
            end = dot_pos - 1;

        for ( i = 0; i < MAX_FAT32_SHORT_FILE_NAME_BASE_LEN; i++ ){
            if ( i > end )
                name[i] = '\0';
            else {
                if ( src->name[i] <= 'z' && src->name[i] >= 'a' )
                    name[i] = src->name[i] - 'a' + 'A';
                else
                    name[i] = src->name[i];
            }
        }

        // 再转换“.”后面的部分
        for ( i = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN, j = dot_pos + 1; i < MAX_FAT32_SHORT_FILE_NAME_LEN; i++, j++ )
        {
            if ( j >= src->len )
                name[i] == '\0';
            else{
                if ( src->name[j] <= 'z' && src->name[j] >= 'a' )
                    name[i] = src->name[j] - 'a' + 'A';
                else
                    name[i] = src->name[j];
            }
        }
        
        dest->name = name;
        dest->len = MAX_FAT32_SHORT_FILE_NAME_LEN;
    }

    // 若src放的是8-3规则的文件名
    else if ( direction == FAT32_NAME_SPECIFIC_TO_NORMAL ) {
        // 默认src的长度必为 MAX_FAT32_SHORT_FILE_NAME_LEN
        // 首先找出新字符串的长度，同时找出“.”的位置
        null = 0;
        dot_pos = MAX_FAT32_SHORT_FILE_NAME_LEN;
        for ( i = MAX_FAT32_SHORT_FILE_NAME_LEN - 1; i  ; i-- ){
            if ( src->name[i] == 0x20 ) {
                dot_pos = i;
                null ++;
            }

        }

        dest->len = MAX_FAT32_SHORT_FILE_NAME_LEN - null;
        name = (u8 *) kmalloc ( (dest->len + 2) * sizeof(u8) );     // 空字符 + '.'(不一定有)
        
        if ( dot_pos > MAX_FAT32_SHORT_FILE_NAME_BASE_LEN )
            dot_pos = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN;
        
        // 先转换应该是“.”之前的部分
        for ( i = 0; i < dot_pos; i++ ) {
            if (src->name[i] <= 'z' && src->name[i] >= 'a' && (mode == 0x10 || mode == 0x00) )
                name[i] = src->name[i] - 'a' + 'A';
            else if (src->name[i] <= 'Z' && src->name[i] >= 'A' && (mode == 0x18 || mode == 0x08) )
                name[i] = src->name[i] - 'A' + 'a';
            else
                name[i] = src->name[i];
        }
        
        i = dot_pos;
        j = MAX_FAT32_SHORT_FILE_NAME_BASE_LEN;
        if (src->name[j] != 0x20){
            name[i] = '.';
            for ( i = dot_pos + 1; j < MAX_FAT32_SHORT_FILE_NAME_LEN && src->name[j] != 0x20; i++, j++ ){
                if (src->name[j] <= 'z' && src->name[j] >= 'a' && (mode == 0x08 || mode == 0x00) )
                    name[i] = src->name[j] - 'a' + 'A';
                else if (src->name[j] <= 'Z' && src->name[j] >= 'A' && (mode == 0x18 || mode == 0x10))
                    name[i] = src->name[j] - 'A' + 'a';
                else
                    name[i] = src->name[j];
            }
            dest->len += 1;
        }
        
        name[i] = '\0';
        dest->name = name;
    }
    else
        return;
}

// 下面是为fat32专门实现的 address_space_operations
// 从外存读入一页
u32 fat32_readpage(struct vfs_page *page){
    u32 err;
    u32 data_base;
    u32 abs_sect_addr;
    struct inode *inode;

    // 计算绝对扇区地址
    inode = page->p_mapping->a_host;
    data_base = ((struct fat32_basic_information *)(inode->i_sb->s_fs_info))->fa_FAT->data_sec;
    abs_sect_addr = data_base + (page->p_location - 2) * (inode->i_blksize >> SECTOR_SHIFT);

    // 分配数据区,调用底层函数读入数据
    page->p_data = ( u8* ) kmalloc ( sizeof(u8) * inode->i_blksize );
    if (page->p_data == 0)
        return -ENOMEM;

    err = read_block(page->p_data, abs_sect_addr, inode->i_blksize >> SECTOR_SHIFT);
    if (err)
        return -EIO;
    
    return 0;
}

// 把一页写回外存
u32 fat32_writepage(struct vfs_page *page){
    u32 err;
    u32 data_base;
    u32 abs_sect_addr;
    struct inode *inode;

    // 计算绝对扇区地址

    inode = page->p_mapping->a_host;


    data_base = ((struct fat32_basic_information *)(inode->i_sb->s_fs_info))->fa_FAT->data_sec;
    abs_sect_addr = data_base + (page->p_location - 2) * (inode->i_blksize >> SECTOR_SHIFT);

    // 调用底层函数写回外存
    err = write_block(page->p_data, abs_sect_addr, inode->i_blksize >> SECTOR_SHIFT);

    if (err)
        return -EIO;
    
    return 0;
}

// 根据由相对文件页号得到相对物理页号
u32 fat32_bmap(struct inode* inode, u32 pageNo){
    // 假设文件内页号是安全的
    return inode->i_data.a_page[pageNo];
}

// 读文件分配表
u32 read_fat(struct inode* inode, u32 index) {
    u8 buffer[SECTOR_SIZE];
    u32 shift;
    u32 base_sect;
    u32 dest_sect;
    u32 dest_index;
    struct fat32_basic_information * FAT32_BI;

    // 首先获得FAT表的基地址
    FAT32_BI = (struct fat32_basic_information *)(inode->i_sb->s_fs_info);
    base_sect = FAT32_BI->fa_FAT->base;

    // 计算扇区及偏移量
    shift = SECTOR_SHIFT - FAT32_FAT_ENTRY_LEN_SHIFT;
    dest_sect = base_sect + ( index >> shift );
    dest_index = index & (( 1 << shift ) - 1 );
    // 读扇区并取相应的项
    read_block(buffer, dest_sect, 1);
    return get_u32(buffer + (dest_index << FAT32_FAT_ENTRY_LEN_SHIFT));
}
