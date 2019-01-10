#ifndef _ZJUNIX_VFS_EXT_2_H
#define _ZJUNIX_VFS_EXT_2_H

#include <zjunix/vfs/vfs.h>

#define                 EXT2_BOOT_BLOCK_SECT                2			// BOOT块占据扇区数
#define                 EXT2_SUPER_BLOCK_SECT               2           // 超级块所占扇区数量
#define                 EXT2_NAME_LEN                       255			// 名字长度上限
#define                 EXT2_GROUP_DESC_BYTE                32			// 组描述符字节大小
#define                 EXT2_ROOT_INO                       2			// 根目录inode号
#define                 EXT2_N_BLOCKS                       15			//
#define                 EXT2_BASE_BLOCK_SIZE                1024		// 块基础大小，左移超级块中的s_log_block_size，得到真实大小
#define                 EXT2_FIRST_MAP_INDEX                12			// 一次间接块
#define                 EXT2_SECOND_MAP_INDEX               13			// 二次间接块
#define                 EXT2_THIRD_MAP_INDEX                14			// 三次间接块
#define                 EXT2_BLOCK_ADDR_SHIFT               2			// 4字节一个地址
#define                 MAX_DIRENT_NUM                      128			// 最大目录数量
#define 				EXT2_GROUPS_PER_SECT				16			// 每扇区快组数量：512/32=16
#define                 EXT2_FIRST_INO                      11          // 普通ino从11开始使用

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
	u32					ex_blksize;							// 块大小，根据sb的信息读到
	u32      			s_groups_count;						// 块组个数(组描述符数量)
	u32					s_desc_per_block;					// 每块中组描述符数量
	union {
		u8                  		*data;
		struct ext2_super_block   	*attr;
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

typedef unsigned int 	__le32;
typedef unsigned short 	__le16;
typedef unsigned char 	__u8;
typedef unsigned short	__u16;
typedef unsigned int 	__u32;

struct ext2_super_block {
	__le32      	s_inodes_count;		        /* Inodes count */
	__le32      	s_blocks_count;		        /* Blocks count */
	__le32      	s_r_blocks_count;	        /* Reserved blocks count */
	__le32      	s_free_blocks_count;	    /* Free blocks count */
	__le32      	s_free_inodes_count;	    /* Free inodes count */
	__le32      	s_first_data_block;	        /* First Data Block */
	__le32      	s_log_block_size;	        /* Block size */
	__le32      	s_log_frag_size;	        /* Fragment size */
	__le32      	s_blocks_per_group;	        /* # Blocks per group */
	__le32      	s_frags_per_group;	        /* # Fragments per group */
	__le32      	s_inodes_per_group;	        /* # Inodes per group */
	__le32      	s_mtime;		            /* Mount time */
	__le32      	s_wtime;		            /* Write time */
	__le16      	s_mnt_count;		        /* Mount count */
	__le16      	s_max_mnt_count;	        /* Maximal mount count */
	__le16      	s_magic;		            /* Magic signature */
	__le16      	s_state;		            /* File system state */
	__le16      	s_errors;		            /* Behaviour when detecting errors */
	__le16      	s_minor_rev_level; 	        /* minor revision level */
	__le32      	s_lastcheck;		        /* time of last check */
	__le32      	s_checkinterval;	        /* max. time between checks */
	__le32      	s_creator_os;		        /* OS */
	__le32      	s_rev_level;		        /* Revision level */
	__le16      	s_def_resuid;		        /* Default uid for reserved blocks */
	__le16      	s_def_resgid;		        /* Default gid for reserved blocks */
	__le32          s_first_ino; 		        /* First non-reserved inode */
	__le16          s_inode_size; 		        /* size of inode structure */
	__le16          s_block_group_nr; 	        /* block group # of this superblock */
	__le32          s_feature_compat; 	        /* compatible feature set */
	__le32          s_feature_incompat; 	    /* incompatible feature set */
	__le32          s_feature_ro_compat; 	    /* readonly-compatible feature set */
	__u8            s_uuid[16];		            /* 128-bit uuid for volume */
	char            s_volume_name[16]; 	        /* volume name */
	char            s_last_mounted[64]; 	    /* directory where last mounted */
	__le32          s_algorithm_usage_bitmap;   /* For compression */

	__u8	        s_prealloc_blocks;	        /* Nr of blocks to try to preallocate*/
	__u8	        s_prealloc_dir_blocks;	    /* Nr to preallocate for dirs */
	__u16	        s_padding1;

	__u8	        s_journal_uuid[16];	        /* uuid of journal superblock */
	__u32	        s_journal_inum;		        /* inode number of journal file */
	__u32	        s_journal_dev;		        /* device number of journal file */
	__u32	        s_last_orphan;		        /* start of list of inodes to delete */
	__u32	        s_hash_seed[4];		        /* HTREE hash seed */
	__u8	        s_def_hash_version;	        /* Default hash version to use */
	__u8	        s_reserved_char_pad;
	__u16	        s_reserved_word_pad;
	__le32	        s_default_mount_opts;
	__le32	        s_first_meta_bg; 	        /* First metablock block group */
	__u32	        s_reserved[190];	        /* Padding to the end of the block */
};

#define EXT2_DIR_PAD		 			4
#define EXT2_DIR_ROUND 					(EXT2_DIR_PAD - 1)
#define EXT2_DIR_REC_LEN(name_len)		(((name_len) + 8 + EXT2_DIR_ROUND) & ~EXT2_DIR_ROUND)
#define EXT2_DIR_NAME_LEN(name_len)		(((name_len) + EXT2_DIR_ROUND) & ~EXT2_DIR_ROUND)

// EXT2 文件系统目录项
struct ext2_dir_entry_2 {
	u32	                ino;                                // 文件的inode号
	u16                 rec_len;                            // 目录项长度（字节）
	u8	                name_len;                           // 名字长度（字节）
	u8                  file_type;                          // 文件类型
	char	            name[EXT2_NAME_LEN];                // 名字
};

// EXT2 组描述符
struct ext2_group_desc {
	u32	                bg_block_bitmap;                    // 块位图所在块
	u32	                bg_inode_bitmap;                    // inode位图所在块
	u32	                bg_inode_table;                     // inode列表所在块
	u16	                bg_free_blocks_count;               // 空闲块数
	u16	                bg_free_inodes_count;               // 空闲节点数
	u16	                bg_used_dirs_count;                 // 目录数
	u16	                bg_pad;                             // 以下均为保留
	u32	                bg_reserved[3];
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
u32 init_ext2(u32 base, u8 * DEV_NAME);
u32 ext2_delete_inode(struct dentry *);
u32 ext2_write_inode(struct inode *, struct dentry *);
struct dentry * ext2_inode_lookup(struct inode *, struct dentry *, struct nameidata *);
u32 ext2_create(struct inode *, struct dentry *, u32, struct nameidata *);
u32 ext2_readdir(struct file *, struct getdent *);

u32 ext2_check_inode_bitmap(struct inode *);
u32 ext2_set_inode_bitmap(struct inode *inode);
u32 ext2_group_base_sect(struct inode *);

struct ext2_group_desc * ext2_get_group_desc(struct ext2_base_information * ext2_BI,
											 u32 ino, int flag);
void ext2_write_group_desc(struct ext2_group_desc * gdt, struct ext2_base_information * ext2_BI,
						   u32 ino, int flag);

struct ext2_base_information *EXT2_SB(struct super_block *sb);
void ext2_sync_super(struct super_block *sb);

// inode.c
u32 ext2_fill_inode(struct inode *);
void ext2_reset_inode(struct ext2_base_information *sbi, u32 base, struct inode *inode);
u32 find_group_other(struct ext2_base_information *sb, struct inode *parent);
struct inode * ext2_new_inode(struct inode *dir, u32 mode);

// ext2_page.c
u32 ext2_readpage(struct vfs_page *);
u32 ext2_writepage(struct vfs_page *);
u32 ext2_bmap(struct inode *, u32);

// dir.c
u32 ext2_mkdir(struct inode * dir, struct dentry * dentry, u32 mode);

// super.c
void ext2_write_super(struct ext2_base_information *sbi);
void ext2_sync_super(struct super_block *sb);

u32 ext2_new_block(struct inode *inode);

#endif