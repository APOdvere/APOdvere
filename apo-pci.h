#ifndef _APO_SEM_H_
#define _APO_SEM_H_

// bus control bits
#define CTRL_WR			0x1 // write
#define CTRL_RD			0x2 // read
#define CTRL_CS0		0x4 // chip select
#define CTRL_PWR 		0x8 // power

#define KBD_BUZZ 		0x80 // buzzer

typedef struct {
    char line1[16];
    char line2[16];
} linesStruct;

#endif
