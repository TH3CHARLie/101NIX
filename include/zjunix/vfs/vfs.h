#ifndef _ZJUNIX_VFS_VFS_H
#define _ZJUNIX_VFS_VFS_H

#include <zjunix/type.h>
#include <zjunix/list.h>
#include <zjunix/vfs/err.h>
#include <driver/vga.h>

#define DEBUG_EXT2
#define DEBUG_VFS
//#define DEBUG_FAT32
//#define DEBUG_SD

static char *quad1 = "    ";
static char *quad2 = "        ";
static char *quad3 = "            ";
static char *quad4 = "                ";
static char *quad5 = "                    ";

#define DPT_MAX_ENTRY_COUNT                     4
#define DPT_ENTRY_LEN                           16
#define SECTOR_SIZE                             512
#define SECTOR_SHIFT                            9
#define BITS_PER_BYTE                           8

#define S_CLEAR                                 0
#define S_DIRTY                                 1

#define MAX_FILE_NAME_LEN                       255

#define D_PINNED                                1
#define D_UNPINNED                              0

// 文件系统类型
#define PARTITION_TYPE_FAT32					0x0b
#define PARTITION_TYPE_EXT2						0x83
#define FAT32_SUPER_MAGIC						0x4d44
#define EXT2_SUPER_MAGIC						0xEF53

// 文件打开方式，即open函数的入参flags。打开文件时用
#define O_RDONLY	                            0x0000                   // 为读而打开
#define O_WRONLY	                            0x0001                   // 为写而打开
#define O_RDWR		                            0x0002                   // 为读和写打开
#define O_ACCMODE	                            0x0003
#define O_CREAT		                            0x0100                   // 如果文件不存在，就创建它
#define O_APPEND	                            0x2000                   // 总是在文件末尾写
#define ACC_MODE(x) ("/000/004/002/006"[(x)&O_ACCMODE])

#define MAY_APPEND	                            0x0008

#define LOOKUP_FOLLOW                           0x0001                  // 如果最后一个分量是符号链接，则追踪（解释）它
#define LOOKUP_DIRECTORY                        0x0002                  // 最后一个分量必须是目录
#define LOOKUP_CONTINUE                         0x0004                  // 在路径名中还有文件名要检查
#define LOOKUP_PARENT                           0x0010                  // 查找最后一个分量所在的目录
#define LOOKUP_CREATE                           0x0200                  // 试图创建一个文件

// LOOKUP_PARENT 中最后分量的类型
enum {LAST_NORM, LAST_ROOT, LAST_DOT, LAST_DOTDOT, LAST_BIND};

// 文件打开方式。打开文件时后用
#define FMODE_READ		                        0x1                     // 文件为读而打开
#define FMODE_WRITE		                        0x2                     // 文件为写而打开
#define FMODE_LSEEK		                        0x4                     // 文件可以寻址
#define FMODE_PREAD		                        0x8                     // 文件可用pread
#define FMODE_PWRITE	                        0x10                    // 文件可用pwrite

#define PAGE_SIZE                               (1 << PAGE_SHIFT)
#define PAGE_SHIFT                              12
#define PAGE_CACHE_SIZE                         PAGE_SIZE
#define PAGE_CACHE_SHIFT                        PAGE_SHIFT
#define PAGE_CACHE_MASK                         (~((1 << PAGE_SHIFT) - 1))

// 挂载选项
#define MS_REMOUNT 								0x0001					// 修改挂载点信息
#define MS_MOVE									0x0002					// 移动挂载点

// dentry用检查
#define IS_ROOT(x) ((x) == (x)->d_parent)

// 文件类型
enum {
         FT_UNKNOWN,     
         FT_REG_FILE,    
         FT_DIR    
};

// VFS文件类型
#define S_IFMT  00170000
#define S_IFSOCK 0140000
#define S_IFLNK	 0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)


// 由于C的编译需要
struct vfs_page;
struct super_block;

// 主引导记录
struct master_boot_record {
    u32                                 m_count;                        // 分区数
    u32                                 m_base[DPT_MAX_ENTRY_COUNT];    // 每个分区的基地址
    u8                                  m_data[SECTOR_SIZE];            // 数据
	u8 									m_type[DPT_MAX_ENTRY_COUNT];	// 分区类型
};

