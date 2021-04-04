#ifndef COMMON_DEF_H_INCLUDED
#define COMMON_DEF_H_INCLUDED

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define ICACHE_FLASH_ATTR
#define os_memset    memset
#define os_strlen    strlen
#define os_memcpy    memcpy
#define os_malloc    malloc
#define os_free      free


#define BOOL     uint8_t
#define TRUE     1
#define FALSE    0
#define NULL     ((void *)0)

#endif // COMMON_DEF_H_INCLUDED
