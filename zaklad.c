#define TRUE 1
#define FALSE 0

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
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <dirent.h>
#include "kbd_hw.h"
#include "chmod_lcd.h"

unsigned char *base_address;

/**
 * Enable PCI - should be done by BIOS when the computer starts.
 * @param isEnable
 * @param device_id
 * @return 1 if ok
 */
int pciEnable(int isEnable, char *dev_enable) {
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

/**
 * Check the device id is the right one.
 * @param file_path
 * @param device_id
 * @return 1 if the id corresponds
 */
int has_correct_device_id(char* file_path, unsigned short device_id[2]) {
    FILE *file = fopen(file_path, "rb");
    unsigned int id;
    unsigned short *id_ptr;
    id_ptr = (unsigned short*) &id;
    fread(id_ptr, 2, 1, file);
    fread(id_ptr + 1, 2, 1, file);
    fclose(file);
    if (id_ptr[0] == device_id[0] && id_ptr[1] == device_id[1]) {
        return TRUE;
    }
    return FALSE;
}

#define MAX_PATH_LEN 64
#define START_PATH "/proc/bus/pci/"
#define START_PATH_LEN 14

/**
 * Find the device file. In /proc/bus/pci/.
 * @param device_id
 * @return path of the device file, NULL when not found
 */
char* find_device(unsigned short device_id[2]) {
    char current_path[MAX_PATH_LEN] = START_PATH;
    DIR *base_dir, *subdir;
    struct dirent *file;
    char *file_path;

    file_path = (char*) malloc(MAX_PATH_LEN * sizeof (char));
    base_dir = opendir(current_path);

    while ((file = readdir(base_dir)) != NULL) {
        if (strcmp(file->d_name, ".") == FALSE || strcmp(file->d_name, "..") == FALSE) { // this or parent directory
            continue;
        }
        strcpy(current_path + START_PATH_LEN, file->d_name);

        if ((subdir = opendir(current_path)) != NULL) {
            while ((file = readdir(subdir)) != NULL) {
                if (strcmp(file->d_name, ".") == FALSE || strcmp(file->d_name, "..") == FALSE) { // this or parent directory
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

    closedir(subdir);
    closedir(base_dir);
    return NULL;
}

/**
 * Read device address from the file.
 * @param file_path
 * @return device address in the file
 */
uint32_t read_device_address(char *file_path) {
    uint32_t address;
    FILE *file;
    file = fopen(file_path, "rb");
    fseek(file, 0x10, SEEK_SET);
    fread(&address, 4, 1, file);
    fclose(file);
    return address;
}

/**
 * Write data to address into the device memory.
 * @param address
 * @param data
 */
void write_to_address(unsigned char address, unsigned char data) {
    *(base_address + ADDR) = address;
    *(base_address + DATA_WRITE) = data;

    *(base_address + CTRL) = WR_ON;
    *(base_address + CTRL) = CS_ON;
    usleep(10);
    *(base_address + CTRL) = CS_OFF;
    *(base_address + CTRL) = WR_OFF;
}

/**
 * Read data from address in the device memory.
 * @param address
 * @return data
 */
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

/**
 * Turn on LEDs on the device.
 */
void turn_on_LED() {
    write_to_address(BUS_LED_WR_o, POWER_ON);
}

/**
 * Turn off LEDs on the device.
 */
void turn_off_LED() {
    write_to_address(BUS_LED_WR_o, POWER_OFF);
}

/**
 * Turn off piezo on the device.
 */
void turn_off_piezo() {
    write_to_address(0x03, 0x00);
}

/**
 * Clear anything written on the LCD.
 */
void clear_LCD() {
    write_to_address(BUS_LCD_INST_o, CHMOD_LCD_CLR);
    usleep(10000);
}

/**
 * Turn on LCD on the device (clear included).
 */
void turn_on_LCD() {
    write_to_address(BUS_LCD_INST_o, CHMOD_LCD_MOD);
    usleep(10000);
    clear_LCD();
    write_to_address(BUS_LCD_INST_o, CHMOD_LCD_DON);
    usleep(10000);
}

/**
 * Write char on LCD into the given row and position.
 * @param c
 * @param row
 * @param position
 */
void write_char_to_LCD(char c, int row, int position) {
    if (row == 1) {
        position = position + 0x40;
    }
    write_to_address(BUS_LCD_INST_o, CHMOD_LCD_POS + position);
    write_to_address(BUS_LCD_WDATA_o, c);
}

/**
 * Print text on LCD into the given row starting on given position.
 * @param text
 * @param len
 * @param row
 * @param start
 */
void print_to_LCD(char * text, int text_length, int row, int start) {
    int i;
    for (i = start; i < text_length; i++) {
        write_char_to_LCD(text[i], row, i);
    }
}

#define ENTER_KEY 'e'
#define CANCEL_KEY 'c'
#define BLANK_KEY ' '
#define EXIT_KEY 'x'
#define COLUMNS 3
#define ROWS 4

/**
 * Read pressed key.
 * @return pressed key char
 */
char read_key_once() {
    unsigned char columns[] = {0xB, 0xD, 0xE};
    unsigned char rows[] = {0x1, 0x2, 0x4, 0x8};
    char key_map[ROWS][COLUMNS] = {
        {'7', '8', '9'},
        {'4', '5', '6'},
        {'1', '2', '3'},
        {ENTER_KEY, EXIT_KEY, CANCEL_KEY}
    };
    int c, r;
    unsigned char data;

    for (c = 0; c < COLUMNS; c++) {
        write_to_address(BUS_KBD_WR_o, columns[c]);
        data = read_from_address(BUS_KBD_RD_o);
        for (r = 0; r < ROWS; r++) {
            if ((data & rows[r]) == 0) {
                return key_map[r][c];
            }
        }
    }
    return BLANK_KEY;
}

/**
 * Read pressed key two times and return it when pressed both times.
 * @return
 */
char read_key() {
    char k1 = read_key_once(k1);
    usleep(1000);
    char k2 = read_key_once(k2);
    if (k1 != k2) {
        return BLANK_KEY;
    }
    return k1;
}

/**
 * Beep once for 0,5 s.
 */
void beepOK() {
    write_to_address(BUS_KBD_WR_o, 0x80);
    usleep(500000); //0,5 s
    write_to_address(BUS_KBD_WR_o, 0x00);
}

/**
 * Beep 3 times.
 */
void beepDenied() {
    write_to_address(BUS_KBD_WR_o, 0x80);
    usleep(1000000); //1 s
    write_to_address(BUS_KBD_WR_o, 0x00);

    usleep(1000000); //1 s

    write_to_address(BUS_KBD_WR_o, 0x80);
    usleep(1000000); //1 s
    write_to_address(BUS_KBD_WR_o, 0x00);

    usleep(1000000); //1 s

    write_to_address(BUS_KBD_WR_o, 0x80);
    usleep(1000000); //1 s
    write_to_address(BUS_KBD_WR_o, 0x00);
}

#define MAXDATASIZE 256
#define PORT "55556"
#define HOST "127.0.0.1"

/**
 * Establish connection to adress and returns socket.
 * @param address
 * @param port
 * @param hint
 * @return
 */
int getSocket(char *address, char *port, struct addrinfo hint) {
    struct addrinfo *res, *p;
    int r, sock_fd;

    if ((r = getaddrinfo(address, port, &hint, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(r));
    }

    for (p = res; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            continue;
        }

        break;
    }
    if (p == NULL) {
        return -1;
    }
    return sock_fd;
}

/**
 * Send message to server.
 * @param sfd
 * @param gate_id
 * @param user_id
 * @param user_pin
 * @return
 */
int sendMessage(int sfd, int gate_id, int user_id, int user_pin) {
    int sent = 0;
    char message[64];
    sprintf(message, "checkaccess %d %d %d\n", gate_id, user_id, user_pin);
    int mlen = strlen(message);
    int bytesleft = mlen;
    int n;

    while (sent < mlen) {
        n = send(sfd, message + sent, bytesleft, 0);
        if (n == -1) {
            return -1;
        }

        sent += n;
        bytesleft -= n;
    }
    return 0;
}

/**
 * Receive server response.
 * @param sfd
 * @param message
 * @return
 */
int recvMessage(int sfd, char *message) {
    int r;
    while (1) {

        r = recv(sfd, message, MAXDATASIZE, 0);

        if (r == -1) {
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        if (strstr(message, "\n") != NULL) {
            message[r] = '\0';
            return 1;
        }
    }

}

#define MAX_ID_LEN 6
#define MAX_PASS_LEN 6

/**
 * Main function - power the device, read user ID and password and check if the access is allowed.
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char *argv[]) {
    int sockfd, numbytes;
    char buf[MAXDATASIZE];
    struct addrinfo hints;
    int gate_id;
    char *dev_enable; //DEV_ENABLE "/sys/bus/pci/devices/0000:03:00.0/enable"
    unsigned short device_id[2] = {0x1172, 0x1f32}; //1172:1f32
    uint32_t dev_address; //DEV_ADDRESS 0xfe8f0000

    // check the number of arguments and get the gate id
    if (argc != 2) {
        fprintf(stderr, "One argument expected (gate id).\n");
        return 1;
    }
    gate_id = atoi(argv[1]);

    // access the memory
    int soubor = open("/dev/mem", O_RDWR | O_SYNC);
    if (soubor == -1) {
        fprintf(stderr, "Failed to access memory.\n");
        return 1;
    }

    // find the device and it address in memory (lspci -nn -v -d 1172:1f32)
    dev_enable = find_device(device_id);
    if (dev_enable == NULL) {
        fprintf(stderr, "Failed to find device.\n");
        return 1;
    }
    dev_address = read_device_address(dev_enable);

    // map the base_address
    base_address = mmap(NULL, 0x10000, PROT_WRITE | PROT_READ, MAP_SHARED, soubor, dev_address);
    if (base_address == MAP_FAILED) {
        fprintf(stderr, "Failed to map memory.\n");
        return 2;
    }

    //setup for socket connection
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    /*if (pciEnable(1, dev_enable)) {*/
    // turn on the device
    *(base_address + CTRL) = POWER_ON;
    usleep(1000);
    // turn of the piezo
    turn_off_piezo();
    // turn on LEDs for one second
    turn_on_LED();
    usleep(1000000);
    turn_off_LED();
    // turn on the LCD (and clear it))
    turn_on_LCD();

    // print the device is ready
    printf("Gate %d is ready.\n", gate_id);
    char gate_ready[20];
    strcpy(gate_ready, "Gate ");
    strcat(gate_ready, argv[1]);
    strcat(gate_ready, " is ready.");
    print_to_LCD(gate_ready, strlen(gate_ready), 0, 0);
    usleep(1000000); // wait 1s

    // prepare strings for the ID and password
    char *id = (char*) malloc(MAX_ID_LEN * sizeof (char));
    char *pass = (char*) malloc(MAX_PASS_LEN * sizeof (char));
    char key;

    while (key != EXIT_KEY) {
        clear_LCD();
        key = BLANK_KEY;

        // ask for the user ID and read it from keys
        print_to_LCD("ID: ", 4, 0, 0);
        printf("ID: ");
        int id_len = 0;
        while (id_len < MAX_ID_LEN) {
            usleep(100000); // wait 0.1s
            key = read_key();
            if (key == BLANK_KEY) {
                continue;
            }
            if (key == ENTER_KEY || key == CANCEL_KEY || key == EXIT_KEY) {
                break;
            }
            id[id_len] = key;
            write_char_to_LCD(key, 0, 4 + id_len);
            id_len++;
            usleep(400000); // wait 0.4s
        }
        // print the provided ID on stdout
        id[id_len] = '\0';
        printf("%s", id);
        printf("\n");

        if (key == CANCEL_KEY || key == EXIT_KEY) {
            continue;
        }
        key = BLANK_KEY;

        // ask for the password and read it from keys
        print_to_LCD("PASS: ", 6, 1, 0);
        printf("PASS: ");
        int pass_len = 0;
        while (pass_len < MAX_PASS_LEN) {
            usleep(100000); // wait 0.1s
            key = read_key();
            if (key == BLANK_KEY) {
                continue;
            }
            if (key == ENTER_KEY || key == CANCEL_KEY || key == EXIT_KEY) {
                break;
            }
            pass[pass_len] = key;
            write_char_to_LCD(key, 1, 6 + pass_len);
            pass_len++;
            usleep(400000); // wait 0.4s
        }
        // print the provided ID on stdout
        pass[pass_len] = '\0';
        printf("%s", pass);
        printf("\n");

        if (key == CANCEL_KEY || key == EXIT_KEY) {
            continue;
        }
        key = BLANK_KEY;

        // ask the server about the access and beep according its response
        if ((sockfd = getSocket(HOST, PORT, hints)) == -1) {
            fprintf(stderr, "Can't connect to server.\n");
            return 1;
        }
        if ((sendMessage(sockfd, 55, 33, 3333)) == -1) {
            fprintf(stderr, "Error sending to server.\n");
            return 1;
        }
        if ((numbytes = recvMessage(sockfd, buf)) <= 0) {
            fprintf(stderr, "Error response from server.\n");
            return 1;
        }
        clear_LCD();
        char *c1, *c2;
        if ((c1 = strstr(buf, "checkaccess")) != NULL && (c2 = strstr(buf, "ok")) != NULL) {
            //access OK
            print_to_LCD("OK", 2, 1, 0);
            beepOK();
        } else {
            //access denied
            print_to_LCD("DENIED", 6, 1, 0);
            beepDenied();
        }

    }

    // turn off the device
    *(base_address + CTRL) = POWER_OFF;
    /*}
    pciEnable(0, dev_enable);*/

    return 0;
}
