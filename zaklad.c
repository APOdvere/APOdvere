#define CTRL 0x8080
#define ADDR 0x8060
#define DATA_READ 0x8040
#define DATA_WRITE 0x8020
#define POWER_ON 0xFF
#define POWER_OFF 0x00
#define WR_ON 0xFD
#define WR_OFF 0xFF
#define CS_ON 0xBD
#define CS_OFF 0xFD
#define RD_ON 0xFE
#define RD_OFF 0xFF

#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include "kbd_hw.h"
#include "chmod_lcd.h"

char *dev_enable; //DEV_ENABLE "/sys/bus/pci/devices/0000:03:00.0/enable"
uint32_t dev_address; //DEV_ADDRESS 0xfe8f0000
unsigned char *base_address;
int gate_id;

int pciEnable(int isEnable) {
    char cen = (isEnable != 0) ? '1' : '0';
    int enable = open(dev_enable, O_WRONLY);
    if (enable == -1) {
        if (cen == '1') {
            fprintf(stderr, "Failed to enable PCI device.\n");
        } else {
            fprintf(stderr, "Failed to disable PCI device.\n");
        }
        return 0;
    }
    write(enable, &cen, 1);
    close(enable);
    return 1;
}

int has_correct_device_id(char* file_path, unsigned short device_id[2]) {
    FILE *file = fopen(file_path, "rb");
    unsigned int id;
    unsigned short *id_ptr;
    id_ptr = (unsigned short*) &id;
    fread(id_ptr, 2, 1, file);
    fread(id_ptr + 1, 2, 1, file);
    fclose(file);
    if (id_ptr[0] == device_id[0] && id_ptr[1] == device_id[1]) {
        return 1;
    } else {
        return 0;
    }
}

#define PATH_LEN 64
#define CURRENT_PATH "/proc/bus/pci/"
#define CURRENT_PATH_LEN 14

char* find_device(unsigned short device_id[2]) {
    char current_path[PATH_LEN] = CURRENT_PATH;
    DIR *base_dir, *subdir;
    struct dirent *file;
    char *file_path;

    file_path = (char*) malloc(PATH_LEN * sizeof (char));
    base_dir = opendir(current_path);

    while ((file = readdir(base_dir)) != NULL) {
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) { // this or parent directory
            continue;
        }
        strcpy(current_path + CURRENT_PATH_LEN, file->d_name);

        if ((subdir = opendir(current_path)) != NULL) {
            while ((file = readdir(subdir)) != NULL) {
                if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) { // this or parent directory
                    continue;
                }
                strcpy(file_path, current_path);
                int file_path_len = strlen(file_path);
                file_path[file_path_len++] = '/';
                strcpy(file_path + file_path_len, file->d_name);
                if (has_correct_device_id(file_path, device_id)) {
                    closedir(subdir);
                    closedir(base_dir);
                    return file_path;
                }
            }
        }
    }

    free(file);
    closedir(subdir);
    closedir(base_dir);
    return NULL;
}

uint32_t read_device_address(char *file_path) {
    uint32_t address;
    FILE *file;
    file = fopen(file_path, "rb");
    fseek(file, 0x10, SEEK_SET);
    fread(&address, 4, 1, file);
    fclose(file);
    return address;
}

void write_to_address(unsigned char address, unsigned char data) {
    *(base_address + ADDR) = address;
    *(base_address + DATA_WRITE) = data;

    *(base_address + CTRL) = WR_ON;
    *(base_address + CTRL) = CS_ON;
    usleep(10);
    *(base_address + CTRL) = CS_OFF;
    *(base_address + CTRL) = WR_OFF;
}

unsigned char read_from_address(unsigned char address) {
    *(base_address + ADDR) = address;

    *(base_address + CTRL) = RD_ON;
    *(base_address + CTRL) = 0xBE;
    usleep(10);
    unsigned char data = *(base_address + DATA_READ);
    *(base_address + CTRL) = 0xFE;
    *(base_address + CTRL) = RD_OFF;
    return data;
}

void turn_on_LED() {
    write_to_address(BUS_LED_WR_o, POWER_ON);
}

void turn_off_LED() {
    write_to_address(BUS_LED_WR_o, POWER_OFF);
}

void turn_off_piezo() {
    write_to_address(0x03, 0x00);
}

void clear_LCD() {
    write_to_address(BUS_LCD_INST_o, CHMOD_LCD_CLR);
    usleep(10000);
}

void turn_on_LCD() {
    write_to_address(BUS_LCD_INST_o, CHMOD_LCD_MOD);
    usleep(10000);
    clear_LCD();
    write_to_address(BUS_LCD_INST_o, CHMOD_LCD_DON);
    usleep(10000);
}

void write_char_to_LCD(char c, int row, int pos) {
    if (row == 1) {
        pos = pos + 0x40;
    }
    write_to_address(BUS_LCD_INST_o, CHMOD_LCD_POS + pos);
    write_to_address(BUS_LCD_WDATA_o, c);
}