// 记录文件系统类型信息
struct file_system_type {
    u8                                  *name;                  // 名称
    struct file_system_type             *next;                  // 下一个文件系统
    struct list_head                    fs_supers;              // 该文件系统的超级块链表
};

// 超级块，一个文件系统对应一个超级块
struct super_block {
    u8                                  s_dirt;                 // 修改标志
	u8									*s_name;				// 超级块对应的设备名
    u32                                 s_blksize;              // 以字节为单位的块大小
    struct file_system_type             *s_type;                // 文件系统类型
    struct dentry                       *s_root;                // 文件系统根目录的目录项对象
    struct super_operations             *s_op;                  // 超级块的操作函数
	struct list_head					s_instances;			// 存于该文件系统类型的fs_supers中
    void                                *s_fs_info;             // 指向特定文件系统的超级块信息的指针
};

// TODO 需不需要加mnt_count?
// 挂载信息，每一个安装对应一个挂载信息
struct vfsmount {
	struct list_head                    mnt_hash;               // 用于散列表的指针
    struct list_head                    mnt_child;              // 挂在父亲的mnt_mounts上，可以用来遍历兄弟
    struct list_head                    mnt_mounts;             // 子挂载双向链表的头
	struct vfsmount                     *mnt_parent;	        // 指向父文件系统，这个文件安装在其上
	struct dentry                       *mnt_mountpoint;        // 指向这个文件系统安装点目录的dentry
	struct dentry                       *mnt_root;              // 指向这个文件系统根目录的dentry
	struct super_block                  *mnt_sb;                // 指向这个文件系统的超级块对象
};

// 存放inode和内存中关联的page缓存
struct address_space {
    u32                                 a_pagesize;             // 页大小(字节)
    u32                                 *a_page;                // 文件页到逻辑页的映射表
    struct inode                        *a_host;                // 相关联的inode
    struct list_head                    a_cache;                // 已缓冲的页链表
    struct address_space_operations     *a_op;                  // 操作函数
};

// 文件节点
struct inode {
    u32                                 i_ino;                  // 索引节点号
    u32                                 i_count;                // 当前的引用计数
    u32                                 i_blocks;               // inode使用的块数
    u32                                 i_blkbits;              // 块的位数，用于移位
    u32                                 i_blksize;              // 块的字节数
    u32                                 i_size;                 // 对应文件的字节数
	u32									i_mode;					// 文件类型与文件权限（暂时未用到）
    struct list_head                    i_hash;                 // 用于散列链表的指针
    struct list_head                    i_LRU;                  // 用于LRU链表的指针
    struct list_head                    i_dentry;               // 引用索引节点的目录项链表的头
    struct inode_operations             *i_op;                  // 索引节点的操作函数
    struct file_operations              *i_fop;                 // 对应的文件操作函数
    struct super_block                  *i_sb;                  // 指向超级块对象的指针
    struct address_space                i_data;                 // 文件的地址空间对象
};

// 字符串包装
struct qstr {
    const u8                            *name;                  // 字符串
    u32                                 len;                    // 长度
    u32                                 hash;                   // 哈希值
};

// 目录项
struct dentry {
    u32                                 d_count;                // 当前的引用计数
    u32                                 d_pinned;               // （额外）是否被锁定（一般为根目录）
    u32                                 d_mounted;              // 对目录而言，记录安装该目录项的文件系统数的计数器
    struct inode                        *d_inode;               // 与文件名相关的索引节点
    struct list_head                    d_hash;                 // 指向散列表表项的指针
    struct dentry                       *d_parent;              // 父目录的目录项对象
    struct qstr                         d_name;                 // 文件名
    struct list_head                    d_LRU;                  // 缓存中的节点，用于未使用目录项链表的指针
    struct list_head                    d_child;                // 对目录而言，用于同一父目中目录项链表的指针
    struct list_head                    d_subdirs;              // 对目录而言，子目录项链表的头
    struct list_head                    d_alias;                // 用于与同一索引节点相关的目录项链表的指针
    struct dentry_operations            *d_op;                  // 目录项方法
    struct super_block                  *d_sb;                  // 文件的超级块对象
};

