#define CTRL 0x8080
#define POWER_ON 0xFF
#define POWER_OFF 0x00

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

int pciEnable(int isEnable) {
    char cen = (isEnable != 0) ? '1' : '0';
    int enable = open(dev_enable, O_WRONLY);
    if (enable == -1) {
        if (cen == '1') {
            fprintf(stderr, "Failed to enable PCI device.");
        } else {
            fprintf(stderr, "Failed to disable PCI device.");
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
    free(file);
    if (id_ptr[0] == device_id[0] && id_ptr[1] == device_id[1]) {
        return 1;
    } else {
        return 0;
    }
}

#define PATH_LEN 64
#define CURRENT_PATH "/sys/bus/pci/"
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
            free(file);
            continue;
        }
        strcpy(current_path + CURRENT_PATH_LEN, file->d_name);
        free(file);

        if ((subdir = opendir(current_path)) != NULL) {
            while ((file = readdir(subdir)) != NULL) {
                if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) { // this or parent directory
                    free(file);
                    continue;
                }
                strcpy(file_path, current_path);
                int file_path_len = strlen(file_path);
                file_path[file_path_len++] = '/';
                strcpy(file_path + file_path_len, file->d_name);
                free(file);
                if (has_correct_device_id(file_path, device_id)) {
                    closedir(subdir);
                    free(subdir);
                    closedir(base_dir);
                    free(base_dir);
                    return file_path;
                }
            }
            closedir(subdir);
            free(subdir);
        }
    }

    closedir(base_dir);
    free(base_dir);
    free(file_path);
    return NULL;
}

uint32_t read_device_address(char *file_path) {
    uint32_t address;
    FILE *file;
    file = fopen(file_path, "rb");
    fseek(file, 0x10, SEEK_SET);
    fread(&address, 4, 1, file);
    fclose(file);
    free(file);
    return address;
}

int main() {
    int soubor = open("/dev/mem", O_RDWR | O_SYNC);
    if (soubor == -1) {
        fprintf(stderr, "Failed to access memory.");
        return 1;
    }

    unsigned short device_id[2] = {0x1172, 0x1f32};
    dev_enable = find_device(device_id);
    if (dev_enable == NULL) {
        fprintf(stderr, "Failed to find device.");
        return 1;
    }
    dev_address = read_device_address(dev_enable);

    // TODO: konstantu dev_address a delku 0x10000 musite najit prohledanim seznamu PCI zarizeni
    unsigned char * base = mmap(NULL, 0x10000, PROT_WRITE | PROT_READ, MAP_SHARED, soubor, dev_address);
    if (base == MAP_FAILED) {
        fprintf(stderr, "Failed to map memory.");
        return 2;
    }

    if (pciEnable(1)) {
        *(base + CTRL) = POWER_ON; // zapni napajeni
        sleep(1); // cekej 1 vterinu - kratsi doba cekani-hledejte: usleep, nanosleep
        // TODO: semestralni prace
        *(base + CTRL) = POWER_OFF; // vypni napajeni
    }
    pciEnable(0);

    return 0;
}

//TODO: Podprogram pro zapis/cteni bytu na emulovane PCI sbernici
//+TODO: cteni klavesnice,
//+TODO zapis na LCD
//+TODO: program automatu na jizdenky