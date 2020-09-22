#include "spifs.h"

static uint32_t strlen_ext(uint8_t *str, FileNameType type);

static BOOL fb_has_name(uint8_t *fb_buffer);

static BOOL find_empty_sector(uint32_t *secList, uint32_t nums);

static BOOL fname_equals(uint8_t *fsname, char *inname, FileNameType type);

/**
 * 创建文件信息字段
 * @param *finfo 信息字段指针
 * @param year 文件创建年份(2000~2255),以2000年为起点
 * @param month 文件创建月份(1~12)
 * @param day 文件创建的日期(1~N),N: day of month
 * @param fstate 文件状态字, flash清空后全为1, 因此多个状态字合用需要使用&操作符
 * */
BOOL make_finfo(FileInfo *finfo, uint32_t year, uint8_t month, uint8_t day, uint8_t fstate) {
    FileStatePack fspack;
    if(finfo == NULL) {
        return FALSE;
    }
    fspack.data = fstate;
    finfo->state = fspack.fstate;
    finfo->day = day;
    finfo->month = month;
    finfo->year = (year - 2000);
    return TRUE;
}

/**
 * 创建文件块
 * @param *file 文件指针
 * @param filename 文件名称 最大8字符
 * @param extname 文件拓展名 最大4字符
 * */
BOOL make_file(File *file, char *filename, char *extname) {
    if(file == NULL) {
        return FALSE;
    }
    if((filename == NULL) || (extname == NULL)) {
        return FALSE;
    }
    if((os_strlen(filename) > FILENAME_SIZE) || (os_strlen(extname) > EXTNAME_SIZE)) {
        return FALSE;
    }
    os_memset(file, EMPTY_BYTE_VALUE, sizeof(File));
    os_memcpy(file->filename, filename, os_strlen(filename));
    os_memcpy(file->extname, extname, os_strlen(extname));
    return TRUE;
}

/**
 * @brief 创建文件
 * @brief 写文件块记录扇区,空间不足时执行垃圾回收
 * @param *file 文件指针
 * @param *finfo 文件信息字段
 * @return Result
 * */
Result create_file(File *file, FileInfo *finfo) {
    FileBlock *fb = NULL;
    uint32_t fb_index, addr_start, addr_end;
    BOOL find_empty_sector = FALSE, twice_gc = FALSE;
    uint8_t *fb_buffer = (uint8_t *)os_malloc(sizeof(uint8_t) * FILEBLOCK_SIZE);

    FIND_FB_SPACE:
    for(fb_index = FB_SECTOR_START; (fb_index < (FB_SECTOR_END + 1)) && (!find_empty_sector); fb_index++) {
        addr_start = fb_index * SECTOR_SIZE;
        addr_end = (addr_start + SECTOR_SIZE - 1);
        while((addr_end - addr_start) >= FILEBLOCK_SIZE) {
            spi_flash_read(addr_start, (uint32_t *)fb_buffer, FILEBLOCK_SIZE);
            // check filename and extname
            if(!fb_has_name(fb_buffer)) {
                find_empty_sector = TRUE;
                break;
            }
            addr_start += FILEBLOCK_SIZE;
        }
    }
    // run gc
    if(!find_empty_sector) {
        if(twice_gc) return NO_FILEBLOCK_SPACE;
        twice_gc = TRUE;
        spifs_gc();
        goto FIND_FB_SPACE;
    }
    // clear fileblock buffer
    os_memset(fb_buffer, EMPTY_BYTE_VALUE, FILEBLOCK_SIZE);
    // FileBlock已四字节对齐
    fb = (FileBlock *)fb_buffer;
    os_memcpy(fb->filename, file->filename, strlen_ext(file->filename, FILENAME));
    os_memcpy(fb->extname, file->extname, strlen_ext(file->extname, EXTNAME));
    os_memcpy(&(fb->info), finfo, sizeof(FileInfo));
    // 适配非空File创建，适用于重命名功能
    if(file->cluster != EMPTY_INT_VALUE && file->length != EMPTY_INT_VALUE) {
        os_memcpy(&(fb->cluster), &(file->cluster), sizeof(uint32_t));
        os_memcpy(&(fb->length), &(file->length), sizeof(uint32_t));
    }else {
        file->cluster = fb->cluster;
        file->length = fb->length;
    }
    // write to flash
    write_fileblock(addr_start, fb);
    // copy FileBlock address to File
    file->block = addr_start;

    os_free(fb_buffer);
    return CREATE_FILEBLOCK_SUCCESS;
}