// 打开的文件
struct file {
    u32                                 f_pos;                  // 文件当前的读写位置
	struct list_head	                f_list;                 // 用于通用文件对象链表的指针
	struct dentry		                *f_dentry;              // 与文件相关的目录项对象
	struct vfsmount                     *f_vfsmnt;              // 含有该文件的已安装文件系统
	struct file_operations	            *f_op;                  // 指向文件操作表的指针
	u32 		                        f_flags;                // 当打开文件时所指定的标志
	u32			                        f_mode;                 // 进程的访问方式
	struct address_space	            *f_mapping;             // 指向文件地址空间对象的指针
};

// 打开用
struct open_intent {
    u32	                                flags;
	u32	                                create_mode;
};

// 寻找目录用结构一
struct nameidata {  
    struct dentry                       *dentry;                // 对应目录项
    struct vfsmount                     *mnt;                   // 对应文件系统挂载项
    struct qstr                         last;                   // 最后一个分量的名字
    u32                                 flags;                  // 打开标记
    u32                                 last_type;              // 最后一个分量的文件类型
    union {                                                     // 打开用
		struct open_intent open;
	} intent;
};

// 寻找目录用结构二
struct path {
  	struct vfsmount                     *mnt;                   // 对应目录项
  	struct dentry                       *dentry;                // 对应文件系统挂载项
};

// 通用目录项信息
struct dirent {
    u32                                 ino;                    // inode 号
    u8                                  type;                   // 文件类型
    const u8                            *name;                  // 文件名
};

// 目录项获取结构体
struct getdent {
    u32                                 count;                  // 目录项数
    struct dirent                       *dirent;                // 目录项数组
};

// 超级块的操作函数
struct super_operations {
    // 删除目录项对应的内存中的VFS索引节点和磁盘上文件数据及元数据
    u32 (*delete_inode) (struct dentry *);
    // 用通过传递参数指定的索引节点对象的内容更新一个文件系统的索引节点
    u32 (*write_inode) (struct inode *, struct dentry *);
};

// 打开的文件的操作函数
struct file_operations {
    // 从文件的*offset处开始读出count个字节，然后增加*offset的值（一般与文件指针对应）
    u32 (*read) (struct file *, u8 *, u32, u32 *);
    // 从文件的*offset处开始写入count个字节，然后增加*offset的值（一般与文件指针对应）
    u32 (*write) (struct file *, u8 *, u32, u32 *);
    // 通过创建一个新的文件对象而打开一个文件，并把它链接到相应的索引节点对象
    u32 (*flush) (struct file *);
    // 读取一个目录的目录项并放入第二个参数中
    u32 (*readdir) (struct file *, struct getdent *);
};

// 文件节点的操作函数
struct inode_operations {
    // 在某一目录下，为与目录项相关的普通文件创建一个新的磁盘索引节点
    u32 (*create) (struct inode *,struct dentry *, u32);
    // 为包含在一个目录项对象中的文件名对应的索引节点查找目录
    struct dentry * (*lookup) (struct inode *,struct dentry *, struct nameidata *);
	// 在某一目录下，创建一个新的目录
	u32 (*mkdir) (struct inode *,struct dentry *, u32);
	// 在某一目录下，递归删除一个目录
	u32 (*rmdir) (struct inode *,struct dentry *);
	// 在某一目录下，创建一个设备文件
	u32 (*mknod) (struct inode *,struct dentry *, int, int);
	// 重命名
	u32 (*rename) (struct inode *, struct dentry *,
				   struct inode *, struct dentry *);
};

// 已缓存的页的操作函数
struct address_space_operations {
    // 把一页写回外存
    u32 (*writepage)(struct vfs_page *);
    // 从外存读入一页
    u32 (*readpage)(struct vfs_page *);
    // 根据由相对文件页号得到相对物理页号
    u32 (*bmap)(struct inode *, u32);
};

