#ifndef __DISKIO_H__
#define __DISKIO_H__

#include "stdint.h"
#include "spifs.h"
#include "w25q32.h"
#include "fslib.h"

void spi_flash_write(uint32_t addr, uint32_t *data, uint32_t size);
void spi_flash_read(uint32_t addr, uint32_t *buffer, uint32_t size);
void spi_flash_erase_sector(uint32_t secIndex);

void write_fileblock(uint32_t addr, FileBlock *fb);
void clear_fileblock(uint8_t *baseAddr, uint32_t offset);
void update_sector_mark(uint32_t secAddr, uint32_t mark);

void write_fileblock_cluster(uint32_t fbaddr, uint32_t cluster);
void write_fileblock_length(uint32_t fbaddr, uint32_t length);
void write_fileblock_state(uint32_t fbaddr, uint8_t fstate);

#endif
