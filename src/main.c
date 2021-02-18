#include <stdio.h>
#include "spifs.h"
#include "w25q32.h"
#include "string.h"
#include <time.h>

#define OUTPUTPATH    ("I:\\ramdisk")

#define LOCALIZATION
#undef LOCALIZATION

typedef union _short {
	uint8_t bytes[2];
	uint16_t value;
} Short;

static void display_fname(File *file);
void disp_list(FileList *list);

static void load_into(char *fullname, char *filename, char *extname);
static void test_create();
static void read_test();
static void rename_test();
static void append_exist_file_test();

static uint8_t byteStrTohex(uint8_t *str);

static Short queryGB2312ByUnicode(Short unicode);

uint8_t *unicodeTogb2312(uint8_t *unicode);

int main(int argc, char **argv) {
    FileList *list;
    uint16_t spifs_version;

    w25q32_allocate();
    printf("init flash finish\n");

    spifs_version = spifs_get_version();
    printf("spifs version:%d.%d\n", (spifs_version >> 8 & 0xFF), (spifs_version & 0xFF));

    spifs_format();
    printf("spifs format\n");
    /*
    load_into("G:\\font\\GB2312.bin", "GB2312", "bin");
    load_into("G:\\font\\AS06_12.bin", "AS06_12", "bin");
    load_into("G:\\font\\AS08_16.bin", "AS08_16", "bin");
    load_into("G:\\font\\GB64SP.bin", "GB64SP", "bin");
    */
    load_into("I:\\fontmap\\utf16.lut", "utf16", "lut");

    //load_into("G:\\fontmap\\gb2312.lut", "gb2312", "lut");

    //test_create();

    //append_exist_file_test();

    //read_test();

    //rename_test();

    const char *str = "3-4\\u7ea7";
    uint8_t buffer[32];

    strcpy((char *)buffer, str);

    unicodeTogb2312(buffer);
    puts("buffer output:");
    for(int i = 0; *(buffer + i) != 0x00; i++) {
        printf("0x%02x ", *(buffer + i));
    }
    putchar('\n');

    // �ļ��г�
    list = list_file();
    disp_list(list);
    recycle_filelist(list);

    // ʣ��ռ�
    uint32_t availFiles = spifs_avail_files();
    printf("> spifs_avail_files:%d\n", availFiles);
    uint32_t availSector= spifs_avail();
    printf("> spifs_avail_sectors:%d\n", availSector);

    #ifdef LOCALIZATION
    uint8_t code = w25q32_output(OUTPUTPATH, "wb+", 0x0, 49152);
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

    uint8_t *buffer = (uint8_t *)malloc(sizeof(uint8_t) * 16);

    make_finfo(&finfo, 2020, 9, 2, (FSTATE_DEFAULT));
    make_file(&file, "tiimage", "c");

    result = create_file(&file, &finfo);

    if(result == CREATE_FILE_SUCCESS) {
        puts("> CREATE_FILE_SUCCESS");
        printf("> file.block:0x%x\n", file.block);

        puts("align write test:");
        // ����׷��д�����
        memset(buffer, 0xAA, sizeof(uint8_t) * 12);
        result = write_file(&file, buffer, 12, APPEND);

        if(result == WRITE_FILE_SUCCESS) {
            puts("> WRITE_FILE_SUCCESS");
        }else if(result == APPEND_FILE_SUCCESS) {
            puts("> APPEND_FILE_SUCCESS");
        }else {
            printf("> write_file err:%d\n", result);
        }

        puts("not align write test:");
        // �Ƕ���׷��д�����
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
        // ����д����
        for(uint32_t i = 0; i < 12; i++) {
            *(buffer + i) = i;
        }
        result = write_file(&file, buffer, 12, OVERRIDE);
    }
    free(buffer);
}

static void append_exist_file_test() {
    File file;
    Result result;
    uint8_t buffer[16];

    memset(buffer, 0xCC, 15);

    if(open_file(&file, "tiimage", "c")) {
        printf("Open file success!\n");

        result = write_file(&file, buffer, 15, APPEND);
        printf("> write_file result:%d\n", result);

        result = write_finish(&file);
        printf("> write_finish result:%d\n", result);

        return;
    }
    printf("Open file failed!\n");
}