/**
 * @brief 覆盖写文件
 * @brief 无数据文件:查找空扇区写入数据,更新文件块记录
 * @brief 存在数据文件:擦除旧文件,新建文件块记录,(原有的标记失效)查找空扇区写入数据,
 * @param *file 文件指针
 * @param *buffer 写入数据缓冲区
 * @param size 写入字节数
 * @param method 写入方式
 * */
Result write_file(File *file, uint8_t *buffer, uint32_t length, WriteMethod method) {
    FileInfo finfo;
    uint32_t offset = 0, i = 0, write_addr;
    uint32_t align, temp;
    uint32_t *sector_list, sectors;
    uint32_t leftsize = 0, write_size;

    if(file->block == EMPTY_INT_VALUE) return FILE_UNALLOCATED;

    // 文件存在数据则标记数据扇区
    if(method == OVERRIDE && file->cluster != EMPTY_INT_VALUE) {
        // 根据链表标记文件占用扇区废弃
        while(file->cluster != EMPTY_INT_VALUE) {
            update_sector_mark(file->cluster, SECTOR_DISCARD_FLAG);
            spi_flash_read((file->cluster + DATA_AREA_SIZE + SECTOR_MARK_SIZE), &temp, sizeof(uint32_t));
            file->cluster = temp;
        }
        // TODO fix
        read_finfo(file, &finfo);
        // 标记文件索引表对应文件块失效，但不执行擦除操作
        write_fileblock_state(file->block, FSTATE_DEPRECATE);
        // 重新创建文件索引块
        if(CREATE_FILEBLOCK_SUCCESS != create_file(file, &finfo)) {
            return NO_FILEBLOCK_SPACE;
        }
    }else {
        if(file->cluster != EMPTY_INT_VALUE) {
            write_addr = file->cluster;
            while(write_addr != EMPTY_INT_VALUE) {
                spi_flash_read((write_addr + DATA_AREA_SIZE + SECTOR_MARK_SIZE), &temp, sizeof(uint32_t));
                if(temp == EMPTY_INT_VALUE) {
                    break;
                }
                write_addr = temp;
            }
            // 追加写
            temp = ((file->length == EMPTY_INT_VALUE) ? 0 : file->length) % DATA_AREA_SIZE;
            leftsize = DATA_AREA_SIZE - temp;
            write_addr += (SECTOR_MARK_SIZE + temp);
            write_size = (length < leftsize) ? length : leftsize;

            if((align = (write_size % sizeof(uint32_t))) == 0) {
                // 已经4字节对齐
                spi_flash_write(write_addr, (uint32_t *)(buffer + offset), write_size);
            }else {
                // 将对齐部分写入
                if(write_size > align) {
                    spi_flash_write(write_addr, (uint32_t *)(buffer + offset), (write_size - align));
                    offset += (write_size - align);
                }
                // 填充剩余不足四字节部分
                temp = EMPTY_INT_VALUE;
                memcpy(&temp, (buffer + offset), align);
                spi_flash_write((write_addr + (write_size - align)), &temp, sizeof(uint32_t));
            }

            length -= write_size;
            offset += write_size;
            write_addr += write_size;
            file->length += write_size;

            if(length <= 0) {
                return APPEND_FILE_SUCCESS;
            }
        }
    }

    // 计算buffer下数据需要占用的扇区数
    sectors = length / DATA_AREA_SIZE;
    if((length % DATA_AREA_SIZE) != 0) {
        sectors += 1;
    }
    // 扇区首地址记录表
    sector_list = (uint32_t *)os_malloc(sizeof(uint32_t) * sectors);
    // 查找空闲扇区
    if(!find_empty_sector(sector_list, sectors)) {
        return NO_SECTOR_SPACE;
    }

    // 更新文件索引信息
    if(method == OVERRIDE) {
        write_fileblock_cluster(file->block, *(sector_list + 0));
        write_fileblock_length(file->block, length);
        file->cluster = *(sector_list + 0);
        file->length = length;
    }else {
        if(file->cluster == EMPTY_INT_VALUE) {
            write_fileblock_cluster(file->block, *(sector_list + 0));
            file->cluster = *(sector_list + 0);
            file->length = length;
        }else {
            spi_flash_write(write_addr, (sector_list + 0), sizeof(uint32_t));
            file->length += length;
        }
    }

    for(i = 0; i < sectors; i++) {
        // 写入地址偏移4字节
        write_addr = *(sector_list + i) + SECTOR_MARK_SIZE;
        // 写占用标记
        update_sector_mark(*(sector_list + i), SECTOR_INUSE_FLAG);
        if(length >= DATA_AREA_SIZE) {
            // DATA_AREA_SIZE = 4088 已对齐到4字节
            spi_flash_write(write_addr, (uint32_t *)(buffer + offset), DATA_AREA_SIZE);
            if((i + 1) < sectors) {
                // length == DATA_AREA_SIZE时不必写入
                spi_flash_write((write_addr + DATA_AREA_SIZE), (sector_list + i + 1), sizeof(uint32_t));
            }
            offset += DATA_AREA_SIZE;
            length -= DATA_AREA_SIZE;
        }else {
            if((align = (length % sizeof(uint32_t))) == 0) {
                // 剩余字节已经4字节对齐
                spi_flash_write(write_addr, (uint32_t *)(buffer + offset), length);
            }else {
                // 将对齐部分写入
                if(length > align) {
                    spi_flash_write(write_addr, (uint32_t *)(buffer + offset), (length - align));
                    offset += (length - align);
                }
                // 填充剩余不足四字节部分
                temp = EMPTY_INT_VALUE;
                memcpy(&temp, (buffer + offset), align);
                spi_flash_write((write_addr + (length - align)), &temp, sizeof(uint32_t));
            }
        }
    }
    os_free(sector_list);
    return ((method == OVERRIDE) ? WRITE_FILE_SUCCESS : APPEND_FILE_SUCCESS);
}
/**
 * @brief 通过追加写方式的文件结束时需要调用，以更新文件索引块的文件大小
 * @param *file 文件指针
 * */
