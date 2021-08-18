#include <stdio.h>
#include "spifs.h"
#include "w25q32.h"
#include "common_def.h"

#define OUTPUTPATH    ("G:\\ramdisk")
#define LOCALIZATION

static void display_fname(File *file);
void disp_list(File *file, uint32_t total);

static void load_into(char *fullname, char *filename, char *extname);
static void test_create();
static void read_test();
static void rename_test();
static void append_exist_file_test();

int main(int argc, char **argv) {

    w25q32_allocate();
    printf("init flash finish\n");

    uint16_t spifs_version = spifs_get_version();
    printf("spifs version:%d.%d\n", (spifs_version >> 8 & 0xFF), (spifs_version & 0xFF));

    spifs_format();
    printf("spifs format\n");

    printf("platform:%I64dbit\n", (sizeof(size_t) * 8));

    /*
    load_into("G:\\font\\GB2312.bin", "GB2312", "bin");
    load_into("G:\\font\\AS06_12.bin", "AS06_12", "bin");
    load_into("G:\\font\\AS08_16.bin", "AS08_16", "bin");
    load_into("G:\\font\\GB64SP.bin", "GB64SP", "bin");

    load_into("I:\\fontmap\\utf16.lut", "utf16", "lut");

    load_into("G:\\fontmap\\gb2312.lut", "gb2312", "lut");
    */

    test_create();

    append_exist_file_test();

    read_test();

    rename_test();

    File filse[8];
    uint32_t next = 0, find = 0;

    // 文件列出
    find = list_file(&next, filse, 8);
    printf("list_file: %d\n", find);

    disp_list(filse, find);

    // 剩余空间
    uint32_t availFiles = spifs_avail_files();
    printf("> spifs_avail_files:%d\n", availFiles);

    uint32_t availSector= spifs_avail_sector();
    printf("> spifs_avail_sectors:%d\n", availSector);

    #ifdef LOCALIZATION
        uint8_t code = w25q32_output(OUTPUTPATH, "wb+", FB_SECTOR_START * SECTOR_SIZE, 512 * 1024);
        if(code) {
            puts("> w25q32_output");
        }
    #endif // LOCALIZATION

    w25q32_destory();
    puts("w25q32 destory");

    return 0;
}

static void load_into(char *fullname, char *filename, char *extname) {
    // stdio
    uint32_t fsize;
    FILE *raw = fopen(fullname, "rb");
    // spifs
    const uint32_t year = 2020;
    const uint8_t month = 11, day = 4, fstate = FSTATE_SYSTEM;
    const uint32_t limit = 4088;
    uint32_t loadsize, part = 0;
    File file;
    FileInfo finfo;
    Result result;
    uint8_t *buffer;

    if(raw == NULL) {
        printf("open %s fail, interrupted !\n", fullname);
        return;
    }

    fseek(raw, 0, SEEK_END);
    fsize = ftell(raw);

    fseek(raw, 0, SEEK_SET);
    buffer = (uint8_t *)malloc(sizeof(uint8_t) * limit);

    // create new file
    make_finfo(&finfo, year, month, day, fstate);
    make_file(&file, filename, extname);
    result = create_file(&file, &finfo);
    printf("create %s.%s result:%d\n", filename, extname, result);

    if(result != CREATE_FILE_SUCCESS) {
    printf("create %s.%s fail, interrupted !\n", filename, extname);
        return;
    }

    while(fsize > 0) {
        loadsize = (fsize > limit) ? limit : fsize;
        fread(buffer, 1, loadsize, raw);
        result = write_file(&file, buffer, loadsize, APPEND);
        printf("part:%d, write_file result:%d\n", part, result);
        fsize -= loadsize;
        part++;
    }
    result = write_finish(&file);
    printf("write_finish result:%d\n", result);

    fclose(raw);
    free(buffer);
}

// test functions

