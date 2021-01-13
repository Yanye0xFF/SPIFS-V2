#ifndef FSLIB_H_INCLUDED
#define FSLIB_H_INCLUDED

#include "stdlib.h"
#include "string.h"

#define os_memset    memset
#define os_memcpy    memcpy
#define os_strlen    strlen
#define os_free      free
#define os_malloc    malloc

#define BOOL    unsigned char
#define FALSE   0
#define TRUE    1

#endif // FSLIB_H_INCLUDED