Result write_finish(File *file) {
    if(file->block == EMPTY_INT_VALUE) return FILE_UNALLOCATED;
    write_fileblock_length(file->block, file->length);
    return APPEND_FILE_FINISH;
}

/**
 * @brief 读取文件
 * @param *file 文件指针
 * @param offset 偏移量以0为基准
 * @param *buffer 存储数据缓冲区
 * @param length 读出字节数
 * */
BOOL read_file(File *file, uint32_t offset, uint8_t *buffer, uint32_t length) {
    uint32_t addr_start = file->cluster, cursor = 0;
    uint32_t sectors = (offset / DATA_AREA_SIZE);
    uint32_t align, leftsize, temp;
    // 边界检查
    if((offset >= file->length) || ((file->length - offset) < length)) {
        return FALSE;
    }
    // 跳过偏移扇区
    for(uint32_t i = 0; i < sectors; i++) {
        spi_flash_read((addr_start + SECTOR_MARK_SIZE + DATA_AREA_SIZE), &temp, sizeof(uint32_t));
        addr_start = temp;
        offset -= DATA_AREA_SIZE;
    }
    // 移至当前扇区偏移地址
    addr_start += (SECTOR_MARK_SIZE + offset);
    leftsize = (DATA_AREA_SIZE - offset);

    while(length > 0) {
        if(length >= leftsize) {
            if((align = (leftsize % sizeof(uint32_t))) == 0) {
                // 剩余空间(leftsize)4字节对齐
                spi_flash_read(addr_start, (uint32_t *)(buffer + cursor), leftsize);
                length -= leftsize;
                cursor += leftsize;
            }else {
                temp = (leftsize - align);
                if(temp > 0) {
                    // 读对齐部分
                    spi_flash_read(addr_start, (uint32_t *)(buffer + cursor), temp);
                    cursor += temp;
                    addr_start += temp;
                }
                // 读取非对齐部分
                spi_flash_read(addr_start, &temp, sizeof(uint32_t));
                os_memcpy((buffer + cursor), &temp, align);
                cursor += align;
                addr_start += align;
                length -= leftsize;
            }
            if(length <= 0) {
                break;
            }
            // 读下一扇区首地址
            spi_flash_read(addr_start, &temp, sizeof(uint32_t));
            addr_start = (temp + SECTOR_MARK_SIZE);
            leftsize = DATA_AREA_SIZE;
        }else {
            if((align = (length % sizeof(uint32_t))) == 0) {
                spi_flash_read(addr_start, (uint32_t *)(buffer + cursor), length);
            }else {
                // 读对齐部分
                temp = (length - align);
                if(temp > 0) {
                    spi_flash_read(addr_start, (uint32_t *)(buffer + cursor), temp);
                    cursor += temp;
                    addr_start += temp;
                }
                // 读取非对齐部分
                spi_flash_read(addr_start, &temp, sizeof(uint32_t));
                os_memcpy((buffer + cursor), &temp, align);
            }
            break;
        }
    }
    return TRUE;
}

