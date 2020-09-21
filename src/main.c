#include <stdio.h>
#include "spifs.h"
#include "w25q32.h"
#include "string.h"
#include <time.h>

#define OUTPUTPATH    ("G:\\ramdisk")

static void display_fname(File *file);
void disp_list(FileList *list);

static void test_create();
static void read_test();

int main(int argc, char **argv) {
    uint16_t spifs_version;

    w25q32_allocate();
    printf("init flash finish\n");

    spifs_version = spifs_get_version();
    printf("spifs version:%d.%d\n", (spifs_version >> 8 & 0xFF), (spifs_version & 0xFF));

    spifs_format();
    printf("spifs format\n");

    test_create();

    read_test();

    w25q32_destory();
    puts("w25q32 destory");

    return 0;
}

// test functions

static void test_create() {
    File file;
    FileInfo finfo;

    Result result;
    FileList *list;
    uint8_t *buffer = (uint8_t *)malloc(sizeof(uint8_t) * 16);

    make_finfo(&finfo, 2020, 9, 2, (FSTATE_DEFAULT));
    make_file(&file, "tiimage", "c");

    result = create_file(&file, &finfo);

    if(result == CREATE_FILEBLOCK_SUCCESS) {
        puts("> CREATE_FILEBLOCK_SUCCESS");
        printf("> file.block:0x%x\n", file.block);
        // 对齐追加写入测试
        memset(buffer, 0xAA, sizeof(uint8_t) * 12);
        result = write_file(&file, buffer, 12, APPEND);
        // 非对齐追加写入测试
        memset(buffer, 0xBB, sizeof(uint8_t) * 12);
        result = write_file(&file, buffer, 3, APPEND);

        if(result == WRITE_FILE_SUCCESS) {
            puts("> WRITE_FILE_SUCCESS");
        }else if(result == APPEND_FILE_SUCCESS) {
            puts("> APPEND_FILE_SUCCESS");
        }else {
            printf("> write_file err:%d\n", result);
        }
        write_finish(&file);
        // 覆盖写测试
        for(uint32_t i = 0; i < 12; i++) {
            *(buffer + i) = i;
        }
        result = write_file(&file, buffer, 12, OVERRIDE);
    }
    // 文件列出
    list = list_file();
    disp_list(list);
    recycle_filelist(list);
    // 剩余空间
    uint32_t availFiles = spifs_avail_files();
    printf("> spifs_avail_files:%d\n", availFiles);
    uint32_t availSector= spifs_avail();
    printf("> spifs_avail_sectors:%d\n", availSector);

    uint8_t code = w25q32_output(OUTPUTPATH, "wb+", 0, 0xD0000 + 81920);
    if(code) {
        puts("> w25q32_output");
    }
    free(buffer);
}

static void read_test() {
    uint8_t buffer[16];
    BOOL result;
    File file;
    // 打开文件测试
    if(open_file(&file, "tiimage", "c")) {
        memset(buffer, 0x00, 16);
        // 偏移/长度对齐读
        result = read_file(&file, 0, buffer, 8);
        printf("> read file result:%d\n", result);
        for(uint32_t i = 0; i < 8; i++) {
            printf("0x%x ", buffer[i]);
        }
        printf("\n");
        // 偏移不对齐，长度对齐
        memset(buffer, 0x00, 16);
        result = read_file(&file, 3, buffer, 8);
        printf("> read file result:%d\n", result);
        for(uint32_t i = 0; i < 8; i++) {
            printf("0x%x ", buffer[i]);
        }
        printf("\n");
        // 偏移不对齐，长度不对齐
        memset(buffer, 0x00, 16);
        result = read_file(&file, 3, buffer, 7);
        printf("> read file result:%d\n", result);
        for(uint32_t i = 0; i < 7; i++) {
            printf("0x%x ", buffer[i]);
        }
        printf("\n");

        puts("> read test finish !");
    }else {
        puts("> open filed !");
    }
}

static void display_fname(File *file) {
    uint32_t i = 0;
    uint8_t *ptr = file->filename;
    for(i = 0; i < FILENAME_FULLSIZE; i++) {
        if(*(ptr + i) == 0xFF) {
            if(i > 8) break;
            putchar('.');
            i = 8;
        }
        putchar(*(ptr + i));
    }
}

void disp_list(FileList *list) {
    FileList *ptr = NULL;
    FileInfo finfo;
    FileStatePack fspack;
    puts("--------filelist--------");
    while(list != NULL) {
        ptr = list->prev;
        printf("filename:");
        display_fname(&list->file);
        putchar('\n');

        read_finfo(&(list->file), &finfo);
        printf("create_time: %d-%d-%d\n", (finfo.year+2000), finfo.month, finfo.day);
        fspack.fstate = finfo.state;

        printf("file_state: 0x%x\n", fspack.data);
        printf("block_addr: 0x%x,cluster_addr: 0x%x,length: 0x%x\n",
               list->file.block, list->file.cluster, list->file.length);
        list = ptr;
    }
}

