#ifndef _ZJUNIX_VFS_ERR_H
#define _ZJUNIX_VFS_ERR_H

#include <zjunix/vfs/errno.h>

#define MAX_ERRNO	4095
#define IS_ERR_VALUE(x) ((x) >= (u32)-MAX_ERRNO)

// 把错误码转化为指针，供之后的转化使用
static inline void * ERR_PTR(u32 error){
	return (void *) error;
}

// 把指针转化为整数，以打印
static inline u32 PTR_ERR(const void *ptr){
	return (u32) ptr;
}

// 判断是不是错误指针
static inline u32 IS_ERR(const void *ptr){
	return IS_ERR_VALUE((u32)ptr);
}

// 判断是不是错误指针或者空指针
static inline u32 IS_ERR_OR_NULL(const void *ptr){
	return (!ptr) || IS_ERR_VALUE((u32)ptr);
}

// 把错误指针的const属性转化掉
static inline void * ERR_CAST(const void *ptr){
	return (void *) ptr;
}

// 把错误指针或者空指针转化为整数
static inline u32 PTR_ERR_OR_ZERO(const void *ptr){
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);
	else
		return 0;
}

#define PTR_RET(p) PTR_ERR_OR_ZERO(p)

#endif