static void read_test() {
    uint8_t buffer[16];
    BOOL result;
    File file;
    // ���ļ�����
    if(open_file(&file, "tiimage", "c")) {

        printf("open_file success\n");

        memset(buffer, 0x00, 16);

        // ƫ��/���ȶ����
        result = read_file(&file, 0, buffer, 8);
        printf("> read file result:%d\n", result);
        for(uint32_t i = 0; i < 8; i++) {
            printf("0x%x ", buffer[i]);
        }
        printf("\n");

        // ƫ�Ʋ����룬���ȶ���
        memset(buffer, 0x00, 16);
        result = read_file(&file, 3, buffer, 8);
        printf("> read file result:%d\n", result);
        for(uint32_t i = 0; i < 8; i++) {
            printf("0x%x ", buffer[i]);
        }
        printf("\n");
        // ƫ�Ʋ����룬���Ȳ�����
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

/**
 * @brief unicode��ʽ�ַ���תgb2312����, ĩβ�Ჹ'\0'
 * @brief ���Խ��ܴ�unicode�ַ�"\u591a\u4e91", ת������С��ģʽ0x1a 0x59 0x91 0x4e
 * @brief ���Խ���ascii + unicode�ַ����"3-4\u7ea7", ת����ascii�벻�䣬unicode���С��ģʽ0x33 0x2d 0x34 0xa7 0x7e
 * @param *unicode unicode�ַ���
 * @return ת��Ϊgb2312������ַ���, ʵ���Ϸ��ص�char *ͬΪ�����char *unicode, ��Ϊת��������ݻ����������С
 * */
uint8_t *unicodeTogb2312(uint8_t *unicode) {
    uint8_t *ch;
    Short code;
    uint32_t index = 0, write = 0;
    while(*(unicode + index) != 0x00) {
        ch = (unicode + index);
        // ������unicode�ַ�����ascii���ֱ��ֲ���
        if(*ch == '\\' && *(ch + 1) == 'u') {
        	// unicode С��
        	code.bytes[0] = byteStrTohex(ch + 4);
        	code.bytes[1] = byteStrTohex(ch + 2);
        	code = queryGB2312ByUnicode(code);
        	// gb2312���
            *(unicode + write) = code.bytes[1];
            write += 1;
            *(unicode + write) = code.bytes[0];
            write += 1;
            // ������unicode��
            index += 6;
            continue;
        }else {
            write++;
        }
        index++;
    }
    *(unicode + write) = '\0';
	return unicode;
}

/**
 * @brief �ֽ��ַ�����ʽת��ֵ
 * @brief �ַ���֮ǰ����"0x"�� ���ݴ�Сд��ϣ���"30", "7C", "aB"
 * @param *str �ֽ��ַ���ָ�룬��ָ�ֽ��ַ�����ͷ����"0x"
 * @return value ת������ֽ���ֵ
 * */
static uint8_t byteStrTohex(uint8_t *str) {
    int i; uint8_t ch;
    uint8_t value = 0;
    for(i = 0; i < 2; i++) {
        ch = *(str + i);
        if(ch >= 0x30 && ch <= 0x39) {
            // 0 ~ 9
            value |= (ch - 0x30);
        }else if(ch >= 0x61 && ch <= 0x66) {
        	// a ~ f
            value |= (ch - 0x61 + 0xA);
        }else if(ch >= 0x41 && ch <= 0x46) {
        	// A ~ F
            value |= (ch - 0x41 + 0xA);
        }
        value <<= (4 - (i << 2));
    }
    return value;
}

/**
 * @brief ʹ��utf16.lut���ұ��ѯunicode��Ӧ��gb2312����
 * @brief unicodeС�˷�ʽ���룬gb2312��˷�ʽ���
 * @param unicode ˫�ֽ�unicode��
 * @return gb2312 gb2312�ַ��������ģʽ
 * */
static Short queryGB2312ByUnicode(Short unicode) {
	File file;
	Short gb2312;
	BOOL success;
	uint16_t readin = 0;
	uint32_t pack, group;
	int32_t start, middle, end;
	gb2312.value = 0;

	if(open_file(&file, "utf16", "lut")) {
		// ���ֽ�һ�飬�����ֽ�Ϊunicode�������ֽ�Ϊgb2312
		group = (file.length / sizeof(uint32_t));
		middle = group / 2;
		end = group;
		start = -1;
		do {
			// ���ַ������м����
			success = read_file(&file, (middle * sizeof(uint32_t)), (uint8_t *)&pack, sizeof(uint32_t));
			if(!success || middle <= 0 || middle >= (group - 1)) {
				break;
			}
			readin = (uint16_t)(pack & 0xFFFF);
			if(unicode.value < readin) {
				// �����ѯ
				end = middle;
				middle -= ((end - start) / 2);
			}else if(unicode.value > readin) {
				// ���Ҳ�ѯ
				start = middle;
				middle += ((end - start) / 2);
			}
		}while(unicode.value != readin);

		gb2312.bytes[0] = (uint8_t)((pack >> 24) & 0xFF);
		gb2312.bytes[1] = (uint8_t)((pack >> 16) & 0xFF);
	}
	return gb2312;
}
