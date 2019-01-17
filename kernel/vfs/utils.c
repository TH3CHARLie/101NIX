#include <zjunix/vfs/vfs.h>
#include <zjunix/utils.h>
#include <driver/sd.h>
#include <zjunix/log.h>
#include <driver/vga.h>

// 封装的读写函数
// 从addr的绝对扇区地址开始读count个扇区的数据
u32 read_block(u8 *buf, u32 addr, u32 count) {
#ifdef DEBUG_SD
    kernel_printf("                                sd_read_block: %x %d\n", addr, count);
#endif
    return sd_read_block(buf, addr, count);
}

// 从addr的绝对扇区地址开始写count个扇区的数据
u32 write_block(u8 *buf, u32 addr, u32 count) {
#ifdef DEBUG_SD
    kernel_printf("                                  sd_write_block: %x %d\n", addr, count);
#endif
    return sd_write_block(buf, addr, count);
}

// 小端模式的读取函数系列
// 读16位
u16 get_u16(u8 *ch) {
    return (*ch) + ((*(ch + 1)) << 8);
}

// 读32位
u32 get_u32(u8 *ch) {
    return (*ch) + ((*(ch + 1)) << 8) + ((*(ch + 2)) << 16) + ((*(ch + 3)) << 24);
}

// 小端模式的写入函数系列
// 写16位
void set_u16(u8 *ch, u16 num) {
    *ch = (u8)(num & 0xFF);
    *(ch + 1) = (u8)((num >> 8) & 0xFF);
}

// 写32位
void set_u32(u8 *ch, u32 num) {
    *ch = (u8)(num & 0xFF);
    *(ch + 1) = (u8)((num >> 8) & 0xFF);
    *(ch + 2) = (u8)((num >> 16) & 0xFF);
    *(ch + 3) = (u8)((num >> 24) & 0xFF);
}

// 找到word中的第一个0
u32 log2(u32 word) {
    u32 b = 0, s;

    s = 16; if (word << 16 != 0) s = 0; b += s; word >>= s;
	s =  8; if (word << 24 != 0) s = 0; b += s; word >>= s;
	s =  4; if (word << 28 != 0) s = 0; b += s; word >>= s;
	s =  2; if (word << 30 != 0) s = 0; b += s; word >>= s;
	s =  1; if (word << 31 != 0) s = 0; b += s;

    return b;
}

// 通用的文件名比较方法(相等则返回0)
u32 generic_compare_filename(const struct qstr *a, const struct qstr *b) {
    u32 i;

    if (a->len != b->len)
        return 1;
    for (i = 0; i < a->len; i++)
        if (a->name[i] != b->name[i])
            return 1;

	return 0;
}

// 不区分大小写的文件名比较方法（相等则返回0）
u32 fat32_compare_filename(const struct qstr *a, const struct qstr *b) {
    u32 i;
    char tmp1, tmp2;

    if (a->len != b->len)
        return 1;

    for (i = 0; i < a->len; i++) {
        tmp1 = a->name[i];
        tmp2 = b->name[i];
        if (tmp1 >= 'a' && tmp1 <= 'z')
            tmp1 = tmp1 + 'A' - 'a';
        if (tmp2 >= 'a' && tmp2 <= 'z')
            tmp2 = tmp2 + 'A' - 'a';
        if (tmp1 != tmp2)
            return 1;
    }

    return 0;
}

// 读位图上某一位
u32 get_bit(const u8 *bitmap, u32 index){
    const u8 *byte;
    u8 mask;
    
    byte = bitmap + index / BITS_PER_BYTE;
    mask = 1 << (index % BITS_PER_BYTE);
    
    return (u32)(*byte & mask) != 0;
}

// 把位图上某一位置1
void set_bit(u8 *bitmap, u32 index){
    u8 *byte;
    u8 mask;
    
    byte = bitmap + index / BITS_PER_BYTE;
    mask = 1 << (index % BITS_PER_BYTE);
    *byte = mask | (*byte);
}

// 把位图上某一位置0
void reset_bit(u8 *bitmap, u32 index){
    u8 *byte;
    u8 mask;
    
    byte = bitmap + index / BITS_PER_BYTE;
    mask = 1 << (index % BITS_PER_BYTE);
    mask = mask ^ 0xFF;
    *byte = mask & (*byte);
}

// 找到位图上的第一个0
u32 find_first_zero_bit(u8 *bitmap, u32 blksize) {
    u32 i, j;
    u32 bitmask;

    for (i = 0; i < blksize; i++) {
        bitmask = 1;
        for (j = 0; j < BITS_PER_BYTE; j++) {
            if (!(bitmask & bitmap[i]))
                return (i << 3) + j;
            bitmask <<= 1;
        }
    }

    return -EINVAL;
}

// 以下为哈希函数
// 为整数值计算哈希值
u32 __intHash(u32 key, u32 size) {
    u32 mask = size - 1;
    return key & mask;
}

// 为字符串计算哈希值
u32 __stringHash(struct qstr * qstr, u32 size) {
    u32 i = 0;
    u32 value = 0;
    u32 mask = size - 1;

    for (i = 0; i < qstr->len; i++) {
        value = value * 31 + (u32)(qstr->name[i]);
        value = value & mask;
    }

    return value;
}