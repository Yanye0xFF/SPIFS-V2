# SPIFS-V2
 SPIFS的升级版，V2与之前版本存在较大差异，是一个全新的版本不兼容之前的代码。<br/>
 无需要在flash中写入文件系统的元数据(metadata)，首次使用全擦flash即可，主要用于32位arm平台<br/>
 src目录为codeblocks项目，main.c中提供了对文件进行读/写/追加/更名/查找等基本操作。<br/>
 文档传送门：https://www.cnblogs.com/yanye0xff/p/14616965.html<br/>
 update 20210217 fb_has_name改为4字节对齐操作。<br/>
 update 20210221 完善align_write_impl/align_read_impl对于非对齐地址处理。<br/>
 update 20210818 修复了"spifs_gc()"函数内存泄漏的问题