static void test_create() {
    File file;
    FileInfo finfo;
    Result result;
    uint8_t buffer[16];

    make_finfo(&finfo, 2020, 9, 2, (FSTATE_DEFAULT));
    make_file(&file, "tiimage", "c");

    result = create_file(&file, &finfo);

    printf("test_create: ");

    if(result == CREATE_FILE_SUCCESS) {
        printf("> CREATE_FILE_SUCCESS");
        printf("> file.block:0x%x\n", file.block);

        printf("address and length aligned write test: ");
        // 对齐追加写入测试
        memset(buffer, 0xAA, sizeof(uint8_t) * 16);
        result = write_file(&file, buffer, 12, APPEND);
        if(result == APPEND_FILE_SUCCESS) {
            puts("> APPEND_FILE_SUCCESS");
        }else {
            printf("> write_file err:%d\n", result);
        }

        printf("length not aligned write test: ");
        // 非对齐追加写入测试
        memset(buffer, 0xBB, sizeof(uint8_t) * 12);
        result = write_file(&file, buffer, 3, APPEND);
        if(result == APPEND_FILE_SUCCESS) {
            puts("> APPEND_FILE_SUCCESS");
        }else {
            printf("> write_file err:%d\n", result);
        }

        printf("address not aligned write test: ");
        // 对齐追加写入测试
        memset(buffer, 0xCC, sizeof(uint8_t) * 16);
        result = write_file(&file, (buffer + 1), 8, APPEND);
        if(result == APPEND_FILE_SUCCESS) {
            puts("> APPEND_FILE_SUCCESS");
        }else {
            printf("> write_file err:%d\n", result);
        }

        printf("address & length not aligned write test: ");
        // 对齐追加写入测试
        memset(buffer, 0xDD, sizeof(uint8_t) * 16);
        result = write_file(&file, buffer + 2, 5, APPEND);
        if(result == APPEND_FILE_SUCCESS) {
            puts("> APPEND_FILE_SUCCESS");
        }else {
            printf("> write_file err:%d\n", result);
        }

        write_finish(&file);

        // 覆盖写测试
        printf("override write test: ");
        for(uint32_t i = 0; i < 12; i++) {
            *(buffer + i) = i;
        }
        result = write_file(&file, buffer, 12, OVERRIDE);
        if(result == WRITE_FILE_SUCCESS) {
            puts("> WRITE_FILE_SUCCESS");
        }else {
            printf("> write_file err:%d\n", result);
        }
    }else {
        printf("> create_file err:%d\n", result);
    }
}

static void append_exist_file_test() {
    File file;
    Result result;
    uint8_t buffer[16];

    memset(buffer, 0xCC, 13);

    printf("append_exist_file_test\n");

    if(open_file(&file, "tiimage", "c")) {
        printf("open file success!\n");

        printf("append file: ");
        result = write_file(&file, buffer, 13, APPEND);
        if(result == APPEND_FILE_SUCCESS) {
            printf("> APPEND_FILE_SUCCESS\n");
        }else {
            printf("> append file err:%d\n", result);
        }

        printf("write finish: ");
        result = write_finish(&file);
        if(result == APPEND_FILE_FINISH) {
            printf("> APPEND_FILE_FINISH\n");
        }else {
            printf("> write finish err:%d\n", result);
        }
    }else {
        printf("open file failed!\n");
    }

}

static void read_test() {
    uint8_t buffer[16];
    BOOL result;
    File file;
    // 打开文件测试
    puts("read_test");

    if(open_file(&file, "tiimage", "c")) {

        printf("open_file success\n");

        memset(buffer, 0x00, 16);

        // 偏移/长度对齐读
        printf("offset and length aligned read: ");
        result = read_file(&file, 0, buffer, 8);
        printf("> actually read in:%d bytes\n", result);
        for(uint32_t i = 0; i < 8; i++) {
            printf("0x%x ", buffer[i]);
        }
        printf("\n");

        // 偏移不对齐，长度对齐
        printf("offset not aligned read: ");
        memset(buffer, 0x00, 16);
        result = read_file(&file, 3, buffer, 8);
        printf("> actually read in:%d bytes\n", result);
        for(uint32_t i = 0; i < 8; i++) {
            printf("0x%x ", buffer[i]);
        }
        printf("\n");
        // 偏移不对齐，长度不对齐

        printf("offset and length not aligned read: ");
        memset(buffer, 0x00, 16);
        result = read_file(&file, 3, buffer, 7);
        printf("> actually read in:%d bytes\n", result);
        for(uint32_t i = 0; i < 7; i++) {
            printf("0x%x ", buffer[i]);
        }
        printf("\n");

        puts("> read test finish !");
    }else {
        puts("> open filed !");
    }
}

static void rename_test() {
    File file;
    Result res;
    if(open_file(&file, "tiimage", "c")) {
        res = rename_file(&file, "hello", "java");
        printf("rename file result:%d\n", res);
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

void disp_list(File *files, uint32_t total) {
    FileInfo finfo;
    FileStatePack fspack;
    puts("--------filelist--------");

    for(int i = 0; i < total; i++) {
        printf("filename:"); display_fname((files + i)); putchar('\n');

        read_finfo((files + i), &finfo);
        printf("create_time: %d-%d-%d\n", (finfo.year+2000), finfo.month, finfo.day);
        fspack.fstate = finfo.state;
        printf("file_state: 0x%x\n", fspack.data);

        printf("block_addr: 0x%x,cluster_addr: 0x%x,length: 0x%x\n",
               (files + i)->block, (files + i)->cluster, (files + i)->length);
    }
}
