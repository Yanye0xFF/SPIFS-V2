#include "w25q32.h"

static uint8_t *w25q32_buffer = NULL;
static uint8_t erase_impl(uint32_t address, uint32_t erase_size);

void w25q32_allocate() {
    if(w25q32_buffer == NULL) {
        // 4MegaBytes
        w25q32_buffer = (uint8_t *)malloc(sizeof(uint8_t) * W25Q32_SIZE);
    }
}

void w25q32_destory() {
    if(w25q32_buffer != NULL) {
        free(w25q32_buffer);
    }
}

uint8_t *w25q32_getbuffer() {
    return w25q32_buffer;
}

/**
 * 将模拟flash内存写入磁盘
 * @param fileName 文件路径
 * @param mode 写入方式
 * @param size 写入大小(字节)
 * @return 0: 打开文件失败, 1: 写入成功
 * */
uint8_t w25q32_output(const char *filePath, const char *mode, uint32_t offset, uint32_t size) {
    FILE *out = fopen(filePath, mode);
    if(out == NULL) {
        return 0;
    }
    fwrite((w25q32_buffer + offset), sizeof(uint8_t), size, out);
    fclose(out);
    return 1;
}




/**
 * 整片擦除,擦除完成后为FF
 * W25Q16:25s
 * W25Q32:40s
 * W25Q64:40s
 * @return state register
 * */
uint8_t w25q32_chip_erase() {
    memset(w25q32_buffer, 0xFF, W25Q32_SIZE);
	return 0x2;
}

/**
 * 扇区擦除 4KB, Tmin = 150ms
 * @param address 扇区起始地址,地址自动对齐到4K边界
 * @return state register
 * */
uint8_t w25q32_sector_erase(uint32_t address) {
	return erase_impl(address, 0x1000);
}

/**
 * 块擦除 32KB
 * @param address 32K块起始地址,地址自动对齐到32K边界
 * @return state register
 * */
uint8_t w25q32_block_erase_32k(uint32_t address) {
	return erase_impl(address, 0x8000);
}

/**
 * 块擦除 64KB
 * @param address 64K块起始地址,地址自动对齐到64K边界
 * @return state register
 * */
uint8_t w25q32_block_erase_64k(uint32_t address) {
	return erase_impl(address, 0x10000);
}

static uint8_t erase_impl(uint32_t address, uint32_t size) {
    memset((w25q32_buffer + address), 0xFF, size);
	return 0x2;
}
/**
 * @brief 四字节对齐读flash
 * @param src_addr flash地址, 四字节边界
 * @param *des_addr 读出数据缓冲区
 * @param size 读取长度, 单位byte, 需要四字节对齐
 */
void w25q32_read_align(uint32_t src_addr, uint32_t *des_addr, uint32_t size) {
    memcpy(des_addr, (w25q32_buffer + src_addr), size);
}

/**
 * @brief 四字节对齐写flash
 * @param des_addr flash地址, 四字节边界
 * @param *src_addr 写到flash数据源
 * @param size 写长度, 单位byte, 需要四字节对齐
 */
void w25q32_write_align(uint32_t des_addr, uint32_t *src_addr, uint32_t size) {
    memcpy((w25q32_buffer + des_addr), src_addr, size);
}

/**
 * 读数据,不限制长度
 * @param buffer 写入数据缓冲区
 * @param size 写入数据长度
 * @param address 写入地址
 * @return state register
 * */
uint32_t w25q32_read(uint32_t address, uint8_t *buffer, uint32_t size) {
	if(buffer == NULL || size <= 0) {
		return 0x00;
	}
	memcpy(buffer, (w25q32_buffer + address), size);
	return size;
}

/**
 * 写一页数据,最大256bytes
 * 由于超出后会回到初始地址覆盖数据,故限制size <= 256
 * @param buffer 写入数据缓冲区
 * @param size 入数据长度
 * @param address 写入地址
 * @return state register
 * */
uint8_t w25q32_write_page(uint32_t address, uint8_t *buffer, uint32_t size) {
	if(buffer == NULL || size <= 0) {
		return 0x00;
	}
	size = (size > 256) ? 256 : size;
	memcpy((w25q32_buffer + address), buffer, size);
	return 0x2;
}

/**
 * 写多页数据,自动换页不限制长度
 * @param buffer 写入数据缓冲区
 * @param size 写入数据长度
 * @param address 写入地址
 * @return state register
 * */
uint8_t w25q32_write_multipage(uint32_t address, uint8_t *buffer, uint32_t size) {
	if(buffer == NULL || size <= 0) {
		return 0x00;
	}
    uint32_t offset = 0, write_size = 0;
    while(size) {
        write_size = (size >= 256) ? 256 : (size % 256);
        w25q32_write_page((address + offset), (buffer + offset), write_size);
        offset += write_size;
        size -= write_size;
    }
	return 0x2;
}

