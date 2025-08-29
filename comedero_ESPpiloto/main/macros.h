#ifndef MACROS_H
#define MACROS_H

#define ID_COMEDERO         0x01
#define ID_GRANJA           0x00
#define ID_USUARIO          0x01


#define TRUE                1
#define FALSE               0

#define OK                  (1)
#define ERROR               (-1)
#define BUSY                (0)

#define SEG                 1000000
#define MICROSEG            1000

#define NUM_BOCAS           5
#define NUM_EVENTOS         100
#define NUM_REGISTROS       3000
#define SEND_BUFFER_SIZE    10
#define MAX_TIME_TRIES      2

#define SENSE_2             33

#define REDLED              14
#define WHITELED            2             

#define DEBOUNCE_TIME       200

// WIFI
#define SSID "danianto"
#define PASWORD "danianto"
//#define SSID "Mensoft_corp"
//#define PASWORD "Mensoft2025"
//#define SSID "MOVISTAR_F2FC"
//#define PASWORD "76D04A5594CC9068B3B7"
//#define SSID "iPhone de Mario"
//#define PASWORD "11111111"

// MACROS SD
#define PWR_SD 15 
#define MOUNT_POINT "/sdcard"
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

// MACROS RTC
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_SCL_IO 22
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TIMEOUT_MS 1000

#define DS3231_ADDR 0x68

typedef struct
{
    uint8_t idComedero;
    uint64_t idAnimal;
    uint32_t timeStamp;
    uint32_t segComiendo;
    uint8_t tm_mday;
    uint8_t tm_mon;
    uint8_t tm_year;
    uint8_t tm_hour;
    uint8_t tm_min;
    uint8_t tm_sec;
} evento;

struct buf_t
{
    uint16_t head;
    uint16_t tail;
    uint16_t max;
    uint8_t full;
}pigbuf;
#endif