#include "diskio.h"

/**
 * 写文件块记录
 * @param addr 物理地址
 * @param *fb 文件结构块指针, 要求指针在4字节边界
 * */
void ICACHE_FLASH_ATTR write_fileblock(uint32_t addr, FileBlock *fb) {
    // FileBlock已四字节对齐，可以强制指针转换
	spi_flash_write(addr, (uint32_t *)fb, sizeof(FileBlock));
}

/**
 * 擦除文件块记录, 只用于内存操作, 即先将flash文件索引扇区读入内存再操作
 * 操作完成需要擦除falsh对应扇区并回写数据，由于文件索引单个数据大小固定，不必进行碎片整理
 * @param baseAddr 基址
 * @param offset 偏移量
 * */
void ICACHE_FLASH_ATTR clear_fileblock(uint8_t *baseAddr, uint32_t offset) {
    os_memset((baseAddr + offset), 0xFF, FILEBLOCK_SIZE);
}

/**
 * 写入扇区状态字, secAddr一般为扇区首地址
 * @param secAddr 写入扇区首地址
 * @param mark 标记符
 * */
void ICACHE_FLASH_ATTR update_sector_mark(uint32_t secAddr, uint32_t mark) {
	spi_flash_write(secAddr, &mark, sizeof(uint32_t));
}

/**
 * 写文件块数据区首簇地址
 * @param fbaddr 文件块地址
 * @param cluster 首簇地址
 * */
void ICACHE_FLASH_ATTR write_fileblock_cluster(uint32_t fbaddr, uint32_t cluster) {
	spi_flash_write(fbaddr + 12, &cluster, sizeof(uint32_t));
}

/**
 * 写文件块文件长度
 * @param fbaddr 文件块地址
 * @param length 文件长度
 * */
void ICACHE_FLASH_ATTR write_fileblock_length(uint32_t fbaddr, uint32_t length) {
	spi_flash_write(fbaddr + 16, &length, sizeof(uint32_t));
}

/**
 * 写文件块文件状态字段
 * @param fbaddr 文件块地址
 * @param state 文件状态字段
 * */
void ICACHE_FLASH_ATTR write_fileblock_state(uint32_t fbaddr, uint8_t fstate) {
    FileInfo finfo;
    FileStatePack fspack;
    spi_flash_read(fbaddr + 20, (uint32_t *)&finfo, sizeof(uint32_t));
    fspack.fstate = finfo.state;
    // 由于flash仅能由1->0因此这里使用&操作
    fspack.data &= fstate;
    finfo.state = fspack.fstate;
    spi_flash_write(fbaddr + 20, (uint32_t *)&finfo, sizeof(uint32_t));
}
