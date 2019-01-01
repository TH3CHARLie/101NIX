#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>

#include <zjunix/log.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/utils.h>

extern struct dentry            * root_dentry;
extern struct vfsmount          * root_mnt;

// 固定挂载：将第二个分区的ext2系统挂载到第一个分区的fat32文件系统的/ext2目录上
u32 mount_ext2(){
    u32 err;
    struct      qstr            qstr;
    struct      dentry          * dentry;
    struct      vfsmount        * mnt;
    struct      list_head       * a;
    struct      list_head       * begin;

    a = &(root_mnt->mnt_hash);
    begin = a;
    a = a->next;
    while ( a != begin ){
        mnt = container_of(a, struct vfsmount, mnt_hash);
        if ( kernel_strcmp(mnt->mnt_sb->s_type->name ,"ext2") == 0 )
            break;
    }
        
    if ( a == begin ) 
        return -ENOENT;
    
    qstr.name = "ext2";
    qstr.len = 4;
    

    // 创建 /ext2 对应的dentry。而且暂时不需要创建对应的inode
    dentry = d_alloc(root_dentry, &qstr);
    if (dentry == 0)
        return -ENOENT;

    dentry->d_mounted = 1;
    dentry->d_inode = mnt->mnt_root->d_inode;
    mnt->mnt_mountpoint = dentry;
    mnt->mnt_parent = root_mnt;
    
    return 0;
}

// 如果目录项是指向某个文件系统安装点的一个目录，需要通过当前文件系统的目录项对象和文件系统对象
// 找到挂载点的真正的dentry结构
// 因为要通过形参修改指针所指向的东西，所以要用二级指针
u32 follow_mount(struct vfsmount **mnt, struct dentry **dentry){
    u32 res = 0;

    // 查找挂载的对象
    while ((*dentry)->d_mounted) {
		struct vfsmount *mounted = lookup_mnt(*mnt, *dentry);
		if (!mounted)
			break;

        // 找到挂载点，更换dentry和mnt的信息
        *mnt = mounted;
        dput(*dentry);
        *dentry = mounted->mnt_root;
        dget(*dentry);
		res = 1;
    }
    
    return res;
}

// 遍历当前文件系统对象的mount链表，找到挂载点，然后更换文件系统对象
struct vfsmount * lookup_mnt(struct vfsmount *mnt, struct dentry *dentry) {
	struct list_head *head = &(mnt->mnt_hash);
	struct list_head *tmp = head;
	struct vfsmount *p, *found = 0;

    // 在字段为hash的双向链表寻找。这里有所有已安装的文件系统的对象
    // 这里并没有为其实现hash查找，仅普通链表
	for (;;) {
		tmp = tmp->next;
		p = 0;
		if (tmp == head)
			break;
		p = list_entry(tmp, struct vfsmount, mnt_hash);
		if (p->mnt_parent == mnt && p->mnt_mountpoint == dentry) {
			found = p;
			break;
		}
	}
	
	return found;
}