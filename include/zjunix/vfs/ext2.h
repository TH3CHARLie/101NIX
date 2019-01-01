#ifndef _ZJUNIX_VFS_EXT_2_H
#define _ZJUNIX_VFS_EXT_2_H

#include <zjunix/vfs/vfs.h>

#define                 EXT2_BOOT_BLOCK_SECT                2
#define                 EXT2_SUPER_BLOCK_SECT               2
#define                 EXT2_NAME_LEN                       255
#define                 EXT2_GROUP_DESC_BYTE                32
#define                 EXT2_ROOT_INO                       2
#define                 EXT2_N_BLOCKS                       15
#define                 EXT2_BASE_BLOCK_SIZE                1024
#define                 EXT2_FIRST_MAP_INDEX                12
#define                 EXT2_SECOND_MAP_INDEX               13
#define                 EXT2_THIRD_MAP_INDEX                14
#define                 EXT2_BLOCK_ADDR_SHIFT               2
#define                 MAX_DIRENT_NUM                      128

// 文件类型
enum {
         EXT2_FT_UNKNOWN,     
         EXT2_FT_REG_FILE,
         EXT2_FT_DIR,   
};

// EXT2 文件系统信息汇总
struct ext2_base_information {
    u32                 ex_base;                            // 启动块的基地址（绝对扇区地址，下同）
    u32                 ex_first_sb_sect;                   // 第一个super_block的基地址
    u32                 ex_first_gdt_sect;                  // 第一个组描述符表的基地址
    union {
        u8                  *data;
        struct ext2_super   *attr;
    } sb;                                                   // 超级块数据    
};
        
// EXT2 文件系统内部超级块
struct ext2_super {
    u32                 inode_num;                          // inode数
    u32                 block_num;                          // 块数
    u32                 res_block_num;                      // 保留块数
    u32                 free_block_num;                     // 空闲块数
    u32                 free_inode_num;                     // 空闲inode数
    u32                 first_data_block_no;                // 第一个数据块号
    u32                 block_size;                         // 块长度（从1K开始的移位数）
    u32                 slice_size;                         // 片长度（从1K开始的移位数）
    u32                 blocks_per_group;                   // 每组块数
    u32                 slices_per_group;                   // 每组片数
    u32                 inodes_per_group;                   // 每组indoes数
    u32                 install_time;                       // 安装时间
    u32                 last_write_in;                      // 最后写入时间
    u16                 install_count;                      // 安装计数
    u16                 max_install_count;                  // 最大安装数
    u16                 magic;                              // 魔数
    u16                 state;                              // 状态
    u16                 err_action;                         // 出错动作
    u16                 edition_change_mark;                // 改版标志
    u32                 last_check;                         // 最后检测时间
    u32                 max_check_interval;                 // 最大检测间隔
    u32                 operating_system;                   // 操作系统
    u32                 edition_mark;                       // 版本标志
    u16                 uid;                                // uid
    u16                 gid;                                // pid
    u32                 first_inode;                        // 第一个非保留的inode
    u16                 inode_size;                         // inode的大小
};

// EXT2 文件系统目录项
struct ext2_dir_entry {
	u32	                ino;                                // 文件的inode号
	u16                 rec_len;                            // 目录项长度（字节）
    u8	                name_len;                           // 名字长度（字节）
    u8                  file_type;                          // 文件类型
	char	            name[EXT2_NAME_LEN];                // 名字
};

// EXT2 组描述符
struct ext2_group_desc {
	u32	                block_bitmap;                       // 块位图所在块
	u32	                inode_bitmap;                       // inode位图所在块
	u32	                inode_table;                        // inode列表所在块
	u16	                free_blocks_count;                  // 空闲块数
	u16	                free_inodes_count;                  // 空闲节点数
	u16	                used_dirs_count;                    // 目录数
	u16	                pad;                                // 以下均为保留
	u32	                reserved[3];
};

// EXT2 内部inode
struct ext2_inode {
	u16	                i_mode;                             // 文件模式
	u16	                i_uid;                              // UID的低16位
	u32	                i_size;                             // 文件大小（字节数）
	u32	                i_atime;                            // 最近访问时间
	u32	                i_ctime;                            // 创建时间
	u32	                i_mtime;                            // 修改时间
	u32	                i_dtime;                            // 删除时间
	u16	                i_gid;                              // GID的低16位
	u16	                i_links_count;                      // 链接计数
	u32	                i_blocks;                           // 关联的块数
	u32	                i_flags;                            // 打开的标记
	u32                 osd1;                               // 与操作系统相关1
	u32	                i_block[EXT2_N_BLOCKS];             // 存放所有相关的块地址
	u32	                i_generation;                       // （NFS用）文件的版本
	u32	                i_file_acl;                         // 文件的ACL
	u32	                i_dir_acl;                          // 目录的ACL
    u32	                i_faddr;                            // 碎片地址
    u32                 osd2[3];                            // 与操作系统相关2
};


// 下面是函数原型
u32 init_ext2(u32);
u32 ext2_delete_inode(struct dentry *);
u32 ext2_write_inode(struct inode *, struct dentry *);
struct dentry * ext2_inode_lookup(struct inode *, struct dentry *, struct nameidata *);
u32 ext2_create(struct inode *, struct dentry *, u32, struct nameidata *);
u32 ext2_readdir(struct file *, struct getdent *);
u32 ext2_readpage(struct vfs_page *);
u32 ext2_writepage(struct vfs_page *);
u32 ext2_bmap(struct inode *, u32);
u32 ext2_fill_inode(struct inode *);
u32 ext2_check_inode_bitmap(struct inode *);
u32 ext2_group_base_sect(struct inode *);


#endif