/*
 * spifs.h
 * @brief SPIFS文件系统
 * Created on: Sep 5, 2020
 * Author: Yanye
 */

#ifndef _FILESYS_H_
#define _FILESYS_H_

#include "common_def.h"
#include "spi_flash.h"

#define MAJOR_VERSION    (0x2)
#define MINOR_VERSION    (0x1)

// 文件状态字值，与flash相关，flash擦除后全为1，因此默认无效状态为1
typedef enum _filestatevalue {
    // 文件状态字 置位值 表明该位有效
    FILE_STATE_MARKED = 0,
    // 文件状态字 默认值 表明该位无效
    FILE_STATE_DEFAULT = 1
} FileStateValue;

// 文件状态字 (1字节) 权限描述: x表示禁止, o表示允许; 置位状态值:0, 默认状态值:1
typedef struct _file_state {
    // delete 0:删除, 1:正常文件
    // 置位权限: 读x 写x 重命名x 打开x
    // GC时将回收该文件占用区域
    uint8_t del : 1;

    // deprecate 0:失效文件, 1:正常文件
    // 置位权限: 读x 写x 重命名x 打开x
    // GC时将回收该文件占用区域
    uint8_t dep : 1;

    // read & write 0:只读文件, 1:读写文件
    // 置位权限: 读o 写x 重命名x 打开o
    uint8_t rw : 1;

    // system 0:系统文件, 1:普通文件
    // 置位权限: 文件系统层不做限制, 等同普通文件
    uint8_t sys : 1;

    // 未使用状态字, 可根据需求自定义
    uint8_t reserve : 4;
} FileState;

/**
 * @brief FileState包装类，可以避免FileState到uint8_t的强制类型转换
 */
typedef union _file_state_pack {
    uint8_t data;
    FileState fstate;
} FileStatePack;

// 文件索引块文件信息结构(4字节)
typedef struct _file_info {
    uint8_t day;        // 创建日期
    uint8_t month;     // 创建月份
    uint8_t year;     // 创建年份,以2000年为基准,记录经过年数
    FileState state; // 文件状态字
} FileInfo;

// 文件索引块结构(24字节)
typedef struct _file_block {
    uint8_t filename[8]; // 文件名
    uint8_t extname[4]; // 拓展名
    uint32_t cluster;  // 首簇地址
    uint32_t length;  // 文件大小
    FileInfo info;   // 文件信息
} FileBlock;

// 文件信息结构(24字节)
typedef struct _file {
    uint8_t filename[8]; // 文件名
    uint8_t extname[4]; // 拓展名
    uint32_t block;    // 文件索引记录地址
    uint32_t cluster; // 文件内容起始扇区地址，首簇号
    uint32_t length; // 文件大小
} File;

/**
 * @brief 文件操作结果码
 */
typedef enum _result {
    // 创建文件索引块成功
    CREATE_FILE_SUCCESS = 0,
    // 文件索引区空间不足
    NO_FILEBLOCK_SPACE,

    // 文件已存在, 用于文件创建/重命名时名称冲突检查
    FILE_ALREADY_EXIST,
    // 文件不允许写入
    CANNOT_WRITE_FILE,
    // 空文件文件未分配
    FILE_NOT_EXIST,

    // 数据区空间不足
    NO_SECTOR_SPACE,

    // 写文件成功
    WRITE_FILE_SUCCESS,
    // 追加写文件成功
    APPEND_FILE_SUCCESS,
    // 追加写结束
    APPEND_FILE_FINISH,

    // 文件不能被重命名
    FILE_CANNOT_RENAME,
    // 文件超出长度
    FILENAME_OUT_OF_BOUNDS,
    // 文件重命名成功
    FILE_RENAME_SUCCESS
} Result;

typedef enum _gc_type {
	GC_TYPE_FILEBLOCK = 0,
	GC_TYPE_DATAAREA,
	GC_TYPE_MAJOR
} GCType;

/**
 * @brief 文件写入方式枚举
 */
typedef enum _write_method {
    // 覆盖写，若文件之前存在数据，则会被标记失效
    OVERRIDE = 0,
    // 追加写，在文件尾部写入数据
    APPEND
}WriteMethod;

#include "diskio.h"

/**
 * 文件簇大小 = 扇区大小 = 4KB
 * 文件簇: 扇区标记字4字节, 数据区4088字节, 最后4字节为下一簇物理地址, FFFFFFFF表示文件结束
 * */
