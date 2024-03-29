#ifndef _ZJUNIX_VFS_ERRNO_H
#define _ZJUNIX_VFS_ERRNO_H

#include <driver/vga.h>

// errno
#define	EPERM		 1	/* Operation not permitted */
#define	ENOENT		 2	/* No such file or directory */
#define	EIO		     5	/* I/O error */
#define	EBADF		 9	/* Bad file number */
#define	ENOMEM		12	/* Out of memory */
#define	EFAULT		14	/* Bad address */
#define EBUSY       16  /* Device or resource busy */
#define	EEXIST		17	/* File exists */
#define	ENODEV		19	/* No such device */
#define	ENOTDIR		20	/* Not a directory */
#define	EISDIR		21	/* Is a directory */
#define	EINVAL		22	/* Invalid argument */
#define	ENFILE		23	/* File table overflow */
#define	ENOSPC		28	/* No space left on device */
#define	ELOOP		62	/* Too many symbolic links encountered */

void kernel_printf_vfs_errno(u32 err);

#endif