void print_to_LCD(char * text, int len, int row, int start) {
    int i;
    for (i = start; i < len; i++) {
        write_char_to_LCD(text[i], row, i);
    }
}

void print_std_and_LCD(char *text, int text_len, int row, int start) {
    printf("%s", text);
    printf("\n");
    print_to_LCD(text, text_len, row, start);
}

void print_std_and_LCD_from_start(char *text, int text_len, int row) {
    print_std_and_LCD(text, text_len, row, 0);
}

#define ENTER_KEY 'e'
#define CANCEL_KEY 'c'
#define BLANK_KEY ' '
#define EXIT_KEY 'x'

char read_key_once() {
    unsigned char columns[] = {0xB, 0xD, 0xE};
    unsigned char rows[] = {0x1, 0x2, 0x4, 0x8, 0x10};
    char key_map[4][3] = {
        {'7', '8', '9'},
        {'4', '5', '6'},
        {'1', '2', '3'},
        {ENTER_KEY, EXIT_KEY, CANCEL_KEY}
    };
    int i, j;
    unsigned char read;

    for (i = 0; i < 3; i++) {
        write_to_address(BUS_KBD_WR_o, columns[i]); // write column

        for (j = 0; j < 4; j++) {
            read = read_from_address(BUS_KBD_RD_o);
            if ((read & rows[j]) == 0) { // row active
                return key_map[j][i];
            }
        }
    }
    return BLANK_KEY;
}

char read_key() {
    char c1 = read_key_once(c1);
    usleep(1000);
    char c2 = read_key_once(c2);
    if (c1 != c2) {
        return BLANK_KEY;
    }
    return c1;
}

#define ID_LEN 6
#define PASS_LEN 6

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "One argument expected (gate id).\n");
        return 1;
    }
    gate_id = atoi(argv[1]);
    int soubor = open("/dev/mem", O_RDWR | O_SYNC);
    if (soubor == -1) {
        fprintf(stderr, "Failed to access memory.\n");
        return 1;
    }

    unsigned short device_id[2] = {0x1172, 0x1f32};
    dev_enable = find_device(device_id);
    if (dev_enable == NULL) {
        fprintf(stderr, "Failed to find device.\n");
        return 1;
    }
    dev_address = read_device_address(dev_enable);

    base_address = mmap(NULL, 0x10000, PROT_WRITE | PROT_READ, MAP_SHARED, soubor, dev_address);
    if (base_address == MAP_FAILED) {
        fprintf(stderr, "Failed to map memory.\n");
        return 2;
    }

    /*if (pciEnable(1)) {*/
    *(base_address + CTRL) = POWER_ON; // zapni napajeni
    usleep(1000);
    turn_off_piezo();
    turn_on_LED();
    usleep(1000000); // wait 1s
    turn_off_LED();
    turn_on_LCD();

    printf("Gate %d is ready.\n", gate_id);
    print_to_LCD("Gate is ready.", 14, 0, 0);
    usleep(1000000); // wait 1s
    clear_LCD();

    char *id = (char*) malloc(ID_LEN * sizeof (char));
    char *pass = (char*) malloc(PASS_LEN * sizeof (char));
    char c;
    c = BLANK_KEY;

    while (c != EXIT_KEY) {
        c = BLANK_KEY;
        clear_LCD();

        print_to_LCD("ID: ", 4, 0, 0);
        printf("ID: ");
        int id_len = 0;
        while (c != ENTER_KEY && c != CANCEL_KEY && c != EXIT_KEY && id_len < ID_LEN) {
            usleep(100000); // wait 0.1s
            c = read_key();
            if (c == BLANK_KEY) {
                continue;
            }
            if (c != ENTER_KEY && c != CANCEL_KEY && c != EXIT_KEY) {
                id[id_len] = c;
                write_char_to_LCD(c, 0, 4 + id_len);
                id_len++;
            }
            usleep(400000); // wait 0.4s
        }
        id[id_len] = '\0';
        printf("%s", id);
        printf("\n");
        if (c == CANCEL_KEY) {
            continue;
        } else if (c == EXIT_KEY) {
            break;
        }
        c = BLANK_KEY;

        print_to_LCD("PASS: ", 6, 1, 0);
        printf("PASS: ");
        int pass_len = 0;
        while (c != ENTER_KEY && c != CANCEL_KEY && c != EXIT_KEY && id_len < PASS_LEN) {
            usleep(100000); // wait 0.1s
            c = read_key();
            if (c == BLANK_KEY) {
                continue;
            }
            if (c != ENTER_KEY && c != CANCEL_KEY && c != EXIT_KEY) {
                pass[pass_len] = c;
                write_char_to_LCD(c, 1, 6 + pass_len);
                pass_len++;
            }
            usleep(400000); // wait 0.4s
        }
        pass[pass_len] = '\0';
        printf("%s", pass);
        printf("\n");
    }

    *(base_address + CTRL) = POWER_OFF; // vypni napajeni
    /*}
    pciEnable(0);*/

    return 0;
}

//+TODO: program automatu na jizdenky