// 使用空指针检查
#define SPIFS_USE_NULL_CHECK

// 文件索引占用扇区号范围[FB_SECTOR_START ~ FB_SECTOR_END]
#define FB_SECTOR_START     0
#define FB_SECTOR_END       3

// 文件数据区占用扇区号范围[DATA_SECTOR_START ~ DATA_SECTOR_END]
#define DATA_SECTOR_START   4
#define DATA_SECTOR_END     255

// 文件索引占用空间大小(字节)
#define FILEBLOCK_SIZE         24
// 文件名+拓展名占用空间大小(字节)
#define FILENAME_FULLSIZE      12
#define FILENAME_SIZE          8
#define EXTNAME_SIZE           4

// 扇区使用中标记大小(字节)
#define SECTOR_MARK_SIZE       4
// 扇区使用中标记
#define SECTOR_INUSE_FLAG      (0xFFFFFFFA)
// 扇区数据废弃标记
#define SECTOR_DISCARD_FLAG    (0xFFFFFFAA)

// 空数据值, 适配flash擦除后全为1
#define EMPTY_INT_VALUE        (0xFFFFFFFF)
#define EMPTY_BYTE_VALUE       (0xFF)

// Flash页大小(字节)
#define PAGE_SIZE          256
// Flash扇区大小(字节)
#define SECTOR_SIZE        4096
// 扇区内数据域大小(字节)
#define DATA_AREA_SIZE     4088

// 由于flash擦除后全为1，且写入只能由1->0，因此状态位多个同时使用需要用&操作符
// 例如指明当前文件为系统文件且只读(FSTATE_SYSTEM & FSTATE_READONLY)
#define FSTATE_DELETE         (0xFE)
#define FSTATE_DEPRECATE      (0xFD)
#define FSTATE_READONLY       (0xFB)
#define FSTATE_SYSTEM         (0xF7)
#define FSTATE_DEFAULT        (0xFF)

// 日期的限制参数
#define YEAR_MINI_VALUE    2000
#define YEAR_MAX_VALUE     2255
#define MONTH_MINI_VALUE   1
#define MONTH_MAX_VALUE    12
#define DAY_MINI_VALUE     1
#define DAY_MAX_VALUE      31

BOOL ICACHE_FLASH_ATTR make_file(File *file, char *filename, char *extname);

BOOL ICACHE_FLASH_ATTR make_finfo(FileInfo *finfo, uint32_t year, uint8_t month, uint8_t day, uint8_t fstate);

Result ICACHE_FLASH_ATTR create_file(File *file, FileInfo *finfo);

Result ICACHE_FLASH_ATTR write_file(File *file, uint8_t *buffer, uint32_t size, WriteMethod method);

Result ICACHE_FLASH_ATTR write_finish(File *file);

uint32_t ICACHE_FLASH_ATTR read_file(File *file, uint32_t offset, uint8_t *buffer, uint32_t size);

BOOL ICACHE_FLASH_ATTR open_file(File *file, char *filename, char *extname);

BOOL ICACHE_FLASH_ATTR open_file_raw(File *file, uint8_t *filename, uint8_t *extname);

Result ICACHE_FLASH_ATTR rename_file(File *file, char *filename, char *extname);

Result ICACHE_FLASH_ATTR rename_file_raw(File *file, uint8_t *filename, uint8_t *extname);

void ICACHE_FLASH_ATTR delete_file(File *file);

uint32_t ICACHE_FLASH_ATTR list_file(uint32_t *startAddr, File *files, uint32_t max);

uint32_t ICACHE_FLASH_ATTR list_file_raw(uint32_t *startAddr, uint8_t *buffer, uint32_t max);

BOOL ICACHE_FLASH_ATTR read_finfo(File *file, FileInfo *finfo);

uint32_t ICACHE_FLASH_ATTR spifs_gc(GCType tp, uint32_t nums);

void ICACHE_FLASH_ATTR spifs_format();

BOOL ICACHE_FLASH_ATTR spifs_erase_sector(uint32_t sec);

uint32_t ICACHE_FLASH_ATTR spifs_avail_sector();

uint32_t ICACHE_FLASH_ATTR spifs_avail_files();

uint16_t ICACHE_FLASH_ATTR spifs_get_version();

#endif
