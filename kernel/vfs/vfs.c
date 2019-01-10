#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>
#include <zjunix/vfs/fat32.h>
#include <zjunix/vfs/ext2.h>

#include <zjunix/log.h>
#include <zjunix/slab.h>
#include <zjunix/utils.h>
#include <driver/vga.h>

// TODO 根文件系统装载重构
// 公用全局变量
struct master_boot_record   * MBR;
struct dentry               * root_dentry;
struct dentry               * pwd_dentry;
struct vfsmount             * root_mnt;
struct vfsmount             * pwd_mnt;

//struct vfs_page* tempp;

struct file_system_type     * file_systems;

// 虚拟文件系统初始化
// TODO init_mount_tree
u32 init_vfs(){
    u32 err;
    u8  i;

    err = init_file_systems();                  // 初始化文件系统
    if ( IS_ERR_VALUE(err) ){
        log(LOG_FAIL, "init_file_systems()");
        goto vfs_init_err;
    }
    log(LOG_OK, "init_file_systems()");

    err = vfs_read_MBR();                       // 读取主引导记录
    if ( IS_ERR_VALUE(err) ){
        log(LOG_FAIL, "vfs_read_MBR()");
        goto vfs_init_err;
    }
    log(LOG_OK, "vfs_read_MBR()");

    err = init_cache();                         // 初始化公用缓存区域
    if ( IS_ERR_VALUE(err) ){
        log(LOG_FAIL, "init_cache()");
        goto vfs_init_err;
    }
    log(LOG_OK, "init_cache()");

    err = init_fat32(MBR->m_base[0]);           // 第一个分区为FAT32，读取元数据
    if ( IS_ERR_VALUE(err) ){
        log(LOG_FAIL, "init_fat32()");
        goto vfs_init_err;
    }
    log(LOG_OK, "init_fat32()");

    u8 DEV_NAME[10] = "/dev/sda0";
    for (i = 1; i < MBR->m_count; i++) {
        DEV_NAME[8] = '0' + i + 1;
        if (MBR->m_type[i] == PARTITION_TYPE_FAT32) {

        } else if (MBR->m_type[i] == PARTITION_TYPE_EXT2) {
            err = init_ext2(MBR->m_base[i], DEV_NAME);            // 第二个分区为EXT2，读取元数据。并挂载EXT2文件系统与指定位置（/ext）
            if (IS_ERR_VALUE(err)) {
                log(LOG_FAIL, "init_ext2()");
                goto vfs_init_err;
            }
            log(LOG_OK, "init_ext2()");
        }
    }

//    err = mount_ext2();                         // 挂载EXT2文件系统与指定位置（/ext），读取元数据
//    if ( IS_ERR_VALUE(err) ){
//        my_log(LOG_FAIL, "mount_ext2()");
//        goto vfs_init_err;
//    }
//    my_log(LOG_OK, "mount_ext2()");

    print_file_systems();

    return 0;

    vfs_init_err:
    kernel_printf_vfs_errno(err);                 // 发生错误，则打印错误代码
    return err;
}

// 读取主引导记录并完善MBR相关信息
u32 vfs_read_MBR(){
    u8  *ptr_lba;
    u8  *ptr_type;
    u8  part_type;
    u32 part_lba;

    // 从外存读入MBR信息
    MBR = (struct master_boot_record*)kmalloc(sizeof(struct master_boot_record));
    if (MBR == 0)
        return -ENOMEM;

    kernel_memset(MBR->m_data, 0, sizeof(u8) * SECTOR_SIZE);
    if (read_block(MBR->m_data, 0, 1))              // MBR在外存的0号扇区
        goto vfs_read_MBR_err;

    // 完善MBR相关信息
    ptr_lba  = MBR->m_data + 446 + 8;
    ptr_type = MBR->m_data + 446 + 4;
    for (MBR->m_count = 0; MBR->m_count < DPT_MAX_ENTRY_COUNT; MBR->m_count++) {
        part_lba  = get_u32(ptr_lba);
        part_type = *ptr_type;
        if (!part_lba)
            break;

        MBR->m_base[MBR->m_count] = part_lba;
        MBR->m_type[MBR->m_count] = part_type;

        ptr_lba  += DPT_ENTRY_LEN;
        ptr_type += DPT_ENTRY_LEN;
#ifdef DEBUG_VFS
        kernel_printf("   MBR[%d]: base: %u %u\n", MBR->m_count, part_lba, part_type);
#endif
    }

    if (MBR->m_count == 0) {
        kernel_printf("[MBR ERROR]: Empty partition\n");
        goto vfs_read_MBR_err;
    } else if (MBR->m_type[0] != PARTITION_TYPE_FAT32) {
        kernel_printf("[MBR ERROR]: First partition must be FAT32\n");
        goto vfs_read_MBR_err;
    }

    return 0;

    vfs_read_MBR_err:
    kfree(MBR);
    return -EIO;
}

// 初始化文件系统
// TODO init_rootfs()
u32 init_file_systems() {

    file_systems = (struct file_system_type *)kmalloc(sizeof(struct file_system_type));
    if (file_systems == 0)
        return -ENOMEM;

    file_systems->name = "root_fs";
    file_systems->next = NULL;
    INIT_LIST_HEAD(&file_systems->fs_supers);

    return 0;
}


void kernel_printf_vfs_errno(u32 err){
    if (err == -EPERM) {
        kernel_printf("[VFS ERROR]: Operation not permitted\n");
    } else if (err == -ENOENT) {
        kernel_printf("[VFS ERROR]: No such file or directory\n");
    } else if (err == -EIO) {
        kernel_printf("[VFS ERROR]: I/O error\n");
    } else if (err == -EBADF) {
        kernel_printf("[VFS ERROR]: Bad file number\n");
    } else if (err == -ENOMEM) {
        kernel_printf("[VFS ERROR]: Out of memory\n");
    } else if (err == -EFAULT) {
        kernel_printf("[VFS ERROR]: Bad address\n");
    } else if (err == -EBUSY) {
        kernel_printf("[VFS ERROR]: Device or resource busy\n");
    } else if (err == -EEXIST) {
        kernel_printf("[VFS ERROR]: File exists\n");
    } else if (err == -ENODEV) {
        kernel_printf("[VFS ERROR]: No such device\n");
    } else if (err == -ENOTDIR) {
        kernel_printf("[VFS ERROR]: Not a directory\n");
    } else if (err == -EISDIR) {
        kernel_printf("[VFS ERROR]: Is a directory\n");
    } else if (err == -EINVAL) {
        kernel_printf("[VFS ERROR]: Invalid argument\n");
    } else if (err == -ENFILE) {
        kernel_printf("[VFS ERROR]: File table overflow\n");
    } else if (err == -ENOSPC) {
        kernel_printf("[VFS ERROR]: No space left on device\n");
    } else if (err == -ELOOP) {
        kernel_printf("[VFS ERROR]: Too many symbolic links encountered\n");
    } else {
        kernel_printf("[VFS ERROR]: Unknown error\n");
    }
}

