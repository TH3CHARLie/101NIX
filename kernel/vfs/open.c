#include <zjunix/vfs/vfs.h>
#include <zjunix/vfs/vfscache.h>

#include <zjunix/utils.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/log.h>

// 外部变量
extern struct cache     *dcache;
extern struct dentry    *root_dentry;
extern struct dentry    *pwd_dentry;
extern struct vfsmount  *root_mnt;
extern struct vfsmount  *pwd_mnt;

// 文件打开函数
struct file* vfs_open(const u8 *filename, u32 flags, u32 mode){
    u32 namei_flags;
    u32 err;
    struct file         *f;
    struct nameidata    nd;

    // 重置flags，因为内外部定义有些地方不一致
    namei_flags = flags;                                 
    if ( (namei_flags + 1) & O_ACCMODE )
        namei_flags ++;
    
    // 逐层解析路径，并把文件关联的dentry和vfsmount对象保存在nd结构中
    err = open_namei(filename, namei_flags, mode, &nd);

    // 用得到的nd结构初始化file对象，并返回
    if (!err)
        return dentry_open(nd.dentry, nd.mnt, flags);

    return ERR_PTR(err);
}

u32 open_namei(const u8 *pathname, u32 flag, u32 mode, struct nameidata *nd){
    u32 err;
    u32 acc_mode;
    struct path     path;
    struct dentry   *dir;
    struct dentry   *dentry;

    // 从打开模式设置访问参数
    acc_mode = ACC_MODE(flag);
    if (flag & O_APPEND)                // O_APPEND 暗示同，但为了区分，另取追加模式
        acc_mode |= MAY_APPEND;
    
    nd->intent.open.flags = flag;       // TOKNOW
    nd->intent.open.create_mode = mode;

    // 没有指定新建，只需要查找，然后直接返回
    // 在nd结构中，dentry和mnt字段将会指向原路径名最后一个所解析分量指向的对象
    if (!(flag & O_CREAT)) {
        err = path_lookup(pathname, LOOKUP_FOLLOW, nd);
        if (err)
            return err;
        goto ok;
    }   
    
    // 设置了O_CREATE，设置LOOKUP_PARENT|LOOKUP_CREATE进行查找
    if ( err = path_lookup(pathname, LOOKUP_PARENT|LOOKUP_CREATE, nd) )
        return err;
    
    // 现在nd里面有最后一个分量的目录对应的mnt和dentry对象，还有最后一个分量的信息
    // 但是所要创建的文件（即这个分量）是否已经存在，现在是不知道的

    // 不是普通的文件，不认为可以创建
	err = -EISDIR;
	if (nd->last_type != LAST_NORM || nd->last.name[nd->last.len])
		goto exit;

	dir = nd->dentry;
	nd->flags &= ~LOOKUP_PARENT;
	dentry = __lookup_hash(&nd->last, nd->dentry, nd);

do_last:
    // 若无法创建dentry
	err = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

    // 若所要创建的分量的确不存在于外存，那现在就创建好一个dentry了
    // 接下来需要新建一个inode，并且与这个dentry建立联系
	if (!dentry->d_inode) {
        err = dir->d_inode->i_op->create(dir->d_inode, dentry, mode, nd);
        dput(nd->dentry);
		nd->dentry = dentry;
		if (err)
			goto exit;

		acc_mode = 0;
		goto ok;
	}

    // 若分量已经存在了
	err = -ENOENT;
	if (!dentry->d_inode)
		goto exit_dput;

	dput(nd->dentry);
	nd->dentry = dentry;
ok:
	return 0;
exit_dput:
    dput(dentry);
exit:
	dput(nd->dentry);
	return err;

}

// 根据父目录和名字查找对应的目录项（创建模式会调用）
struct dentry * __lookup_hash(struct qstr *name, struct dentry *base, struct nameidata *nd) {
    u32 err;
	struct dentry   *dentry;
	struct inode    *inode;

    inode = base->d_inode;
    // 在目录项高速缓存中寻找，查找文件名和父目录项相符的目录缓冲项
    struct condition cond;
    cond.cond1 = (void*) nd->dentry;
    cond.cond2 = (void*) name;
    dentry = (struct dentry*) dcache->c_op->look_up(dcache, &cond);

