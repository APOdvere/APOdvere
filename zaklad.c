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

char* dev_enable; //DEV_ENABLE "/sys/bus/pci/devices/0000:03:00.0/enable"
uint32_t dev_address; //DEV_ADDRESS 0xfe8f0000
unsigned char * base_address;
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

    if (pciEnable(1)) {
        printf("Gate %d is ready.\n", gate_id);
        *(base_address + CTRL) = POWER_ON; // zapni napajeni
        usleep(1000);
        turn_off_piezo();
        turn_on_LED();
        usleep(1000000); // wait 1s
        turn_off_LED();
        // TODO: semestralni prace
        *(base_address + CTRL) = POWER_OFF; // vypni napajeni
    }
    pciEnable(0);

    return 0;
}

//TODO: Podprogram pro zapis/cteni bytu na emulovane PCI sbernici
//+TODO: cteni klavesnice,
//+TODO zapis na LCD
//+TODO: program automatu na jizdenky