/**
 * @brief 根据文件名+拓展名打开文件
 * @param file 文件指针
 * @param filename 文件名 没有名字可以传入空字符串 "" 或者 NULL
 * @param extname 拓展名 没有名字可以传入空字符串 "" 或者 NULL
 * @return 0:未找到该文件, 1:成功获取文件
 * */
BOOL open_file(File *file, char *filename, char *extname) {
    FileBlock *fb;
    uint32_t addr_start, addr_end;
    uint8_t *slot_buffer = (uint8_t *)os_malloc(sizeof(uint8_t) * FILEBLOCK_SIZE);
    for(uint32_t i = FB_SECTOR_START; i < (FB_SECTOR_END + 1); i++) {
        addr_start = i * SECTOR_SIZE;
        addr_end = (addr_start + SECTOR_SIZE - 1);
        while((addr_end - addr_start) >= FILEBLOCK_SIZE) {
            spi_flash_read(addr_start, (uint32_t *)slot_buffer, FILEBLOCK_SIZE);
            fb = (FileBlock *)slot_buffer;
            // 忽略标记删除/废弃的文件
            if(fb->info.state.del == 0 || fb->info.state.dep == 0) {
                addr_start += FILEBLOCK_SIZE;
                continue;
            }
            if(fname_equals(fb->extname, extname, EXTNAME) && fname_equals(fb->filename, filename, FILENAME)) {
                file->block = addr_start;
                file->cluster = fb->cluster;
                file->length = fb->length;
                os_memcpy(file->filename, fb->filename, FILENAME_SIZE);
                os_memcpy(file->extname, fb->extname, EXTNAME_SIZE);
                os_free(slot_buffer);
                return TRUE;
            }
            addr_start += FILEBLOCK_SIZE;
        }
    }
    os_free(slot_buffer);
    return FALSE;
}

/**
 * @brief 重命名文件
 * @param *file 文件指针
 * @param *filename 文件名
 * @param *extname 拓展名
 * @return Result 成功: FILE_RENAME_SUCCESS, 其他结果码见Result定义
 */
Result rename_file(File *file, char *filename, char *extname) {
    FileInfo fileinfo;
    uint32_t fnamelen, extnamelen;

    if(file == NULL || file->block == EMPTY_INT_VALUE) {
        return FILE_NOT_EXIST;
    }
    // 读出文件状态字
    read_finfo(file, &fileinfo);
    // 文件状态检查
    if(fileinfo.state.del & fileinfo.state.dep & fileinfo.state.rw & fileinfo.state.sys) {
        fnamelen = os_strlen(filename);
        extnamelen = os_strlen(extname);
        // 输入文件名长度检查
        if(fnamelen > FILENAME_SIZE || extnamelen > EXTNAME_SIZE) {
            return FILENAME_INCORRECT;
        }
        if(spifs_avail_files() > 0) {
            // 标记文件索引表原始文件对应文件块失效，但不执行擦除操作
            write_fileblock_state(file->block, FSTATE_DEPRECATE);
            // 复制文件名到file
            os_memcpy(file->filename, filename, fnamelen);
            os_memcpy(file->extname, extname, extnamelen);
            // 填充末尾
            os_memset((file->filename + fnamelen), EMPTY_BYTE_VALUE, (FILENAME_SIZE - fnamelen));
            os_memset((file->extname + extnamelen), EMPTY_BYTE_VALUE, (EXTNAME_SIZE - extnamelen));
            // 重新创建文件索引块
            return (CREATE_FILEBLOCK_SUCCESS == create_file(file, &fileinfo)) ? FILE_RENAME_SUCCESS : NO_FILEBLOCK_SPACE;
        }
        return NO_FILEBLOCK_SPACE;
    }
    return FILE_CANNOT_RENAME;
}