    // 如果没有找到，尝试在外存找
	if (!dentry) {
        // 新dentry首先需要被创建
        struct dentry *new = d_alloc(base, name);
		dentry = ERR_PTR(-ENOMEM);
		if (!new)
            return dentry;
        
        // 尝试在外存中查找需要的dentry对应的inode。若找到，相应的inode会被新建并加入高速缓存，dentry与之的联系也会被建立
		dentry = inode->i_op->lookup(inode, new, nd);
		if (!dentry)
			dentry = new;   // 若相应的inode并没能找到，则需要进一步创建inode。dentry的引用计数暂时需要保持
		else
			dput(new);
	}

	return dentry;
}

u32 path_lookup(const u8 * name, u32 flags, struct nameidata *nd) {
    nd->last_type = LAST_ROOT;
    nd->flags = flags;

    // 如果路径名的第一个字符是“/”，那么这个路径名是绝对路径，从根目录开始搜索
    if ( *name == '/' ) {
        dget(root_dentry);
        nd->mnt     = root_mnt;
        nd->dentry  = root_dentry;
    }
    
    // 否则认为是相对路径，从当前目录开始搜索
    else {
        dget(pwd_dentry);
        nd->mnt     = pwd_mnt;
        nd->dentry  = pwd_dentry;
    }
    
    return link_path_walk(name, nd);
}

// 从文件路径名导出相应的索引节点
// 暂时不考虑任何与符号链接有关的事
u32 link_path_walk(const u8 *name, struct nameidata *nd) {
    u32 err;
    struct path     next;
    struct inode    *inode;
    u32 lookup_flags = nd->flags;

    // 跳过第一个分量前的任何‘/’或空格
    while (*name=='/' || *name == ' ' )  
        name++;
    if (!(*name))
        goto return_reval;

    // 初始化inode，指向开始路径名查找的目录
    // 此后将指向最后一个所解析分量的索引节点对象
    inode = nd->dentry->d_inode;

    // 把剩余的文件路径名分解为分量，对于每一个分量作以下处理。
    for( ; ; ){
        u8 c;
        struct qstr this;
        
        // 考虑要解析的下一个分量
        this.name = name;
        do {  
            name++;      
            c = *(const u8 *)name;  
        } while (c && (c != '/'));  
        this.len = name - (const u8*) this.name;

        // 要解析的分量是原路径名的最后一个分量
        if (!c)  
            goto last_component;
        
        // 如果‘/’终止了要解析的分量名，跳过它之后的任何尾部‘/’
        while (*++name == '/');

        // 如果路径名尾部有一个“/”
        if (!*name)
            goto last_with_slashes;

        // 处理“.”和“..”分量
        if (this.name[0] == '.') switch (this.len) {  
            default:
                break;

            // 分量名是“..”，尝试回到父目录（nd的mnt和dentry回到父目录），并继续下一分量
            case 2:
                if (this.name[1] != '.')  
                    break;  
                follow_dotdot(&nd->mnt, &nd->dentry);  
                inode = nd->dentry->d_inode;

            // 分量名是“.”，继续下一分量
            case 1:  
                continue;
        }
        
        // 既不是“.”又不是“..”的话，只能在目录项高速缓存去查找它

        nd->flags |= LOOKUP_CONTINUE;               // 表示还有下一个分量要分析
        err = do_lookup(nd, &this, &next);          // 真正的查找，得到与给定的父目录（nd->dentry）和文件名
        if (err)                                    // (this.name)相关的目录项对象（next.dentry）
            break;

        // 检查刚解析的分量（next.dentry）是否指向某个文件系统安装点的一个目录
        // 是的话更新这个值，以便它们指向由这个路径分量所表示的目录上安装的
        // 最上层文件系统的目录项对象和已安装文件系统对象
        follow_mount(&next.mnt, &next.dentry);

        // 检查刚解析的分量，首先检查对应inode是否为空
        err = -ENOENT;
        inode = next.dentry->d_inode;

		if (!inode){
            goto out_dput;
        }
			
        err = -ENOTDIR; 
		if (!inode->i_op)
            goto out_dput;
            
        dput(nd->dentry);
        nd->mnt = next.mnt;
        nd->dentry = next.dentry;

        // 检查刚解析的分量是否为目录，因为不是最后一个分量，应该是目录。非目录则返回错误
        err = -ENOTDIR;
		if (!inode->i_op->lookup)
            break;
            
        continue;
        // 主for循环在此终止，接下来是各种特殊处理片段

last_with_slashes:
        // 修改标志位，强制由后面的函数来解释最后一个作为目录名的分量
        lookup_flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;

last_component:
        // 现在，除了最后一个分量，原路径名所有分量被解析
        nd->flags &= ~LOOKUP_CONTINUE;              // 消除LOOKUP_CONTINUE标志

		if (lookup_flags & LOOKUP_PARENT)           // 检查LOOKUP_PARENT标志，看是否要查找最后一个分量的目录
            goto lookup_parent;
        
        // 处理“.”和“..”分量
		if (this.name[0] == '.') switch (this.len) {
			default:
                break;
                
            // 分量名是“..”，尝试回到父目录
			case 2:	
				if (this.name[1] != '.')
					break;
				follow_dotdot(&nd->mnt, &nd->dentry);
				inode = nd->dentry->d_inode;
            
            // 分量名是“.”，终止并返回值0。在nd中，dentry和mnt字段指向路径名中倒数第二个分量对应的对象
			case 1:
				goto return_reval;
        }
            
        // 既不是“.”又不是“..”的话，只能在目录项高速缓存去查找它
        // 在这一步结束时，next局部变量存放的是指向最后分量名对应的目录项和已安装文件系统描述符的指针
		err = do_lookup(nd, &this, &next);
		if (err)
            break;

        // 检查刚解析的分量（next.dentry）是否指向某个文件系统安装点的一个目录
        // 是的话更新这个值，以便它们指向由这个路径分量所表示的目录上安装的
        // 最上层文件系统的目录项对象和已安装文件系统对象
        follow_mount(&next.mnt, &next.dentry);

        // 假设要解析的分量不是一个符号链接（符号链接处理略过），最后的目录项对象就是整个查找操作的结果
        dput(next.dentry);
        nd->mnt = next.mnt;
		nd->dentry = next.dentry;

        // 检查是否没有索引节点对象与目录项关联，通常是因为路径名指向一个不存在的文件
        inode = nd->dentry->d_inode;

		err = -ENOENT;
		if (!inode){
            break;
        }
        
        // 如果在lookup_flags设置了LOOKUP_DIRECTORY方法，检查它是不是目录
		if (lookup_flags & LOOKUP_DIRECTORY) {
			err = -ENOTDIR; 
			if (!inode->i_op || !inode->i_op->lookup)
				break;
        }
            
        //无错误返回
        goto return_base;
        
lookup_parent:
        // 来到这里，是需要寻找父路径名。比如，当创建文件时。（this已经是该父路径了）
        // 最后一个分量不会被解释，因此从这里离开时，nd的dentry和mnt字段指向的是最后一个对象的目录对应的对象

        // 当需要LOOKUP_PARENT时，意味着nd的如下字段需要被建立
        nd->last = this;
        
		nd->last_type = LAST_NORM;
		if (this.name[0] != '.')
            goto return_base;
        
		if (this.len == 1)
			nd->last_type = LAST_DOT;
		else if (this.len == 2 && this.name[1] == '.')
			nd->last_type = LAST_DOTDOT;
		else
            goto return_base;
        
return_reval:
        // 这里本来是检查dentry的有效性的，此略过
return_base:
		return 0;
out_dput:
        dput(next.dentry);
		break;
    }
    dput(nd->dentry);
return_err:
    return err;
}

