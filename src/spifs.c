#include "spifs.h"

static uint32_t ICACHE_FLASH_ATTR strlen_ext(uint8_t *str, uint32_t max) ;

static BOOL ICACHE_FLASH_ATTR fb_has_name(uint8_t *fb_buffer);

static BOOL ICACHE_FLASH_ATTR find_empty_sector(uint32_t *secList, uint32_t nums);

static BOOL ICACHE_FLASH_ATTR filename_equals(uint8_t *src, uint8_t *target, uint32_t length);

static BOOL ICACHE_FLASH_ATTR open_file_impl(File *file, uint8_t *filename, uint8_t *extname, BOOL rawname);

static Result ICACHE_FLASH_ATTR rename_file_impl(File *file, uint8_t *filename, uint8_t *extname, BOOL raw);

static void ICACHE_FLASH_ATTR align_write_impl(uint8_t *buffer, uint32_t offset, uint32_t write_addr, uint32_t write_size);

static void ICACHE_FLASH_ATTR align_read_impl(uint8_t *buffer, uint32_t offset, uint32_t read_addr, uint32_t read_size);

/**
 * @brief 配置文件信息字段
 * @param *finfo 信息字段指针
 * @param year 文件创建年份(2000~2255),以2000年为起点
 * @param month 文件创建月份(1~12)
 * @param day 文件创建的日期(1~N),N: (day of month)
 * @param fstate 文件状态字, flash清空后全为1, 因此标记位'0'有效, 多个状态字合用需要使用&操作符
 * @return FALSE:失败 TRUE:成功
 * */
BOOL ICACHE_FLASH_ATTR make_finfo(FileInfo *finfo, uint32_t year, uint8_t month, uint8_t day, uint8_t fstate) {
    FileStatePack fspack;

#ifdef SPIFS_USE_NULL_CHECK
    if(finfo == NULL) return FALSE;
#endif

    if((year < YEAR_MINI_VALUE || year > YEAR_MAX_VALUE)
    	|| (month < MONTH_MINI_VALUE || month > MONTH_MAX_VALUE)
        || (day < DAY_MINI_VALUE || day > DAY_MAX_VALUE)) {
    	return FALSE;
    }
    fspack.data = fstate;
    finfo->state = fspack.fstate;
    finfo->day = day;
    finfo->month = month;
    finfo->year = (year - YEAR_MINI_VALUE);
    return TRUE;
}

/**
 * @brief 初始化文件, 对输入的文件名拓展名检查后配置
 * @param *file 文件指针
 * @param filename 文件名称 最大8字符
 * @param extname 文件拓展名 最大4字符
 * @return FALSE:失败 TRUE:成功
 * */
BOOL ICACHE_FLASH_ATTR make_file(File *file, char *filename, char *extname) {
    uint32_t fname_length, extname_length;

#ifdef SPIFS_USE_NULL_CHECK
    if((file == NULL) || (filename == NULL) || (extname == NULL)) {
        return FALSE;
    }
#endif

    fname_length = os_strlen(filename);
    extname_length = os_strlen(extname);

    if((fname_length > FILENAME_SIZE) || (extname_length > EXTNAME_SIZE)) return FALSE;

    os_memset(file, EMPTY_BYTE_VALUE, sizeof(File));
    os_memcpy(file->filename, filename, fname_length);
    os_memcpy(file->extname, extname, extname_length);

    return TRUE;
}

/**
 * @brief 创建文件
 * @brief 写文件块记录扇区,空间不足时执行垃圾回收
 * @param *file 文件指针
 * @param *finfo 文件信息字段
 * @return Result
 * */
Result ICACHE_FLASH_ATTR create_file(File *file, FileInfo *finfo) {
    File temp_file;
    FileBlock *fb = NULL;
    // stack allocated aligned with 4 bytes
    uint8_t fb_buffer[FILEBLOCK_SIZE];
    uint32_t fb_index, addr_start, addr_end;
    BOOL find_empty_sector = FALSE;

#ifdef SPIFS_USE_NULL_CHECK
    if(file == NULL || finfo == NULL) {
        return FILE_NOT_EXIST;
    }
#endif

    // 检查该文件名/拓展名的文件是否已经存在
    if(open_file_impl(&temp_file, file->filename, file->extname, TRUE)) {
        // 同名文件已经存在
        return FILE_ALREADY_EXIST;
    }

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
        if(spifs_gc(GC_TYPE_FILEBLOCK, 1) >= 1) {
        	goto FIND_FB_SPACE;
        }else {
        	return NO_FILEBLOCK_SPACE;
        }
    }
    // clear fileblock buffer
    os_memset(fb_buffer, EMPTY_BYTE_VALUE, FILEBLOCK_SIZE);
    // FileBlock已四字节对齐
    fb = (FileBlock *)fb_buffer;
    os_memcpy(fb->filename, file->filename, FILENAME_SIZE);
    os_memcpy(fb->extname, file->extname, EXTNAME_SIZE);
    os_memcpy(&(fb->info), finfo, sizeof(FileInfo));
    // 适配非空File创建，适用于重命名功能
    if((file->cluster & file->length) != EMPTY_INT_VALUE) {
        os_memcpy(&(fb->cluster), &(file->cluster), sizeof(uint32_t));
        os_memcpy(&(fb->length), &(file->length), sizeof(uint32_t));
    }else {
        file->cluster = fb->cluster;
        file->length = fb->length;
    }
    write_fileblock(addr_start, fb);
    file->block = addr_start;
    // 成功
    return CREATE_FILE_SUCCESS;
}

