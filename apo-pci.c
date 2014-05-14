/** @file */

#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include "kbd_hw.h"
#include "chmod_lcd.h"
#include "apo-pci.h"

char* devEnable; // "/sys/bus/pci/devices/0000:03:00.0/enable"
uint32_t devAddress; // 0xfe8f0000

#define CTRL 0x8020
#define DATA_OUT 0x8040
#define DATA_IN 0x8060

unsigned char* base;

/**
 * Enables and disables PCI
 * @param setStatus 1 turn on, 0 turn off.
 * @return 1 on success, 0 on failure
 */
int pciEnable(int setStatus) {
    char status = (setStatus != 0) ? '1' : '0';
    int enableDevice = open(devEnable, O_WRONLY);
    if (enableDevice == -1) {
        if (status == '1')
            fprintf(stderr, "Enabling PCI device failed!");
        else
            fprintf(stderr, "Disabling PCI device failed!");
        return 0;
    }
    write(enableDevice, &status, 1);
    close(enableDevice);

    return 1;
}

/**
 * Writes control byte to PCI card
 * @param offsetAddr Specific offset
 * @param controlData data to write
 * @return 1 on success, otherwise return err number
 */
int writeCtrlByte(int offsetAddr, unsigned char controlData) {
    *(base + DATA_OUT) = controlData; // write control data byte
    usleep(1);
    *(base + CTRL) = ((CTRL_PWR | CTRL_CS0 | CTRL_RD | (CTRL_WR & 0)) << 4) + offsetAddr; // activate WR and offsetAddr
    *(base + CTRL) = ((CTRL_PWR | (CTRL_CS0 & 0) | CTRL_RD | (CTRL_WR & 0)) << 4) + offsetAddr; // activate WR, CS0 and offsetAddr
    usleep(1);
    *(base + CTRL) = ((CTRL_PWR | CTRL_CS0 | CTRL_RD | (CTRL_WR & 0)) << 4) + offsetAddr; // deactivate CS0
    *(base + CTRL) = ((CTRL_PWR | CTRL_CS0 | CTRL_RD | CTRL_WR) << 4) | 0xF; // deactivate WR, CS0
    return 1;
}

/**
 * Reads control byte to PCI card
 * @param offsetAddr specific offset
 * @return 1 on success, otherwise return error number
 */
unsigned char readCtrlByte(int offsetAddr) {
    unsigned char readChar;

    *(base + CTRL) = ((CTRL_PWR | CTRL_CS0 | (CTRL_RD & 0) | CTRL_WR) << 4) + offsetAddr; // activate RD and offsetAddr
    *(base + CTRL) = ((CTRL_PWR | (CTRL_CS0 & 0) | (CTRL_RD & 0) | CTRL_WR) << 4) + offsetAddr; // activate RD, CS0 and offsetAddr
    usleep(10);
    readChar = *(base + DATA_IN); // read
    *(base + CTRL) = ((CTRL_PWR | CTRL_CS0 | (CTRL_RD & 0) | CTRL_WR) << 4) + offsetAddr; // deactivate CS0
    *(base + CTRL) = ((CTRL_PWR | CTRL_CS0 | CTRL_RD | CTRL_WR) << 4) | 0x0; // deactivate RD, CS0

    return readChar;
}

/**
 * Masks columns of keyboard matrix and reads the row. Assigns and returns correct character according to mapping.
 * @return pressed key or ' ' if no key active
 */
unsigned char readKey() {
    unsigned char columnMask[] = {0xB, 0xD, 0xE};
    unsigned char rowMask[] = {0x1, 0x2, 0x4, 0x8, 0x10};
    unsigned char mapped[5][3] = {
        {'1', '5', '8'},
        {'2', '6', ' '},
        {'3', '7', ' '},
        {'4', 'X', 'M'}
    };
    int i, j;
    unsigned char readByte;

    for (i = 0; i < 3; i++) {
        writeCtrlByte(BUS_KBD_WR_o, columnMask[i]); // write column

        for (j = 0; j < 5; j++) {
            readByte = readCtrlByte(BUS_KBD_RD_o);
            if ((readByte & rowMask[j]) == 0) { // row active
                return mapped[j][i];
            }
        }
    }
    return ' ';

}

/**
 * Beeps for a given period of time. Writes the control byte and leave it for given period of milliseconds
 * @param durationInMillis duration of the beep in milliseconds
 */