// 尝试回退到父目录
inline void follow_dotdot(struct vfsmount **mnt, struct dentry **dentry){

    while(1) {
		struct vfsmount *parent;
		struct dentry *old = *dentry;

        // 如果当前所处的目录即为根目录则退出
		if (*dentry == root_dentry && *mnt == root_mnt )
			break;
        
        // 如果当前所处的目录不为当前路径所属文件系统的根目录，可以直接向上退一级，然后退出
		if (*dentry != (*mnt)->mnt_root) {
            *dentry = (*dentry)->d_parent;
            dget(*dentry);
            dput(old);
			break;
		}
        
        // 当前所处的目录为当前路径所属文件系统的根目录
        parent = (*mnt)->mnt_parent;
        // 文件系统即为本身，则表明没有父文件系统，退出
		if (parent == *mnt)
			break;

        // 取当前文件系统的挂载点，退回父文件系统
        *dentry = (*mnt)->mnt_mountpoint;
        dget(*dentry);
        dput(old);
        *mnt = parent;
        // 回到前面两种情况
    }
}

// 根据父目录和名字查找对应的目录项
u32 do_lookup(struct nameidata *nd, struct qstr *name, struct path *path){
    
    struct vfsmount *mnt = nd->mnt;
    // 在目录项高速缓存中寻找，查找文件名和父目录项相符的目录缓冲项
    struct condition cond;
    cond.cond1 = (void*) nd->dentry;
    cond.cond2 = (void*) name;
    struct dentry* dentry = (struct dentry*) dcache->c_op->look_up(dcache, &cond); 
    
    // 目录项高速缓冲（内存）没有，到外存中找
    if (!dentry)
        goto need_lookup;  

done:  
    // 找到，修改path的字段，返回无错误
    path->mnt = mnt;
    path->dentry = dentry;
    dget(dentry);
    return 0;  
  
need_lookup:
    // 即将使用底层文件系统在外存中查找，并构建需要的目录项
    dentry = real_lookup(nd->dentry, name, nd);
    if (IS_ERR(dentry))
        goto fail;
    goto done;
  
fail:
    return PTR_ERR(dentry);
}