// 目录项的操作函数
struct dentry_operations {
    // 比较两个文件名。name1应该属于dir所指的目录。缺省的VFS函数是常用的。不过，每个文件系统可用自己的实现。如MS-DOS文件系统不区分大小写
    u32 (*compare)(const struct qstr *, const struct qstr *);
};

// 接下来是函数声明（缩进表明层次结构）
// vfs.c
u32 init_vfs();
u32 vfs_read_MBR();
u32 init_file_systems();

// open.c
struct file * vfs_open(const u8 *, u32, u32);
u32 open_namei(const u8 *, u32, u32, struct nameidata *);
u32 path_lookup(const u8 *, u32 , struct nameidata *);
u32 link_path_walk(const u8 *, struct nameidata *);
void follow_dotdot(struct vfsmount **, struct dentry **);
u32 do_lookup(struct nameidata *, struct qstr *, struct path *);
struct dentry * real_lookup(struct dentry *, struct qstr *, struct nameidata *);
struct dentry * __lookup_hash(struct qstr *, struct dentry *, struct nameidata *);
struct dentry * d_alloc(struct dentry *, const struct qstr *);
struct file * dentry_open(struct dentry *, struct vfsmount *, u32);
u32 vfs_close(struct file *);

// read_write.c
u32 vfs_read(struct file *file, char *buf, u32 count, u32 *pos);
u32 vfs_write(struct file *file, char *buf, u32 count, u32 *pos);
u32 generic_file_read(struct file *, u8 *, u32, u32 *);
u32 generic_file_write(struct file *, u8 *, u32, u32 *);
u32 generic_file_flush(struct file *);

// mount.c
u32 mount_ext2();
u32 follow_mount(struct vfsmount **, struct dentry **);

// vfsmount.c
struct vfsmount * alloc_vfsmnt(const u8 *name);
struct vfsmount * lookup_mnt(struct vfsmount *, struct dentry *);

// utils.c
u32 read_block(u8 *, u32, u32);
u32 write_block(u8 *, u32, u32);
u16 get_u16(u8 *);
u32 get_u32(u8 *);
void set_u16(u8 *, u16);
void set_u32(u8 *, u32);
u32 generic_compare_filename(const struct qstr *, const struct qstr *);
u32 fat32_compare_filename(const struct qstr *a, const struct qstr *b);
u32 get_bit(const u8 *, u32);
void set_bit(u8 *, u32);
void reset_bit(u8 *, u32);
u32 find_first_zero_bit(u8 *bitmap, u32 blksize);
u32 log2(u32 word);

u32 __intHash(u32, u32);
u32 __stringHash(struct qstr *, u32);

// usr.c
u32 vfs_cat(const u8 *);
u32 vfs_cd(const u8 *);
u32 vfs_ls(const u8 *);
u32 vfs_rm(const u8 *);
u32 vfs_mount(char *a, char *b, char *c, int mode);
u32 vfs_umount(const u8 *dir_name);
u32 sys_mkdir(const u8* path, u32 mode);
u32 sys_create(const u8* path, u32 mode);

// filesystems.c
struct file_system_type * register_filesystem(struct file_system_type * fs);
u32 unregister_filesystem(struct file_system_type *);
struct file_system_type *get_fs_type(const u8 *name);
struct super_block * get_sb(struct file_system_type * fs, const u8 * name);
void print_file_systems();

// namespace.c
struct vfsmount *mntget(struct vfsmount *mnt);
u32 do_mount(const u8 *dev_name, const u8 *dir_name,
			 const u8 *type_page, u32 flags);
	u32 do_new_mount(struct nameidata *nd, const u8 *type_page,
					 const u8 *dev_name);
		struct vfsmount * do_kern_mount(const u8 *fstype, const u8 *dev_name);
		u32 do_add_mount(struct vfsmount *newmnt, struct nameidata *nd);
			static u32 graft_tree(struct vfsmount *mnt, struct nameidata *nd);
	u32 do_move_mount(struct nameidata *nd, const u8 *dev_name);
u32 do_umount(const u8 *dir_name);
void path_release(struct nameidata *nd);

// init.c
struct dentry * alloc_dentry();
struct inode * alloc_inode(struct super_block * sb);
struct vfs_page * alloc_vfspage(u32 location, struct address_space *mapping);

#endif