void beep(int durationInMillis) {
    writeCtrlByte(BUS_KBD_WR_o, KBD_BUZZ);
    usleep(1000 * durationInMillis);
    writeCtrlByte(BUS_KBD_WR_o, 0x0);
}

/**
 * Reads key twice after 1ms and compares read characters, if they are the same it returns the character, space otherwise.
 * @return pressed key
 */
unsigned char doubleReadKey() {
    unsigned char character1 = readKey(base);
    usleep(1000);
    unsigned char character2 = readKey(base);
    if (character1 == character2)
        return character1;
    else
        return ' ';
}

/**
 * Sends initialization sequence to the LCD display
 */
void initDisplay() {
    writeCtrlByte(BUS_LCD_INST_o, CHMOD_LCD_MOD); // init setup
    usleep(10000);
    writeCtrlByte(BUS_LCD_INST_o, CHMOD_LCD_MOD);
    usleep(10000);
    writeCtrlByte(BUS_LCD_INST_o, CHMOD_LCD_CLR); // clear internal memory
    usleep(10000);
    writeCtrlByte(BUS_LCD_INST_o, CHMOD_LCD_DON); // display on
    usleep(10000);
}

/**
 * Writes single character to the LCD
 * @param position position on display
 * @param character character to write
 * @param line line to write
 */
void writeCharToLCD(int position, char character, int line) {
    while ((readCtrlByte(BUS_LCD_STAT_o) & CHMOD_LCD_BF) == 1) { // wait until ready
        usleep(1000);
    }
    if (line == 1) {
        writeCtrlByte(BUS_LCD_INST_o, CHMOD_LCD_POS + position);
    } else if (line == 2) {
        writeCtrlByte(BUS_LCD_INST_o, CHMOD_LCD_POS + (position + (0x40))); // second line with 0x40 offset
    }
    writeCtrlByte(BUS_LCD_WDATA_o, character);
}

/**
 * Turns on the LED
 * @param value
 */
void writeLED(unsigned char value) {
    writeCtrlByte(BUS_LED_WR_o, value);
}

/**
 * Writes whole string to the LCD using writeCharToLCD()
 * @param string string to write
 * @param line line to write
 */
void writeStrToLCD(char* string, int line) {
    int i;
    for (i = 0; i < 16; i++) {
        if (string[i] == '\0') {
            writeCharToLCD(i, ' ', line);
            break;
        }
        writeCharToLCD(i, string[i], line);
    }
}

/**
 * Parses /proc/meminfo
 * @return lines with caption and memory usage in MB
 */
linesStruct ramUsage() {
    FILE* memFile = fopen("/proc/meminfo", "r");
    int memFree, memTotal;
    size_t n = 0;
    char* line = NULL;

    while (1) {
        getline(&line, &n, memFile);
        if (line == NULL) {
            break;
        }
        if (strstr(line, "MemTotal:") != NULL) {
            sscanf(line, "MemTotal:  %d kB", &memTotal);
            continue;
        }
        if (strstr(line, "MemFree:") != NULL) {
            sscanf(line, "MemFree:  %d kB", &memFree);
            break;
        }
    }
    linesStruct lines;
    sprintf(lines.line1, "Vyuziti RAM:    ");
    sprintf(lines.line2, "%d / %d MB      ", (memTotal - memFree) / 1024, memTotal / 1024);

    fclose(memFile);
    free(line);

    return lines;
}

/**
 * Parses /proc/cpuinfo
 * @return lines with caption and CPU frequency
 */
linesStruct cpuFreq() {
    FILE* cpuFile = fopen("/proc/cpuinfo", "r");
    float freq = 0;
    size_t n = 0;
    char* line = NULL;

    while (1) {
        getline(&line, &n, cpuFile);
        if (line == NULL) {
            break;
        }
        if (strstr(line, "cpu MHz") != NULL) {
            sscanf(line, "cpu MHz  :  %f", &freq);
            break;
        }
    }
    linesStruct lines;
    sprintf(lines.line1, "Frekvence CPU:  ");
    sprintf(lines.line2, "%.0f MHz        ", freq);

    fclose(cpuFile);
    free(line);
    return lines;
}

/**
 * Parses /proc/stat
 * @return lines with caption and CPU usage in percent
 */
