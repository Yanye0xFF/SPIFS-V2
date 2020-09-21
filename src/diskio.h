#ifndef __DISKIO_H__
#define __DISKIO_H__

#include "stdint.h"
#include "w25q32.h"
#include "spifs.h"

typedef	enum {
    SPI_FLASH_RESULT_OK,
    SPI_FLASH_RESULT_ERR,
    SPI_FLASH_RESULT_TIMEOUT
} SpiFlashOpResult;

#define FLASH_ID    (0x1640ef)

// 需要自行移植的部分
uint32_t spi_flash_get_id(void);

SpiFlashOpResult spi_flash_erase_sector(uint16_t sec);
SpiFlashOpResult spi_flash_write(uint32_t des_addr, uint32_t *src_addr, uint32_t size);
SpiFlashOpResult spi_flash_read(uint32_t src_addr, uint32_t *des_addr, uint32_t size);
//


void write_fileblock(uint32_t addr, FileBlock *fb);
void clear_fileblock(uint8_t *baseAddr, uint32_t offset);
void update_sector_mark(uint32_t secAddr, uint32_t mark);

void write_fileblock_cluster(uint32_t fbaddr, uint32_t cluster);
void write_fileblock_length(uint32_t fbaddr, uint32_t length);
void write_fileblock_state(uint32_t fbaddr, uint8_t fstate);

#endif