// 调用底层文件系统的方法，尝试在外存中查找
struct dentry * real_lookup(struct dentry *parent, struct qstr *name, struct nameidata *nd){
    struct dentry   *result;
    struct inode    *dir = parent->d_inode;

    // 新建一个dentry对象，并且用parent和name初始化之
    struct dentry *dentry = d_alloc(parent, name);
    result = ERR_PTR(-ENOMEM);

	if (dentry) {
        // 尝试在外存中查找需要的dentry对应的inode。若找到，相应的inode会被新建并加入高速缓存，dentry与之的联系也会被建立
        result = parent->d_inode->i_op->lookup(dir, dentry, nd);
        
		if (result)
			dput(dentry);
		else                    // 若相应的inode并没能找到，则需要进一步创建inode。dentry的引用计数暂时需要保持
			result = dentry;
    }
    
    return result;
}

// 新建一个file对象并用相应的数据填充
struct file * dentry_open(struct dentry *dentry, struct vfsmount *mnt, u32 flags) {
	struct file *f;
	struct inode *inode;
	u32 error;

    // 从内存分配一个file对象
	error = -ENFILE;
	f = (struct file* ) kmalloc ( sizeof(struct file) );
    INIT_LIST_HEAD(&f->f_list);
	if (!f)
        goto cleanup_dentry;
    
    // 初始化file对象的各项参数
    inode           = dentry->d_inode;

	f->f_flags      = flags;
	f->f_mode       = ((flags+1) & O_ACCMODE) | FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE ;
	f->f_mapping    = &(inode->i_data);
	f->f_dentry     = dentry;
	f->f_vfsmnt     = mnt;
	f->f_pos        = 0;
	f->f_op         = inode->i_fop;
	f->f_flags      &= ~(O_CREAT);

	return f;

cleanup_all:
	f->f_dentry     = 0;
    f->f_vfsmnt     = 0;

cleanup_file:
    list_del_init(&f->f_list);
    kfree(f);

cleanup_dentry:
    dput(dentry);
    return ERR_PTR(error);
}

// 文件关闭入口
u32 vfs_close(struct file *filp) {
	u32 err;

    // 把页高速缓存的数据（如果需要）全部写回磁盘    
	err = filp->f_op->flush(filp);
    if (!err)
        kfree(filp);

	return err;
}

// 根据相应信息新建一个dentry项，填充相关信息，并放进dentry高速缓存
struct dentry * d_alloc(struct dentry *parent, const struct qstr *name){  
    u8* dname;
    u32 i;
    struct dentry* dentry;  
    
    dentry = (struct dentry *) kmalloc ( sizeof(struct dentry) );
    if (!dentry)  
        return 0;
    
    dname = (u8*) kmalloc ( (name->len + 1)* sizeof(u8*) );
    kernel_memset(dname, 0, (name->len + 1));
    for ( i = 0; i < name->len; i++ ){
        dname[i] = name->name[i];
    }
    dname[i] == '\0';


    dentry->d_name.name         = dname;
    dentry->d_name.len          = name->len;   
    dentry->d_count             = 1;
    dentry->d_inode             = 0;  
    dentry->d_parent            = parent;
    dentry->d_sb                = parent->d_sb;
    dentry->d_op                = 0;
    
    INIT_LIST_HEAD(&dentry->d_hash);  
    INIT_LIST_HEAD(&dentry->d_LRU);  
    INIT_LIST_HEAD(&dentry->d_subdirs);
    INIT_LIST_HEAD(&(root_dentry->d_alias));

    if (parent) {
        dentry->d_parent = parent;
        dget(parent);
        dentry->d_sb = parent->d_sb;
        list_add(&dentry->d_child, &parent->d_subdirs);
	} else {
		INIT_LIST_HEAD(&dentry->d_child);
	}

    dcache->c_op->add(dcache, (void*)dentry);
    return dentry;
}