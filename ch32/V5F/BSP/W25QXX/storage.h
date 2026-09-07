#ifndef __STORAGE_H
#define __STORAGE_H

#include <stdint.h>

/* 用户设置结构体（写入 Flash，16字节） */
typedef struct {
    uint16_t magic;            /* 0xA55A */
    uint16_t version;          /* 2 */
    uint16_t temp_threshold;
    uint16_t humi_threshold;
    uint16_t light_threshold;
    uint8_t  mute;             /* 1=静音模式 */
    uint8_t  tts_volume;       /* TTS音量 0-9 */
    uint8_t  tts_tone;         /* TTS语调 0-9 */
    uint8_t  tts_speed;        /* TTS语速 0-9 */
    uint16_t log_interval_s;   /* 日志保存间隔（秒），默认60 */
    uint16_t crc;
} __attribute__((packed)) UserSettings_t;

/* 传感器日志记录（16字节/条，对齐256避免页写跨页） */
typedef struct {
    uint8_t  year_off;         /* 相对2024的偏移 */
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    int16_t  temp_x10;         /* 温度×10（如 264 = 26.4°C），-32768=异常 */
    uint16_t humi_x10;         /* 湿度×10（如 544 = 54.4%），0xFFFF=异常 */
    uint8_t  light_h;          /* 光照高字节，0xFF=异常 */
    uint8_t  light_l;          /* 光照低字节 */
    uint8_t  reserved[5];      /* 填充至16字节，防止页写跨页 */
} __attribute__((packed)) SensorLog_t;

/* 日志区索引头（16字节，存在日志区首地址） */
typedef struct {
    uint32_t magic;            /* 0xLOG1 */
    uint32_t write_idx;        /* 下一条写入位置（从0开始） */
    uint32_t count;            /* 已写入总条数 */
    uint32_t crc;
} __attribute__((packed)) LogIndex_t;

/* WiFi 凭据结构体（写入 Flash，100字节，存放在独立扇区） */
typedef struct {
    uint16_t magic;            /* 0xBEEF */
    uint16_t version;          /* 1 */
    char     ssid[33];         /* WiFi名称 */
    char     password[65];     /* WiFi密码 */
    uint16_t crc;
} __attribute__((packed)) WiFiSettings_t;

/* 天气地点结构体（写入 Flash，40字节） */
typedef struct {
    uint16_t magic;            /* 0xBEAD */
    uint16_t version;          /* 1 */
    char     location[33];     /* 天气查询地点，如 "nanning" */
    uint16_t crc;
} __attribute__((packed)) WeatherSettings_t;

/* 触摸校准数据结构体（写入 Flash，16字节） */
typedef struct {
    uint32_t magic;            /* 0x54435031 */
    float    xfac;             /* X 轴比例因子 */
    float    yfac;             /* Y 轴比例因子 */
    short    xc;               /* 中心点 X 原始 ADC 值 */
    short    yc;               /* 中心点 Y 原始 ADC 值 */
    uint32_t crc;
} __attribute__((packed)) TPCalData_t;

#define TP_CAL_MAGIC    0x54435031   /* "TPC1" */

/* API */
void     Storage_InitSPI(void);   /* 仅初始化 SPI3，用于提前读取 WiFi 凭据 */
void     Storage_Init(void);      /* 完整初始化（含日志索引） */
_Bool    Storage_LoadSettings(UserSettings_t *settings);
void     Storage_SaveSettings(const UserSettings_t *settings);
_Bool    Storage_LoadWiFi(WiFiSettings_t *wifi);
void     Storage_SaveWiFi(const WiFiSettings_t *wifi);
_Bool    Storage_LoadWeather(WeatherSettings_t *weather);
void     Storage_SaveWeather(const WeatherSettings_t *weather);
void     Storage_SaveSensorLog(int16_t temp_x10, uint16_t humi_x10,
                               uint16_t light, uint8_t year_off,
                               uint8_t month, uint8_t day,
                               uint8_t hour, uint8_t minute);
uint32_t Storage_GetLogCount(void);
void     Storage_ReadLogs(uint32_t start, uint32_t count, SensorLog_t *buf);
void     Storage_ClearLogs(void);
_Bool    Storage_LoadTPCal(TPCalData_t *cal);
void     Storage_SaveTPCal(const TPCalData_t *cal);

#endif