linesStruct cpuCurLoad() {
    FILE* statFile = fopen("/proc/stat", "r");
    int user1, nice1, system1, idle1;
    int user2, nice2, system2, idle2;

    size_t n = 0;
    char* line = NULL;

    getline(&line, &n, statFile);
    sscanf(line, "%*s %d %d %d %d %*s", &user1, &nice1, &system1, &idle1);

    rewind(statFile);

    usleep(100000);

    getline(&line, &n, statFile);
    sscanf(line, "%*s %d %d %d %d %*s", &user2, &nice2, &system2, &idle2);

    fclose(statFile);

    int busy = user2 + nice2 + system2 - user1 - nice1 - system1;
    int total = busy + idle2 - idle1;
    if (total == 0) {
        total = 1;
    }

    linesStruct lines;
    sprintf(lines.line1, "Zatizeni CPU:   ");
    sprintf(lines.line2, "%d%%            ", busy * 100 / total);
    return lines;
}

int avgloadswitch = 0;

/**
 * Parses /proc/loadavg
 * @return lines with caption and CPU load averages
 */
linesStruct cpuAvgLoad() {
    FILE* loadavg = fopen("/proc/loadavg", "r");
    char avg1[4], avg5[4], avg15[4];

    size_t n = 0;
    char* line = NULL;

    getline(&line, &n, loadavg);
    sscanf(line, "%s %s %s", avg1, avg5, avg15);

    fclose(loadavg);

    linesStruct lines;
    if (avgloadswitch % 10 < 5) {
        sprintf(lines.line1, "1m   5m   15m");
        sprintf(lines.line2, "%s %s %s", avg1, avg5, avg15);
    } else {
        sprintf(lines.line1, "Historie        ");
        sprintf(lines.line2, "vytizeni CPU:   ");
    }
    avgloadswitch++;
    return lines;
}

#define FUNC_READ 0
#define FUNC_SPENT_READING 1
#define FUNC_WRITE 2
#define FUNC_SPENT_WRITING 3

/**
 * Parses /proc/diskstats
 * @param function determine output information
 * @return lines with statistical disk information
 */
linesStruct diskStats(int function) {
    FILE* diskstats = fopen("/proc/diskstats", "r");
    int read, spentReading, written, spentWritting;

    size_t n = 0;
    char* line = NULL;
    getline(&line, &n, diskstats);
    while (!strstr(line, "sda")) {
        getline(&line, &n, diskstats);
    }
    line = strndup(line + 17, 100);
    sscanf(line, "%*d %*d %d %d %*d %*d %d %d", &read, &spentReading, &written, &spentWritting);

    fclose(diskstats);

    linesStruct lines;
    if (function == 0) {
        sprintf(lines.line1, "Precteno sekt.: ");
        sprintf(lines.line2, "%d              ", read);
    } else if (function == 1) {
        sprintf(lines.line1, "Straveno ctenim:");
        sprintf(lines.line2, "%d ms           ", spentReading);
    } else if (function == 2) {
        sprintf(lines.line1, "Zapsano sekt.:  ");
        sprintf(lines.line2, "%d              ", written);
    } else {
        sprintf(lines.line1, "Strav. zapisem: ");
        sprintf(lines.line2, "%d ms           ", spentWritting);
    }
    return lines;
}

int menuswitch = 0;

/**
 * Displays menu, changes output when called more times (simple animation)
 * @return lines with menu
 */
linesStruct menuScreen() {
    linesStruct lines;

    if (menuswitch % 24 < 5) {
        sprintf(lines.line1, "Stiskem tlacitka");
        sprintf(lines.line2, "vyberte funkci  ");
    } else if (menuswitch % 24 < 10) {
        sprintf(lines.line1, "Navrat libovol. ");
        sprintf(lines.line2, "tlacitkem       ");
    } else if (menuswitch % 24 < 15) {
        sprintf(lines.line1, "1. sloupec      ");
        sprintf(lines.line2, "infor. o disku  ");
    } else if (menuswitch % 24 < 20) {
        sprintf(lines.line1, "2. sloupec      ");
        sprintf(lines.line2, "informace o CPU ");
    } else {
        sprintf(lines.line1, "3. sloupec      ");
        sprintf(lines.line2, "informace o RAM ");
        menuswitch = 0;
    }

    menuswitch++;
    return lines;
}

/**
 * Reads first 4 bytes that identifies a device
 * @param path file to read
 * @return device id
 */
unsigned int readIdFromFile(char* path) {
    FILE *file = fopen(path, "rb");
    unsigned int value;
    unsigned short *pointer;
    pointer = (unsigned short*) &value;
    fread(pointer, 2, 1, file);
    fread(pointer + 1, 2, 1, file);
    fclose(file);
    return value;
}

