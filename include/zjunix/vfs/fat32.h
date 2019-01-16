#ifndef _ZJUNIX_VFS_FAT32_H
#define _ZJUNIX_VFS_FAT32_H

#include <zjunix/vfs/vfs.h>

#define MAX_FAT32_SHORT_FILE_NAME_BASE_LEN      8
#define MAX_FAT32_SHORT_FILE_NAME_EXT_LEN       3
#define MAX_FAT32_SHORT_FILE_NAME_LEN           ( MAX_FAT32_SHORT_FILE_NAME_BASE_LEN  + MAX_FAT32_SHORT_FILE_NAME_EXT_LEN )
#define FAT32_FAT_ENTRY_LEN_SHIFT               2
#define FAT32_DIR_ENTRY_LEN                     32

#define ATTR_DIRECTORY   0x10

#define FAT32_NAME_NORMAL_TO_SPECIFIC           0
#define FAT32_NAME_SPECIFIC_TO_NORMAL           1
#define INF                                     10000

// FAT32 文件系统信息汇总
struct fat32_basic_information {
    struct fat32_dos_boot_record* fa_DBR;               // DBR 扇区信息
    struct fat32_file_system_information* fa_FSINFO;    // FSINFO 辅助信息
    struct fat32_file_allocation_table* fa_FAT;         // FAT 文件分配表
};

// FAT32 DBR扇区信息
struct fat32_dos_boot_record {
    u32 base;                                           // 基地址（绝对扇区地址）
    u32 reserved;                                       // 保留扇区（文件分配表之前）
    u32 fat_num;                                        // 文件分配表的个数
    u32 fat_size;                                       // 一张文件分配表所占的扇区数
    u32 root_clu;                                       // 根目录起始所在簇号（算上0号和1号簇），
    u32 sec_per_clu;                                    // 每一簇的扇区数
    u8 data[SECTOR_SIZE];                               // 数据
};

// FSINFO 文件系统信息
struct fat32_file_system_information {
    u32 base;                                           // 基地址（绝对扇区地址）
    u8 data[SECTOR_SIZE];                               // 数据
};

// FAT 文件分配表汇总
struct fat32_file_allocation_table {
    u32 base;                                           // 基地址（绝对扇区地址）
    u32 data_sec;                                       // （FAT表无关）数据区起始位置的绝对扇区(方便)
    u32 root_sec;                                       // （FAT表无关）根目录内容所在绝对扇区（方便）
};

// 文件分配表（忽略长文件名文件）
struct __attribute__((__packed__)) fat_dir_entry {
    u8 name[MAX_FAT32_SHORT_FILE_NAME_LEN];             // 文件名(含拓展名)
    u8 attr;                                            // 属性
    u8 lcase;                                           // 系统保留
    u8 ctime_cs;                                        // 创建时间的10毫秒位
    u16 ctime;                                          // 创建时间
    u16 cdate;                                          // 创建日期
    u16 adate;                                          // 最后访问日期
    u16 starthi;                                        // 文件起始簇（相对物理帧）的高16位
    u16 time;                                           // 文件最后修改时间
    u16 date;                                           // 文件最后修改日期
    u16 startlo;                                        // 文件起始簇（相对物理帧）的低16位
    u32 size;                                           // 文件长度（字节）
};

// 下面是函数声明
// fat32.c
u32 init_fat32(u32);
u32 fat32_delete_inode(struct dentry *);
u32 fat32_write_inode(struct inode *, struct dentry *);
struct dentry* fat32_inode_lookup(struct inode *, struct dentry *, struct nameidata *);
u32 fat32_create(struct inode *, struct dentry *, u32 mode, struct nameidata *);
u32 fat32_readdir(struct file *, struct getdent *);
void fat32_convert_filename(struct qstr*, const struct qstr*, u8, u32);
u32 fat32_readpage(struct vfs_page *);
u32 fat32_writepage(struct vfs_page *);
u32 fat32_bmap(struct inode *, u32);
u32 read_fat(struct inode *, u32);

#endif