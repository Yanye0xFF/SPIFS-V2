#include "diskio.h"

/**
 * @brief 读取flashId
 */
uint32_t spi_flash_get_id(void) {
    return FLASH_ID;
}

/**
 * @brief falsh扇区擦除
 * @aramn sec 扇区号,从扇区0开始计数,每扇区4KB
 * @return SpiFlashOpResult
 */
SpiFlashOpResult spi_flash_erase_sector(uint16_t sec) {
    w25q32_sector_erase(sec * 0x1000);
    return SPI_FLASH_RESULT_OK;
}

/**
 * @brief 4字节对齐写flash
 * @aramn des_addr 写入flash目的地址
 * @aramn *src_addr 写入数据指针
 * @aramn size 数据长度,单位byte,需要四字节对齐
 * @return SpiFlashOpResult
 */
SpiFlashOpResult spi_flash_write(uint32_t des_addr, uint32_t *src_addr, uint32_t size) {
    w25q32_write_align(des_addr, src_addr, size);
    return SPI_FLASH_RESULT_OK;
}

/**
 * @brief 4字节对齐写flash
 * @aramn src_addr 读取flash目的地址
 * @aramn *des_addr 存放数据指针
 * @aramn size 数据长度,单位byte,需要四字节对齐
 * @return SpiFlashOpResult
 */
SpiFlashOpResult spi_flash_read(uint32_t src_addr, uint32_t *des_addr, uint32_t size) {
    w25q32_read_align(src_addr, des_addr, size);
    return SPI_FLASH_RESULT_OK;
}

/**
 * 写文件块记录
 * @param addr 物理地址
 * @param *fb 文件结构块指针
 * */
void write_fileblock(uint32_t addr, FileBlock *fb) {
    // FileBlock已四字节对齐，可以强制指针转换
    w25q32_write_align(addr, (uint32_t *)fb, sizeof(FileBlock));
}

/**
 * 擦除文件块记录, 只用于内存操作, 即先将flash文件索引扇区读入内存再操作
 * 操作完成需要擦除falsh对应扇区并回写数据，由于文件索引单个数据大小固定，不必进行碎片整理
 * @param baseAddr 基址
 * @param offset 偏移量
 * */
void clear_fileblock(uint8_t *baseAddr, uint32_t offset) {
    memset((baseAddr + offset), 0xFF, FILEBLOCK_SIZE);
}

/**
 * 写入扇区状态字, secAddr一般为扇区首地址
 * @param secAddr 写入扇区首地址
 * @param mark 标记符
 * */
void update_sector_mark(uint32_t secAddr, uint32_t mark) {
    w25q32_write_align(secAddr, &mark, sizeof(uint32_t));
}

/**
 * 写文件块数据区首簇地址
 * @param fbaddr 文件块地址
 * @param cluster 首簇地址
 * */
void write_fileblock_cluster(uint32_t fbaddr, uint32_t cluster) {
    w25q32_write_align(fbaddr + 12, &cluster, sizeof(uint32_t));
}

/**
 * 写文件块文件长度
 * @param fbaddr 文件块地址
 * @param length 文件长度
 * */
void write_fileblock_length(uint32_t fbaddr, uint32_t length) {
    w25q32_write_align(fbaddr + 16, &length, sizeof(uint32_t));
}

/**
 * 写文件块文件状态字段
 * @param fbaddr 文件块地址
 * @param state 文件状态字段
 * */
void write_fileblock_state(uint32_t fbaddr, uint8_t fstate) {
    FileInfo finfo;
    FileStatePack fspack;
    w25q32_read_align(fbaddr + 20, (uint32_t *)&finfo, sizeof(uint32_t));
    fspack.fstate = finfo.state;
    // 由于flash仅能由1->0因此这里使用&操作
    fspack.data &= fstate;
    finfo.state = fspack.fstate;
    w25q32_write_align(fbaddr + 20, (uint32_t *)&finfo, sizeof(uint32_t));
}