/**
 * Compares id in a file defined by path and deviceId in a parameter.
 * @param path file to compare
 * @param deviceId device id
 * @return 1 if is the same, 0 otherwise
 */
int isCorrectId(char* path, unsigned short deviceId[2]) {
    unsigned int currentId;
    unsigned short *pointer;
    currentId = readIdFromFile(path);
    pointer = (unsigned short *) &currentId;
    if (pointer[0] != deviceId[0] || pointer[1] != deviceId[1]) {
        return 0;
    } else {
        return 1;
    }
}

/**
 * Walks through two levels of directories in /proc/bus/pci/ and returns file path for matching device id
 * @param deviceId id to match
 * @return path to a file containing given id
 */
char* findDevFile(unsigned short* deviceId) {
    DIR *directory, *subDirectory;
    struct dirent *file;
    char path[100] = "/proc/bus/pci/";
    char *devPath;

    devPath = (char*) malloc(100);
    directory = opendir(path);
    while ((file = readdir(directory)) != NULL) { // /proc/bus/pci/xx
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
            continue;
        }
        strcpy(path + 14, file->d_name);

        if ((subDirectory = opendir(path)) != NULL) {
            while ((file = readdir(subDirectory)) != NULL) { // /proc/bus/pci/03/xx.x
                if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
                    continue;
                }
                strcpy(devPath, path);
                int length = strlen(devPath);
                devPath[length++] = '/';
                strcpy(devPath + length, file->d_name);
                if (isCorrectId(devPath, deviceId)) {
                    closedir(subDirectory);
                    closedir(directory);
                    return devPath;
                }
            }
            closedir(subDirectory);
        }
        free(file);
    }
    closedir(directory);
    return NULL;
}

/**
 * Reads device address
 * @param path device file path
 * @return address of the device
 */
uint32_t readAddress(char *path) {
    uint32_t address;
    FILE *file;
    file = fopen(path, "rb");
    fseek(file, 0x10, SEEK_SET);
    fread(&address, 4, 1, file);
    fclose(file);
    return address;
}

int main() {
    int file = open("/dev/mem", O_RDWR | O_SYNC);
    if (file == -1) {
        fprintf(stderr, "Accessing physical memory failed. Do you run under root?");
        return 1;
    }

    unsigned short deviceId[2] = {0x1172, 0x1f32}; // module id
    devEnable = findDevFile(deviceId); // get device file
    if (devEnable == NULL) {
        fprintf(stderr, "Device not found.");
        return 1;
    }
    devAddress = readAddress(devEnable); // get device address
    base = mmap(NULL, 0x10000, PROT_WRITE | PROT_READ, MAP_SHARED, file, devAddress); // creates address mapping
    if (base == MAP_FAILED) {
        fprintf(stderr, "Mapping virtual memory failed!");
        return 2;
    }

    if (pciEnable(1)) {
        printf("Device activated\n");
        *(base + CTRL) = 0x80; // power
        writeCtrlByte(3, 0x01); // disable buzzer
        initDisplay(base);

        linesStruct lines;
        char function = 'M';
        int display = 0;
        int led = 0;
        while (1) {
            char readKey = doubleReadKey();
            if (readKey != ' ' && function != readKey) {
                function = readKey;
                beep(100);
            }
            if (display % 10 == 0) {
                switch (function) {
                    case '1':
                        lines = diskStats(FUNC_READ);
                        led = 0x1;
                        break;
                    case '2':
                        lines = diskStats(FUNC_SPENT_READING);
                        led = 0x2;
                        break;
                    case '3':
                        lines = diskStats(FUNC_WRITE);
                        led = 0x4;
                        break;
                    case '4':
                        lines = diskStats(FUNC_SPENT_WRITING);
                        led = 0x8;
                        break;
                    case '5':
                        lines = cpuFreq();
                        led = 0x10;
                        break;
                    case '6':
                        lines = cpuCurLoad();
                        led = 0x20;
                        break;
                    case '7':
                        lines = cpuAvgLoad();
                        led = 0x40;
                        break;
                    case '8':
                        lines = ramUsage();
                        led = 0x1;
                        break;
                    case 'M':
                        lines = menuScreen();
                        led = 0x0;
                        break;
                    case 'X':
                        pciEnable(0);
                        *(base + CTRL) = 0x00; // power
                        return 0;
                }
                writeLED(led);
                writeStrToLCD(lines.line1, 1);
                writeStrToLCD(lines.line2, 2);
            }
            display++;
            usleep(25000);
        }
    }
    return 0;
}
