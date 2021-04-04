#include "spi_flash.h"

uint32_t spi_flash_get_id(void) {
    return W25Q32_FLASH_ID;
}

SpiFlashOpResult spi_flash_erase_sector(uint16_t sec) {
    w25q32_sector_erase(sec * SPI_FLASH_SEC_SIZE);
    return SPI_FLASH_RESULT_OK;
}

SpiFlashOpResult spi_flash_write(uint32_t des_addr, uint32_t *src_addr, uint32_t size) {
    w25q32_write_align(des_addr, src_addr, size);
    return SPI_FLASH_RESULT_OK;
}

SpiFlashOpResult spi_flash_read(uint32_t src_addr, uint32_t *des_addr, uint32_t size) {
    w25q32_read_align(src_addr, des_addr, size);
    return SPI_FLASH_RESULT_OK;
}