/**
 * @brief 写文件
 * @brief override 无数据文件:查找空扇区写入数据,更新文件块记录
 * @brief override 存在数据文件:擦除旧文件,新建文件块记录,(原有的标记失效)查找空扇区写入数据,
 * @param *file 文件指针
 * @param *buffer 写入数据缓冲区，起始地址四字节对齐
 * @param size 写入字节数
 * @param method 写入方式
 * @return Result
 * */
Result ICACHE_FLASH_ATTR write_file(File *file, uint8_t *buffer, uint32_t length, WriteMethod method) {
    FileInfo finfo;
    uint32_t offset = 0, i = 0, write_addr = 0;
    uint32_t *sector_list, sectors;
    uint32_t leftsize = 0, write_size, temp;
#ifdef SPIFS_USE_NULL_CHECK
    // 空指针检查
    if(file == NULL || buffer == NULL || file->block == EMPTY_INT_VALUE) {
        return FILE_NOT_EXIST;
    }
#endif

    read_finfo(file, &finfo);
    // 权限检查
    if(!(finfo.state.del & finfo.state.dep & finfo.state.rw)) {
        return CANNOT_WRITE_FILE;
    }
    // 文件存在数据则标记数据扇区
    if(method == OVERRIDE && (file->cluster != EMPTY_INT_VALUE)) {
        // 根据链表标记文件占用扇区废弃
        while(file->cluster != EMPTY_INT_VALUE) {
            update_sector_mark(file->cluster, SECTOR_DISCARD_FLAG);
            spi_flash_read((file->cluster + DATA_AREA_SIZE + SECTOR_MARK_SIZE), &temp, sizeof(uint32_t));
            file->cluster = temp;
        }
        file->length = EMPTY_INT_VALUE;
        // 标记文件索引表对应文件块失效，但不执行擦除操作
        write_fileblock_state(file->block, FSTATE_DEPRECATE);
        // 重新创建文件索引块
        if(CREATE_FILE_SUCCESS != create_file(file, &finfo)) {
            return NO_FILEBLOCK_SPACE;
        }
    }else if(method == APPEND && (file->cluster != EMPTY_INT_VALUE) && (file->length != EMPTY_INT_VALUE)) {
    	// 遍历扇区链表，找到最后一个扇区
		write_addr = file->cluster;
		while(write_addr != EMPTY_INT_VALUE) {
			spi_flash_read((write_addr + SECTOR_MARK_SIZE + DATA_AREA_SIZE), &temp, sizeof(uint32_t));
			if(temp == EMPTY_INT_VALUE) {
				break;
			}
			write_addr = temp;
		}
    	// 判断当前扇区使用空间
		if((temp = (file->length % DATA_AREA_SIZE)) == 0) {
			write_addr += (SECTOR_MARK_SIZE + DATA_AREA_SIZE);
			goto NEXT_PART_WRITE;
		}
		// 当前扇区还剩空间，追加写
		leftsize = (DATA_AREA_SIZE - temp);
		write_addr += (SECTOR_MARK_SIZE + temp);
		write_size = (length < leftsize) ? length : leftsize;
		align_write_impl(buffer, offset, write_addr, write_size);
		// 地址更新
		offset += write_size;
		write_addr += write_size;
		length -= write_size;
		file->length += write_size;
		if(length <= 0) {
			return APPEND_FILE_SUCCESS;
		}
    }

NEXT_PART_WRITE:
    // 计算buffer下数据需要占用的扇区数
    sectors = (length / DATA_AREA_SIZE);
    if((sectors * DATA_AREA_SIZE) < length) {
    	sectors += 1;
    }
    // 扇区首地址记录表
    sector_list = (uint32_t *)os_malloc(sizeof(uint32_t) * sectors);

    // 查找空闲扇区
    do {
    	if(find_empty_sector(sector_list, sectors)) {
    		break;
    	}
    	if(spifs_gc(GC_TYPE_DATAAREA, sectors) >= sectors) {
    		find_empty_sector(sector_list, sectors);
    		break;
    	}
    	return NO_SECTOR_SPACE;
    }while(0);

    // 更新文件索引信息
    if(method == OVERRIDE) {
        write_fileblock_cluster(file->block, *(sector_list + 0));
        write_fileblock_length(file->block, length);
        file->cluster = *(sector_list + 0);
        file->length = length;
    }else {
        if(file->cluster == EMPTY_INT_VALUE) {
        	// 对空文件追加, 仅写入首簇号
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
        write_addr = (*(sector_list + i) + SECTOR_MARK_SIZE);
        // 写占用标记
        update_sector_mark(*(sector_list + i), SECTOR_INUSE_FLAG);

        write_size = (length >= DATA_AREA_SIZE) ? DATA_AREA_SIZE : length;
        align_write_impl(buffer, offset, write_addr, write_size);

        if((write_size >= DATA_AREA_SIZE) && ((i + 1) < sectors)) {
        	// 除了最后一个扇区，其余扇区都需要在最后四字节写入下一扇区首地址，形成单链表
			spi_flash_write((write_addr + DATA_AREA_SIZE), (sector_list + i + 1), sizeof(uint32_t));
        }
        offset += write_size;
    }

    os_free(sector_list);

    return ((method == OVERRIDE) ? WRITE_FILE_SUCCESS : APPEND_FILE_SUCCESS);
}

/**
 * @param *buffer 可由malloc或者静态分配
 * @param offset buffer中的读取偏移量(读出buffer->写入)
 * @param write_addr 写入flash的地址，随机地址
 * @param write_size 写入flash的数据长度
 */
static void align_write_impl(uint8_t *buffer, uint32_t offset, uint32_t write_addr, uint32_t write_size) {
    size_t addr_align;
    uint32_t data_align, temp, towrite;

	// (buffer + offset)对齐处理，判断写入地址是否在平台指针边界
    addr_align = ((size_t)(buffer + offset)) & (sizeof(size_t) - 1);
	if(addr_align != 0) {
		// 当前(buffer + offset)不对齐，写出不对齐部分，随后(buffer + offset)对齐到4字节边界
		temp = EMPTY_INT_VALUE;
		towrite = ((sizeof(size_t) - addr_align) < write_size) ? (sizeof(size_t) - addr_align) : write_size;
        os_memcpy(&temp, (buffer + offset), towrite);
		spi_flash_write(write_addr, &temp, sizeof(uint32_t));

		write_addr += towrite;
		offset += towrite;
		write_size -= towrite;
	}

    if(write_size == 0) {
        return;
    }
	// 写入地址+写入大小是否在4字节边界
	// 由于(uint32_t)(buffer + offset)已判断过对齐，此处仅需判断write_size是否对齐
	if((data_align = (write_size % sizeof(uint32_t))) == 0) {
		// (buffer + offset)对齐，且write_size对齐，直接写入
		spi_flash_write(write_addr, (uint32_t *)(buffer + offset), write_size);
	}else {
		// (buffer + offset)对齐，但write_size不对齐
		// 将对齐部分写入
		if(write_size > data_align) {
			temp = (write_size - data_align);
			spi_flash_write(write_addr, (uint32_t *)(buffer + offset), temp);
			offset += temp;
			write_addr += temp;
		}
		// 填充剩余不足四字节部分
		temp = EMPTY_INT_VALUE;
		os_memcpy(&temp, (buffer + offset), data_align);
		spi_flash_write(write_addr, &temp, sizeof(uint32_t));
	}
}

/**
 * @brief 通过追加写方式的文件结束时需要调用，以更新文件索引块的文件大小
 * @param *file 文件指针
 * @return Result
 * */
Result ICACHE_FLASH_ATTR write_finish(File *file) {
    FileBlock fblock;
    FileInfo finfo;
    Result result;
#ifdef SPIFS_USE_NULL_CHECK
    if(file == NULL || file->block == EMPTY_INT_VALUE) {
        return FILE_NOT_EXIST;
    }
#endif
    // 读取原始文件索引块
    spi_flash_read(file->block, (uint32_t *)&fblock, sizeof(fblock));
    if(fblock.length == EMPTY_INT_VALUE) {
        // 文件大小信息为空，直接写入文件大小信息
        write_fileblock_length(file->block, file->length);
        return APPEND_FILE_FINISH;
    }
    //读取文件属性
    read_finfo(file, &finfo);
    // 标记旧的文件索引块失效，但不执行擦除操作
    write_fileblock_state(file->block, FSTATE_DEPRECATE);
    // 重新创建文件索引块
    result = create_file(file, &finfo);
    return (result == CREATE_FILE_SUCCESS) ? APPEND_FILE_FINISH : result;
}

/**
 * @brief 读取文件
 * @param *file 文件指针
 * @param offset 文件偏移量, 以0为基准
 * @param *buffer 存储数据缓冲区，需要4字节对齐
 * @param length 读出字节数
 * @return length 实际读取的大小(bytes),正确返回时该值大于0
 * */
uint32_t ICACHE_FLASH_ATTR read_file(File *file, uint32_t offset, uint8_t *buffer, uint32_t length) {
    FileInfo finfo;
    uint32_t addr_start = file->cluster, cursor = 0;
    uint32_t sectors = (offset / DATA_AREA_SIZE);
    uint32_t i, read_size, temp;
#ifdef SPIFS_USE_NULL_CHECK
    // 空指针检查
    if(file == NULL || (file->block & file->cluster & file->length) == EMPTY_INT_VALUE) {
        return 0;
    }
#endif
    read_finfo(file, &finfo);
    // 权限检查
    if(!(finfo.state.del & finfo.state.dep)) {
        return 0;
    }
    // 边界检查
    if(offset >= file->length) {
        return 0;
    }

    if((file->length - offset) < length) {
    	length = (file->length - offset);
    }

    // 跳过偏移扇区
    for(i = 0; i < sectors; i++) {
        spi_flash_read((addr_start + SECTOR_MARK_SIZE + DATA_AREA_SIZE), &temp, sizeof(uint32_t));
        addr_start = temp;
        offset -= DATA_AREA_SIZE;
    }
    i = length;
    // 移至当前扇区偏移地址
    addr_start += (SECTOR_MARK_SIZE + offset);
    read_size = (DATA_AREA_SIZE - offset);

    while(length > 0) {
    	if(length > read_size) {
    		align_read_impl(buffer, cursor, addr_start, read_size);
    		addr_start += read_size;
    		cursor += read_size;
    		length -= read_size;

    		spi_flash_read(addr_start, &temp, sizeof(uint32_t));
    		addr_start = (temp + SECTOR_MARK_SIZE);
    		read_size = (length > DATA_AREA_SIZE) ? DATA_AREA_SIZE : (length);
    	}else {
    	    read_size = (length > DATA_AREA_SIZE) ? DATA_AREA_SIZE : (length);
    		align_read_impl(buffer, cursor, addr_start, read_size);
    		length -= read_size;
    	}
    }
    return i;
}

/**
 * @param *buffer 可由malloc或者静态分配
 * @param offset buffer中的写入偏移量(读取->写入buffer)
 * @param write_addr 写入flash的地址，随机地址
 * @param write_size 写入flash的数据长度
 */
static void align_read_impl(uint8_t *buffer, uint32_t offset, uint32_t read_addr, uint32_t read_size) {
    uint32_t data_align, temp, toread;
    size_t addr_align;

	// (buffer + offset)对齐处理，判断读入缓存地址是否在平台指针边界
	addr_align = ((size_t)(buffer + offset)) & (sizeof(size_t) - 1);

	if(addr_align != 0) {
		// 当前(buffer + offset)不对齐，读取不对齐部分填充，随后(buffer + offset)对齐到4字节边界
		spi_flash_read(read_addr, &temp, sizeof(uint32_t));
		toread = ((sizeof(size_t) - addr_align) < read_size) ? (sizeof(size_t) - addr_align) : read_size;
		os_memcpy((buffer + offset), &temp, toread);

		offset += toread;
		read_addr += toread;
		read_size -= toread;
	}

    if(read_size == 0) {
        return;
    }
	// 由于(uint32_t)(buffer + offset)已判断过对齐，此处仅需判断read_size是否4字节对齐
	if((data_align = (read_size % sizeof(uint32_t))) == 0) {
		// (buffer + offset)对齐，且read_size对齐，直接读取
		spi_flash_read(read_addr, (uint32_t *)(buffer + offset), read_size);
	}else {
		// (buffer + offset)对齐，但read_size不对齐
		// 将对齐部分读入
		if(read_size > data_align) {
			temp = (read_size - data_align);
			spi_flash_read(read_addr, (uint32_t *)(buffer + offset), temp);
			offset += temp;
			read_addr += temp;
		}
		// 读取剩余不足四字节部分
		spi_flash_read(read_addr, &temp, sizeof(uint32_t));
		os_memcpy((buffer + offset), &temp, data_align);
	}
}

/**
 * @brief 根据文件名+拓展名打开文件，文件名以'\0'结尾
 * @param file 文件指针
 * @param filename 文件名 没有名字可以传入空字符串 ""
 * @param extname 拓展名 没有名字可以传入空字符串 ""
 * @return 0:未找到该文件, 1:成功获取文件
 * */
BOOL ICACHE_FLASH_ATTR open_file(File *file, char *filename, char *extname) {
    return open_file_impl(file, (uint8_t *)filename, (uint8_t *)extname, FALSE);
}

/**
 * @brief 根据文件名+拓展名打开文件，文件名空缺部分以0xFF填充
 * */
BOOL ICACHE_FLASH_ATTR open_file_raw(File *file, uint8_t *filename, uint8_t *extname) {
    return open_file_impl(file, filename, extname, TRUE);
}

/**
 * @brief 打开文件实现
 * @param *file 文件指针
 * @param filename 文件名
 * @param extname 拓展名
 * @param rawname 原始格式文件名，原始格式空缺填充0xFF
 * @return 0:未找到该文件, 1:成功获取文件
 * */
static BOOL ICACHE_FLASH_ATTR open_file_impl(File *file, uint8_t *filename, uint8_t *extname, BOOL rawname) {
    FileBlock *fb;
    uint32_t addr_start, addr_end, i;
    // 栈上分配保证4字节对齐，允许强制转换成(uint32_t *)
    uint8_t slot_buffer[FILEBLOCK_SIZE];
    // 全部转换成原始文件名
    uint8_t tempFileName[FILENAME_SIZE], tempExtName[EXTNAME_SIZE];

    if(rawname) {
    	os_memcpy(tempFileName, filename, FILENAME_SIZE);
    	os_memcpy(tempExtName, extname, EXTNAME_SIZE);
    }else {
    	os_memset(tempFileName, 0xFF, FILENAME_SIZE);
    	os_memset(tempExtName, 0xFF, EXTNAME_SIZE);
    	i = os_strlen((const char *)filename);
    	os_memcpy(tempFileName, filename, i);
    	i = os_strlen((const char *)extname);
    	os_memcpy(tempExtName, extname, i);
    }

    for(i = FB_SECTOR_START; i < (FB_SECTOR_END + 1); i++) {

        addr_start = i * SECTOR_SIZE;
        addr_end = (addr_start + SECTOR_SIZE - 1);

        while((addr_end - addr_start) >= FILEBLOCK_SIZE) {
            spi_flash_read(addr_start, (uint32_t *)slot_buffer, FILEBLOCK_SIZE);
            fb = (FileBlock *)slot_buffer;
            // 忽略标记删除/废弃的文件
            if(!(fb->info.state.del & fb->info.state.dep)) {
            	addr_start += FILEBLOCK_SIZE;
				continue;
            }
            // 检查文件名与拓展名
            if(filename_equals(fb->filename, tempFileName, FILENAME_SIZE) && filename_equals(fb->extname, tempExtName, EXTNAME_SIZE)) {
                file->block = addr_start;
                file->cluster = fb->cluster;
                file->length = fb->length;
                os_memcpy(file->filename, fb->filename, FILENAME_SIZE);
                os_memcpy(file->extname, fb->extname, EXTNAME_SIZE);
                return TRUE;
            }
            addr_start += FILEBLOCK_SIZE;
        }
    }
    return FALSE;
}

Result ICACHE_FLASH_ATTR rename_file(File *file, char *filename, char *extname) {
	return rename_file_impl(file, (uint8_t *)filename, (uint8_t *)extname, FALSE);
}


Result ICACHE_FLASH_ATTR rename_file_raw(File *file, uint8_t *filename, uint8_t *extname) {
	return rename_file_impl(file, filename, extname, TRUE);
}
/**
 * @brief 重命名文件
 * @param *file 文件指针
 * @param *filename 文件名
 * @param *extname 拓展名
 * @return Result 成功: FILE_RENAME_SUCCESS, 其他结果码见Result定义
 */
static Result ICACHE_FLASH_ATTR rename_file_impl(File *file, uint8_t *filename, uint8_t *extname, BOOL raw) {
    FileInfo fileinfo;
    uint32_t fnamelen, extnamelen;

#ifdef SPIFS_USE_NULL_CHECK
    if(file == NULL || file->block == EMPTY_INT_VALUE) {
        return FILE_NOT_EXIST;
    }
#endif
    // 读出文件状态字
    read_finfo(file, &fileinfo);
    // 文件状态检查
    if(fileinfo.state.del & fileinfo.state.dep & fileinfo.state.rw) {
    	if(raw) {
    		fnamelen = strlen_ext(filename, FILENAME_SIZE);
			extnamelen = strlen_ext(extname, EXTNAME_SIZE);
    	}else {
    		fnamelen = os_strlen((const char *)filename);
    		extnamelen = os_strlen((const char *)extname);
    	}
        // 输入文件名长度检查
        if(fnamelen > FILENAME_SIZE || extnamelen > EXTNAME_SIZE) {
            return FILENAME_OUT_OF_BOUNDS;
        }


        if(spifs_avail_files() > 0) {
            // 标记文件索引表原始文件对应文件块失效，但不执行擦除操作
            write_fileblock_state(file->block, FSTATE_DEPRECATE);
            // 清空原文件名
            os_memset((file->filename), EMPTY_BYTE_VALUE, FILENAME_SIZE);
            os_memset((file->extname), EMPTY_BYTE_VALUE, EXTNAME_SIZE);
            // 复制新文件名到file
            os_memcpy(file->filename, filename, fnamelen);
            os_memcpy(file->extname, extname, extnamelen);
            // 重新创建文件索引块
            return (CREATE_FILE_SUCCESS == create_file(file, &fileinfo)) ? FILE_RENAME_SUCCESS : NO_FILEBLOCK_SPACE;
        }
        return NO_FILEBLOCK_SPACE;
    }
    return FILE_CANNOT_RENAME;
}

/**
 * @param *files 用于存储查找到的文件信息
 * @param max *files的最大容量
 * @param *startAddr 用于接收下一次list_file的起始地址(FB_SECTOR物理地址)，第一次调用传入FB_SECTOR_START*4096
 * @return count 实际查找到的文件数量(count <= max)
 * */
uint32_t ICACHE_FLASH_ATTR list_file(uint32_t *startAddr, File *files, uint32_t max) {
    BOOL firstIn = TRUE;
	uint32_t addr_start, addr_end, count = 0;
	FileBlock *fb;
    uint8_t buffer[FILEBLOCK_SIZE];
	uint32_t sector = ((*startAddr) / SECTOR_SIZE);

	for(; (sector < (FB_SECTOR_END + 1)) && (count < max); sector++) {
		if(firstIn) {
			firstIn = FALSE;
			addr_start = (*startAddr);
			addr_end = (sector * SECTOR_SIZE + SECTOR_SIZE - 1);
		}else {
	        addr_start = sector * SECTOR_SIZE;
	        addr_end = (addr_start + SECTOR_SIZE - 1);
		}
        while(addr_end - addr_start >= FILEBLOCK_SIZE) {
		   spi_flash_read(addr_start, (uint32_t *)buffer, FILEBLOCK_SIZE);
		   fb = (FileBlock *)buffer;
		   if((fb->info.state.del & fb->info.state.dep) && (fb->cluster != EMPTY_INT_VALUE)) {
			   // FileBlock转File结构
			   os_memcpy((files + count)->filename, fb->filename, FILENAME_SIZE);
			   os_memcpy((files + count)->extname, fb->extname, EXTNAME_SIZE);
			   (files + count)->block = addr_start;
			   (files + count)->cluster = fb->cluster;
			   (files + count)->length = fb->length;
			   count++;
		   }
		   addr_start += FILEBLOCK_SIZE;
		   if(count >= max) {
			   if((addr_end - addr_start) <= FILEBLOCK_SIZE) {
				   // 下一FILEBLOCK地址在当前扇区
				   *startAddr = addr_start;
			   }else {
				   // 切换到下一扇区首地址
				   sector = (sector + 1);
				   // startAddr地址限制在FB_SECTOR_END扇区内
				   *startAddr = (sector < (FB_SECTOR_END + 1)) ? (sector * SECTOR_SIZE) : (FB_SECTOR_START * SECTOR_SIZE);
			   }
			   break;
		   }
	   }
	}
	return count;
}

/**
 * @brief 获取文件列表，buffer地址推荐size_t对齐
 * @param startAddr 起始查找的物理地址,首次调用传入FB_SECTOR_START*4096
 * @param buffer 存储(File + FileInfo)结构数据集(28bytes对齐),长度>=max * 28bytes
 * @param max 最大获取的数量
 * @return 实际获取的数量，startAddr指向的内存会被修改返回
 * */
uint32_t ICACHE_FLASH_ATTR list_file_raw(uint32_t *startAddr, uint8_t *buffer, uint32_t max) {
    BOOL firstIn = TRUE;
	uint32_t addr_start, addr_end, count = 0;
    FileBlock *fb;
    uint8_t fileblock[FILEBLOCK_SIZE];
	uint32_t sector = ((*startAddr) / SECTOR_SIZE);

	for(; (sector < (FB_SECTOR_END + 1)) && (count < max); sector++) {
		if(firstIn) {
			firstIn = FALSE;
			addr_start = (*startAddr);
			addr_end = (sector * SECTOR_SIZE + SECTOR_SIZE - 1);
		}else {
	        addr_start = sector * SECTOR_SIZE;
	        addr_end = (addr_start + SECTOR_SIZE - 1);
		}
        while(addr_end - addr_start >= FILEBLOCK_SIZE) {
		   spi_flash_read(addr_start, (uint32_t *)fileblock, FILEBLOCK_SIZE);
		   fb = (FileBlock *)fileblock;
		   if((fb->info.state.del & fb->info.state.dep) && (fb->cluster != EMPTY_INT_VALUE)) {
			   // File结构
			   os_memcpy((buffer + count * 28), fileblock, FILENAME_SIZE);
			   os_memcpy((buffer + count * 28 + FILENAME_SIZE), (fileblock + FILENAME_SIZE), EXTNAME_SIZE);
			   // 文件索引记录地址
			   os_memcpy((buffer + count * 28 + FILENAME_FULLSIZE), &addr_start, sizeof(uint32_t));
			   // 首簇地址
			   os_memcpy((buffer + count * 28 + 16), (fileblock + FILENAME_FULLSIZE), sizeof(uint32_t));
			   // 文件大小
			   os_memcpy((buffer + count * 28 + 20), (fileblock + 16), sizeof(uint32_t));
			   // FileInfo结构
			   os_memcpy((buffer + count * 28 + 24), (fileblock + 20), sizeof(uint32_t));
			   count++;
		   }
		   addr_start += FILEBLOCK_SIZE;
		   if(count >= max) {
			   if((addr_end - addr_start) <= FILEBLOCK_SIZE) {
				   // 下一FILEBLOCK地址在当前扇区
				   *startAddr = addr_start;
			   }else {
				   // 切换到下一扇区首地址
				   sector = (sector + 1);
				   // startAddr地址限制在FB_SECTOR_END扇区内
				   *startAddr = (sector < (FB_SECTOR_END + 1)) ? (sector * SECTOR_SIZE) : (FB_SECTOR_START * SECTOR_SIZE);
			   }
			   break;
		   }
	   }
	}
	return count;
}

/**
 * @brief 读取文件信息
 * @param *file 文件结构指针
 * @param *finfo 存放文件信息指针
 * @return FALSE: file=null 或finfo = null, TRUE: 读取成功
 */
BOOL ICACHE_FLASH_ATTR read_finfo(File *file, FileInfo *finfo) {
    FileBlock *fb;
    uint8_t slot_buffer[FILEBLOCK_SIZE];

#ifdef SPIFS_USE_NULL_CHECK
    if(file == NULL || finfo == NULL) {
        return FALSE;
    }
#endif

    spi_flash_read(file->block, (uint32_t *)slot_buffer, sizeof(FileBlock));
    fb = (FileBlock *)slot_buffer;
    os_memcpy(finfo, &(fb->info), sizeof(FileInfo));

    return TRUE;
}

/**
 * @brief 查找数据区空闲扇区, 不主动回收，读写均衡
 * @param *secList 存放空闲扇区首地址缓冲区
 * @param nums 需要查找的扇区数量
 * @return TRUE: 成功找到nums个空扇区, FALSE: 空扇区数量 < nums
 * */
static BOOL ICACHE_FLASH_ATTR find_empty_sector(uint32_t *secList, uint32_t nums) {
    uint32_t sector_index, sector_mark, cnt = 0;
    for(sector_index = DATA_SECTOR_START; ((cnt < nums) && (sector_index < (DATA_SECTOR_END + 1))); sector_index++) {
        spi_flash_read((sector_index * SECTOR_SIZE), &sector_mark, sizeof(uint32_t));
        if(sector_mark == EMPTY_INT_VALUE) {
            *(secList + cnt) = (sector_index * SECTOR_SIZE);
            cnt++;
        }
    }
    return (cnt == nums);
}

/**
 * 删除文件, 此操作不会立即擦除扇区
 * 而将文件状态字标注为被删除,仅在垃圾回收时才会擦除扇区数据
 * @param *file 文件指针
 * */
void ICACHE_FLASH_ATTR delete_file(File *file) {
	uint32_t cluster;
	if(file->block != EMPTY_INT_VALUE) {
		// 标记文件索引删除
		write_fileblock_state(file->block, FSTATE_DELETE);
        // 根据链表标记文件占用扇区废弃
        while(file->cluster != EMPTY_INT_VALUE) {
            update_sector_mark(file->cluster, SECTOR_DISCARD_FLAG);
            spi_flash_read((file->cluster + DATA_AREA_SIZE + SECTOR_MARK_SIZE), &cluster, sizeof(uint32_t));
            file->cluster = cluster;
        }
		file->block = EMPTY_INT_VALUE;
		file->cluster = EMPTY_INT_VALUE;
		file->length = EMPTY_INT_VALUE;
	}
}

/**
 * @brief spifs垃圾回收
 * @brief 应用层的删除文件操作并不会从闪存中擦除文件数据, 而是标记其文件块的状态属性为可删除文件
 * @param tp GC类型，GC_TYPE_FILEBLOCK：仅扫描文件索被标记删除/失效/无cluster的item
 * 					GC_TYPE_DATAAREA：仅扫描数据区被标记废弃的扇区
 * 					GC_TYPE_MAJOR：Full GC
 * @param nums 期望GC后回收的数量
 * @return  返回实际回收的数量
 * 			对于GC_TYPE_FILEBLOCK，返回值通常 >= nums
 * 			对于GC_TYPE_DATAAREA，返回值 == nums
 * 			对于GC_TYPE_MAJOR，返回值 = FILEBLOCK回收数量+DATAAREA回收数量
 * */
uint32_t ICACHE_FLASH_ATTR spifs_gc(GCType tp, uint32_t nums) {
    FileBlock *fb = NULL;
    BOOL rewrite = FALSE;
    uint8_t slot_buffer[FILEBLOCK_SIZE];
    uint32_t offset, fb_index, count = 0;
    uint8_t *sector_buffer = (uint8_t *)os_malloc(sizeof(uint8_t) * SECTOR_SIZE);

    // 扫描文件索引表查找被标记文件
    if(tp == GC_TYPE_FILEBLOCK || tp == GC_TYPE_MAJOR) {
    	sector_buffer = (uint8_t *)os_malloc(sizeof(uint8_t) * SECTOR_SIZE);

    	for(fb_index = FB_SECTOR_START; fb_index < (FB_SECTOR_END + 1); fb_index++) {
			offset = 0;
			spi_flash_read((fb_index * SECTOR_SIZE), (uint32_t *)sector_buffer, SECTOR_SIZE);
			// 最小回收一个扇区
			while((SECTOR_SIZE - offset) >= FILEBLOCK_SIZE) {
				os_memcpy(slot_buffer, (sector_buffer + offset), FILEBLOCK_SIZE);

				if(!fb_has_name(slot_buffer)) {
					offset += FILEBLOCK_SIZE;
					continue;
				}

				fb = (FileBlock *)slot_buffer;
				if((fb->info.state.del) == FILE_STATE_MARKED) {
					// 文件被标识为删除, 清除文件索引信息
					clear_fileblock(sector_buffer, offset);
					rewrite = TRUE;
					count++;
				}else if((fb->info.state.dep) == FILE_STATE_MARKED) {
					// 文件被标记为失效, 清除文件索引信息
					clear_fileblock(sector_buffer, offset);
					rewrite = TRUE;
					count++;
				}else if(fb->cluster == EMPTY_INT_VALUE) {
					// 创建文件但未填充数据, 空文件索引
					clear_fileblock(sector_buffer, offset);
					rewrite = TRUE;
					count++;
				}
				offset += FILEBLOCK_SIZE;
			}
			// 擦除文件索引扇区，回写新文件索引表
			if(rewrite) {
				rewrite = FALSE;
				spi_flash_erase_sector(fb_index);
				spi_flash_write(fb_index * SECTOR_SIZE, (uint32_t *)sector_buffer, SECTOR_SIZE);
			}
			if(tp == GC_TYPE_FILEBLOCK && count >= nums) {
				// only fileblock and count more than nums
				break;
			}
		}
		os_free(sector_buffer);
    }

    // 扫描数据扇区, 查找标记为废弃扇区, fb_index复用做扇区首地址, offset复用做扇区标识符
    if(tp == GC_TYPE_DATAAREA || tp == GC_TYPE_MAJOR) {
    	count = (tp == GC_TYPE_DATAAREA) ? 0 : count;
    	for(fb_index = DATA_SECTOR_START; (fb_index < (DATA_SECTOR_END + 1) && count < nums); fb_index++) {
			spi_flash_read(fb_index * SECTOR_SIZE, &offset, sizeof(uint32_t));
			// LSB      MSB
			// FF FF FF FF 空扇区不做处理
			// F0 FF FF FF 有效数据扇区不做处理
			// 00 FF FF FF 标记为废弃扇区需擦除
			if(offset == SECTOR_DISCARD_FLAG) {
				spi_flash_erase_sector(fb_index);
				count++;
			}
		}
    }

    return count;
}

/**
 * @brief 文件系统格式化
 * @brief 仅擦除文件索引块区/数据区扇区，擦除完成后为0xFF
 * */
void ICACHE_FLASH_ATTR spifs_format() {
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
 * @brief 效果等同于spifs_format
 * @note spifs_erase_sector一次只擦除一个扇区，适用于不能阻塞CPU的场合
 * @param sec 扇区编号 FB_SECTOR_START~FB_SECTOR_END or DATA_SECTOR_START~DATA_SECTOR_END
 * */
BOOL ICACHE_FLASH_ATTR spifs_erase_sector(uint32_t sec) {
	sec &= 0xFFFF;
	if((sec >= FB_SECTOR_START) && (sec < FB_SECTOR_END + 1)) {
		spi_flash_erase_sector(sec);
		return TRUE;
	}
	if((sec >= DATA_SECTOR_START) && (sec < DATA_SECTOR_END + 1)) {
		spi_flash_erase_sector(sec);
		return TRUE;
	}
	return FALSE;
}

/**
 * @brief 查询flash数据区可用扇区数量
 * @return 空闲的扇区
 * */
uint32_t ICACHE_FLASH_ATTR spifs_avail_sector() {
    uint32_t i, temp, avail = 0;
    for(i = DATA_SECTOR_START; i < (DATA_SECTOR_END + 1); i++) {
        spi_flash_read(i * SECTOR_SIZE, &temp, sizeof(uint32_t));
        if(temp != SECTOR_INUSE_FLAG) {
        	// SECTOR_DISCARD_FLAG & EMPTY_INT_VALUE都认为是空闲扇区
            avail++;
        }
    }
    return avail;
}

/**
 * @brief 查询flash文件索引区还能创建的文件数量
 * @return 文件索引区可创建文件数量
 * */
uint32_t ICACHE_FLASH_ATTR spifs_avail_files() {
    uint32_t sec_index, addr_start, addr_end, avail = 0;
    uint8_t fb_buffer[FILENAME_FULLSIZE];
    for(sec_index = FB_SECTOR_START; sec_index < (FB_SECTOR_END + 1); sec_index++) {

    	addr_start = sec_index * SECTOR_SIZE;
        addr_end = (addr_start + SECTOR_SIZE - 1);

        while((addr_end - addr_start) >= FILEBLOCK_SIZE) {

            spi_flash_read(addr_start, (uint32_t *)fb_buffer, FILENAME_FULLSIZE);

            if(!fb_has_name(fb_buffer)) {
                avail++;
            }
            addr_start += FILEBLOCK_SIZE;
        }

    }
    return avail;
}

/**
 * @brief 获取文件系统版本
 * @return uint16 低字节子版本号,高字节主版本号
 */
uint16_t ICACHE_FLASH_ATTR spifs_get_version() {
    return ((MAJOR_VERSION << 8) | MINOR_VERSION);
}

/**
 * @brief 字符串长度计算，以0xFF为结束符，限制长度
 * @param *str 文件名字符串
 * @param max 文件名的最大长度
 * @return 文本实际长度
 * */
static uint32_t ICACHE_FLASH_ATTR strlen_ext(uint8_t *str, uint32_t max) {
    uint32_t i;

    for(i = 0; ((i < max) && (*str != EMPTY_BYTE_VALUE)); i++, str++);

    return i;
}

/**
 * @brief 比较文件索引块文件名与输入文件名
 * @param *src 文件块的文件名
 * @param *target 输入的文件名
 * @param length 文件名长度类型: FILENAME_SIZE:文件名, EXTNAME_SIZE:拓展名
 * @return TRUE:文件名相同, FALSE:文件名不同
 * */
static BOOL ICACHE_FLASH_ATTR filename_equals(uint8_t *src, uint8_t *target, uint32_t length) {
    uint32_t i = 0, temp1, temp2;
    if((length & 0x3) != 0) {
    	return FALSE;
    }
    for(; i < length; i += sizeof(uint32_t)) {
        memcpy(&temp1, (src + i), sizeof(uint32_t));
        memcpy(&temp2, (target + i), sizeof(uint32_t));
        if(temp1 != temp2) {
            return FALSE;
        }
    }
    return TRUE;
}

/**
 * @brief 判断文件索引是否含有文件名/拓展名
 * @param *fb_buffer 文件索引块, FileBlock *可强制转换成uint8_t *
 * @return TRUE: 含有文件名, FALSE: 文件索引不含有文件名
 */
static BOOL fb_has_name(uint8_t *fb_buffer) {
	uint32_t i = 0, temp;
    for(; i < FILENAME_FULLSIZE; i += sizeof(uint32_t)) {
        memcpy(&temp, (fb_buffer + i), sizeof(uint32_t));
        if(temp != EMPTY_INT_VALUE) {
            return TRUE;
        }
    }
    return FALSE;
}
