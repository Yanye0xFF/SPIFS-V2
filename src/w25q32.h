#ifndef _W25Q32_H_
#define _W25Q32_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define W25Q32_SIZE 4194304

void w25q32_allocate();
void w25q32_destory();

uint8_t *w25q32_getbuffer();
uint8_t w25q32_output(const char *filePath, const char *mode, uint32_t offset, uint32_t size);

void w25q32_read_align(uint32_t src_addr, uint32_t *des_addr, uint32_t size);
void w25q32_write_align(uint32_t des_addr, uint32_t *src_addr, uint32_t size);

uint32_t w25q32_read(uint32_t address, uint8_t *buffer, uint32_t size);
uint8_t w25q32_write_page(uint32_t address, uint8_t *buffer, uint32_t size);
uint8_t w25q32_write_multipage(uint32_t address, uint8_t *buffer, uint32_t size);

uint8_t w25q32_chip_erase();
uint8_t w25q32_sector_erase(uint32_t address);
uint8_t w25q32_block_erase_32k(uint32_t address);
uint8_t w25q32_block_erase_64k(uint32_t address);

#endif