/**
 * @brief 返回文件列表, 以链表形式存储, 使用完毕务必调用recycle_filelist()释放文件
 * @return FileList * 文件列表链表
 * */
FileList *list_file() {
    FileBlock *fb;
    FileList *index = NULL;
    uint32_t addr_start, addr_end;
    uint8_t *cache = (uint8_t *)os_malloc(sizeof(uint8_t) * FILEBLOCK_SIZE);

    for(uint32_t i = FB_SECTOR_START; i < (FB_SECTOR_END + 1); i++) {
        addr_start = i * SECTOR_SIZE;
        addr_end = addr_start + SECTOR_SIZE;
        while(addr_end - addr_start >= FILEBLOCK_SIZE) {
            spi_flash_read(addr_start, (uint32_t *)cache, FILEBLOCK_SIZE);
            fb = (FileBlock *)cache;
            if((fb->info.state.del != 0) && (fb->info.state.dep != 0) && (fb->cluster != EMPTY_INT_VALUE)) {
                FileList *item = (FileList *)os_malloc(sizeof(FileList));
                os_memcpy(item->file.filename, fb->filename, FILENAME_SIZE);
                os_memcpy(item->file.extname, fb->extname, EXTNAME_SIZE);
                item->file.block = addr_start;
                item->file.cluster = fb->cluster;
                item->file.length = fb->length;
                item->prev = index;
                index = item;
            }
            addr_start += FILEBLOCK_SIZE;
        }
    }
    os_free(cache);
    return index;
}

/**
 * @brief 释放文件列表内存
 * @param *list 文件列表指针
 * */
void recycle_filelist(FileList *list) {
    FileList *ptr = NULL;
    while(list != NULL) {
        ptr = list->prev;
        os_free(list);
        list = ptr;
    }
}

/**
 * @brief 读取文件信息
 * @param *file 文件结构指针
 * @param *finfo 存放文件信息指针
 * @return FALSE: file=null 或finfo = null, TRUE: 读取成功
 */
BOOL read_finfo(File *file, FileInfo *finfo) {
    FileBlock *fb;
    uint8_t *slot_buffer;
    if(file == NULL || finfo == NULL) {
        return FALSE;
    }
    slot_buffer = (uint8_t *)os_malloc(sizeof(uint8_t) * sizeof(FileBlock));
    spi_flash_read(file->block, (uint32_t *)slot_buffer, sizeof(FileBlock));
    fb = (FileBlock *)slot_buffer;
    os_memcpy(finfo, &(fb->info), sizeof(FileInfo));
    os_free(slot_buffer);
    return TRUE;
}

/**
 * @brief 查找数据区空闲扇区
 * @parame *secList 存放空闲扇区首地址缓冲区
 * @parame nums 需要查找的扇区数量
 * @return TRUE:成功找到nums个空扇区, FALSE: 空扇区不足
 * */
static BOOL find_empty_sector(uint32_t *secList, uint32_t nums) {
    uint32_t sector_index, sector_mark, i = 0;
    for(sector_index = DATA_SECTOR_START; (i < nums) && (sector_index < (DATA_SECTOR_END + 1)); sector_index++) {
        spi_flash_read((sector_index * SECTOR_SIZE), &sector_mark, sizeof(uint32_t));
        if(sector_mark == EMPTY_INT_VALUE) {
            *(secList + i) = (sector_index * SECTOR_SIZE);
            i++;
        }
    }
    return (i == nums);
}

