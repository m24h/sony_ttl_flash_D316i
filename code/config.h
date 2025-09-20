// lib config, do not change these if hardware is not changed
#define MCU_STC8H1K08
#define FOSC 22118400

// ports config, do not change these if hardware is not changed
#define MA     P11
#define MB     P15
#define POSA_N P13
#define POSB_N P12
#define VHCTL  P16
#define ILLU   P17
#define LED    P54
#define FIRE_N P14
#define PON_N  P37
#define PHOLD  P36
#define RXD    P30
#define TXD    P31
#define ADC_VHSAMP_CH 0 // ADC channel

// flash config
#define FLASH_F1SYNC    P33
#define FLASH_F2DAT     P34
#define FLASH_F3CLK     P32
#define FLASH_FIRE_N    P14
#define FLASH_LEVEL_FULL 12 // GN16
#define FLASH_LOW_FLASH_ENHANCE

// main config
#define DEBUG       2       // debug level, 0 means no debug
#define BAUDRATE    9600

/*
V_sample = V_high * VH_RATIO + VH_BIAS, if VHCTL==1
*/
#define VH_RATIO     (5.1/665.1)
#define VH_BIAS      0
#define VH_LEAST     280     // 260V is the lowest fire voltage, but for P-TTL (double flash), 280V seems to be more stable
#define VH_DEST      300
#define ADC_RATE     100000  // 10us each time
#define ADC_REF_MV   1190
