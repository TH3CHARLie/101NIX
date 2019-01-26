# 101NIX

## 简介

101NIX 是浙江大学 2018-2019 秋冬学期《操作系统》课程的课程作业，是一个基于课程组提供的 ZJUNIX 进行修改的简易操作系统，运行于 Sword IV 开发板上。

## 特性

101NIX 基于原版 ZJUNIX 在进程部分、内存部分、文件部分上做出了较大改进，具体如下：

- 进程部分：重写进程模块，实现了完全公平调度、信号量、用户程序加载运行
- 内存部分：实现了用户进程的虚拟内存地址空间、进程间内存保护、用户内存系统调用、基于共享内存的进程间通信、记录型信号量机制、TLB模块，以及修复了 ZJUNIX 原版物理内存管理系统中的部分问题
- 文件部分：实现了 ext2 文件系统、重构缓存系统、基于 VFS 的分区挂载

## 分工

101NIX 是小组作业，小组人员群策群力，分工合作，实现了预期的目标，具体分工如下：

- 进程部分: yxd
- 内存部分：zzh
- 文件部分：yjc
- 整合与调试：全体

## 使用教程简介

注：此部分与原版 ZJUNIX 一致

### 编译

1. 下载安装 [Mips Toolchain](https://community.imgtec.com/developers/mips/tools/codescape-mips-sdk/)
2. 设置工具链路径
    - 在程序根目录下的 config/tools.conf 中修改 TOOLCHAIN_DIR 为编译器二进制文件所在目录
3. 设置 make 路径
    - 如果使用的是 Windows 系统，则修改 config/tools.conf 中的 MAKE 为 make 可执行文件所在路径
    - 如果使用的是 Linux 系统，则注释该行
4. 在根目录下执行 make 命令即可编译得到 kernel.bin ，也就是可载入的操作系统内核文件
    - make all / make objcopy 与直接执行 make 效果相同
    - make disassembly 可获得反编译文件 kernel.txt
    - make clean 可以清理所有中间文件，包括子目录下的所有中间文件
    - make install INSTALL_DIR=path 可以编译并将 kernel.bin 复制到 path 目录下，完成系统镜像安装

### 烧写 FPGA

1. 安装 Digilent Adept
1. 连接 JTAG 线到 SWORD 板，上电并启动
1. 在 Adept 中点击 Initialize Chain
1. 在主区域点击 Browse 选择 .bit ，点击 Program 烧写

### 安装操作系统镜像

1. 取一张 SD 卡(大于2GB，推荐使用8GB)
1. 格式化分区为 FAT32，簇大小 4096 KB，(如果内存卡太大无法指定 4096 KB簇大小，可以格式化为多个分区，将第一个分区设为 4096 KB簇大小即可)
1. 将操作系统内核 kernel.bin 放在 SD 卡根目录下(如有多个分区，即在第一个分区的根目录下)
1. 将 SD 卡插入 SWORD 板，使用板卡右下方的 RESET 按钮重启系统，即可载入内核(如不成功可多次重启)

### 注意事项

1. 源码的每个目录下都有 Makefile ，请勿删除
1. 如果工具路径中含有空格，则在 config/tools.conf 中的对应路径需要加双引号来保证路径正确性
1. kernel.bin 的文件名和路径不可修改，因为 bootloader 默认从第一个分区的根目录寻找 kernel.bin 文件来载入
1. 如果 Adept 出现 Programming Failed 提示，请查看板卡是否开启(开关是否拨到 ON 位置)
1. 如果需要重写 Makefile ，请务必保证 arch/start.o 是中间文件列表的第一项，如当前 Makefile 中的 find-all-objs 所示

## License

[![License](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](./LICENSE)

此工程遵循 BSD3 协议