/**
 * 删除文件, 此操作不会立即擦除扇区
 * 而将文件状态字标注为被删除,仅在垃圾回收时才会擦除扇区数据
 * @param *file 文件指针
 * */
void delete_file(File *file) {
    write_fileblock_state(file->block, FSTATE_DELETE);
}

/**
 * @brief spifs垃圾回收
 * @brief 应用层的删除文件操作并不会从闪存中擦除文件数据
 * @brief 而是标记其文件块的状态属性为可删除文件
 * @brief 当空间不足时才进行全盘扫描, 删除标记的文件数据
 * */
void spifs_gc() {
    FileBlock *fb = NULL;
    BOOL rewrite = FALSE;
    uint32_t offset = 0, fb_index, addr_cluster;

    uint8_t *slot_buffer = (uint8_t *)os_malloc(sizeof(uint8_t) * FILEBLOCK_SIZE);
    uint8_t *sector_buffer = (uint8_t *)os_malloc(sizeof(uint8_t) * SECTOR_SIZE);
    // 扫描文件索引表查找被标记文件
    for(fb_index = FB_SECTOR_START; fb_index < (FB_SECTOR_END + 1); fb_index++) {
        offset = 0;
        spi_flash_read((fb_index * SECTOR_SIZE), (uint32_t *)sector_buffer, SECTOR_SIZE);
        while((SECTOR_SIZE - offset) >= FILEBLOCK_SIZE) {
            os_memcpy(slot_buffer, (sector_buffer + offset), FILEBLOCK_SIZE);
            if(!fb_has_name(slot_buffer)) {
                offset += FILEBLOCK_SIZE;
                continue;
            }
            fb = (FileBlock *)slot_buffer;
            // 文件被标识为删除
            if((fb->info.state.del) == 0) {
                // 根据链表擦除文件占用扇区
                while(fb->cluster != EMPTY_INT_VALUE) {
                    spi_flash_read((fb->cluster + DATA_AREA_SIZE + SECTOR_MARK_SIZE), &addr_cluster, sizeof(uint32_t));
                    spi_flash_erase_sector(fb->cluster / SECTOR_SIZE);
                    fb->cluster = addr_cluster;
                }
                // 清除文件索引信息
                clear_fileblock(sector_buffer, offset);
                rewrite = TRUE;
            }
            // 文件被标记为失效
            if((fb->info.state.dep) == 0) {
                // 清除文件索引信息
                clear_fileblock(sector_buffer, offset);
                rewrite = TRUE;
            }
            // 创建文件但未填充数据, 空文件索引
            if(fb->cluster == EMPTY_INT_VALUE) {
                // 清除文件索引信息
                clear_fileblock(sector_buffer, offset);
                rewrite = TRUE;
            }
            offset += FILEBLOCK_SIZE;
        }
        // 擦除文件索引扇区，回写新文件索引表
        if(rewrite) {
            rewrite = FALSE;
            spi_flash_erase_sector(fb_index);
            spi_flash_write(fb_index * SECTOR_SIZE, (uint32_t *)sector_buffer, SECTOR_SIZE);
        }
    }
    os_free(sector_buffer);
    os_free(slot_buffer);
    // 扫描数据扇区,查找标记为废弃扇区
    // fb_index复用做扇区首地址, offset 复用做扇区标识符
    for(fb_index = DATA_SECTOR_START; fb_index < (DATA_SECTOR_END + 1); fb_index++) {
        spi_flash_read(fb_index * SECTOR_SIZE, &offset, sizeof(uint32_t));
        // LSB      MSB
        // FF FF FF FF 空扇区不做处理
        // 00 FF 00 FF 有效数据扇区不做处理
        // 00 CC 00 FF 标记为废弃扇区需擦除
        if(offset == SECTOR_DISCARD_FLAG) {
            spi_flash_erase_sector(fb_index);
        }
    }
}

/**
 * @brief 文件系统格式化
 * @brief 仅擦除文件索引块区/数据区扇区，擦除完成后为0xFF
 * */
