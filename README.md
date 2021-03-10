# SPIFS-V2
 SPIFS的升级版，V2与之前版本存在较大差异，需要重新编译才可使用。<br/>
 无需要在flash中写入文件系统的元数据(metadata)，首次使用全擦flash即可，主要用于32位arm平台<br/>
 update 20210217 fb_has_name改为4字节对齐操作。<br/>
 update 20210221 完善align_write_impl/align_read_impl对于非对齐地址处理。<br/>
