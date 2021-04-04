#ifndef __DISKIO_H__
#define __DISKIO_H__

#include "common_def.h"
#include "spi_flash.h"
#include "spifs.h"

void ICACHE_FLASH_ATTR write_fileblock(uint32_t addr, FileBlock *fb);
void ICACHE_FLASH_ATTR clear_fileblock(uint8_t *baseAddr, uint32_t offset);
void ICACHE_FLASH_ATTR update_sector_mark(uint32_t secAddr, uint32_t mark);

void ICACHE_FLASH_ATTR write_fileblock_cluster(uint32_t fbaddr, uint32_t cluster);
void ICACHE_FLASH_ATTR write_fileblock_length(uint32_t fbaddr, uint32_t length);
void ICACHE_FLASH_ATTR write_fileblock_state(uint32_t fbaddr, uint8_t fstate);

#endif