void spifs_format() {
	uint32_t sector;
	// 擦除文件索引块扇区
	for(sector = FB_SECTOR_START; sector < (FB_SECTOR_END + 1); sector++) {
		spi_flash_erase_sector(sector);
	}
	// 擦除数据区扇区
	for(sector = DATA_SECTOR_START; sector < (DATA_SECTOR_END + 1); sector++) {
		spi_flash_erase_sector(sector);
	}
}

/**
 * @brief 查询flash数据区可用扇区数量
 * */
uint32_t spifs_avail() {
    uint32_t i, temp, avail = 0;
    for(i = DATA_SECTOR_START; i < (DATA_SECTOR_END + 1); i++) {
        spi_flash_read(i * SECTOR_SIZE, &temp, sizeof(uint32_t));
        if(temp == EMPTY_INT_VALUE) {
            avail++;
        }
    }
    return avail;
}

/**
 * @brief 查询flash文件索引区还能创建的文件数量
 * */
uint32_t spifs_avail_files() {
    uint32_t fb_index, addr_start, addr_end, avail = 0;
    uint8_t *fb_buffer = (uint8_t *)os_malloc(sizeof(uint8_t) * FILEBLOCK_SIZE);

    for(fb_index = FB_SECTOR_START; fb_index < (FB_SECTOR_END + 1); fb_index++) {
        addr_start = fb_index * SECTOR_SIZE;
        addr_end = (addr_start + SECTOR_SIZE - 1);
        while((addr_end - addr_start) >= FILEBLOCK_SIZE) {
            spi_flash_read(addr_start, (uint32_t *)fb_buffer, FILEBLOCK_SIZE);
            if(!fb_has_name(fb_buffer)) {
                avail++;
            }
            addr_start += FILEBLOCK_SIZE;
        }
    }
    os_free(fb_buffer);
    return avail;
}

/**
 * @brief 获取文件系统版本
 * @return uint16 低字节子版本号,高字节主版本号
 */
uint16_t spifs_get_version() {
    return ((MAJOR_VERSION << 8) | MINOR_VERSION);
}

/**
 * @brief 字符串长度计算，以0xFF为结束符，限制长度
 * @param *str 文件名字符串
 * @param type 文件名类型: FILENAME:文件名, EXTNAME:拓展名
 * @return 文本实际长度
 * */
static uint32_t strlen_ext(uint8_t *str, FileNameType type) {
    uint32_t max = ((type == FILENAME) ? FILENAME_SIZE : EXTNAME_SIZE);
    uint32_t i = 0;
    for(; ((i < max) && (*str != EMPTY_BYTE_VALUE)); i++, str++);
    return i;
}

/**
 * @brief 比较文件索引块文件名与输入文件名
 * @param *fsname 文件块的文件名,以0xFF结尾
 * @param *inname 输入的文件名,以\0结尾, 没有名字可以传入空字符串 "" 或者 NULL
 * @param type 文件名类型: FILENAME:文件名, EXTNAME:拓展名
 * @return TRUE:文件名相同, FALSE:文件名不同
 * */
static BOOL fname_equals(uint8_t *fsname, char *inname, FileNameType type) {
    uint32_t i = 0, limit = ((type == FILENAME) ? FILENAME_SIZE : EXTNAME_SIZE);
    uint32_t fsname_length = strlen_ext(fsname, type);
    uint32_t inname_length = 0;
    // NULL 检查
    if(inname != NULL) {
    	inname_length = os_strlen(inname);
    }
    if((inname_length > limit) || (fsname_length != inname_length)) {
        return FALSE;
    }
    for(i = 0; i < fsname_length; i++) {
        if(*(fsname + i) != *(inname + i)) return FALSE;
    }
    return TRUE;
}

/**
 * @brief 判断文件索引是否含有文件名/拓展名
 * @param *fb_buffer 文件索引块, FileBlock *可强制转换成uint8_t *
 * @return TRUE: 含有文件名, FALSE: 文件索引不含有文件名
 */
static BOOL fb_has_name(uint8_t *fb_buffer) {
    for(uint32_t i = 0; i < FILENAME_FULLSIZE; i++) {
        if(*(fb_buffer + i) != EMPTY_BYTE_VALUE) return TRUE;
    }
    return FALSE;
}
