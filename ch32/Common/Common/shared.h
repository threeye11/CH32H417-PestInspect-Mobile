/********************************** (C) COPYRIGHT  *******************************
* File Name          : shared.h
* Author             : WCH
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : This file contains all the functions prototypes for the 
*                      sharing.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#ifndef __SHARED_H
#define __SHARED_H

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdint.h>

/* ---- 传感器数据共享结构体（V3F 写入，V5F 读取） ---- */
typedef struct {
    uint8_t  seq;              /* 序列号，每次更新 +1 */
    uint8_t  valid;            /* 1=数据有效，0=未初始化 */
    uint16_t light;            /* 光照强度 (lux) */
    int16_t  temperature_x10;  /* 温度 * 10 (242 = 24.2°C) */
    int16_t  humidity_x10;     /* 湿度 * 10 (455 = 45.5%) */
    uint8_t  bh1750_ok;        /* 1=BH1750正常, 0=异常 */
    uint8_t  sht30_ok;         /* 1=SHT30正常, 0=异常 */
} shared_sensor_data_t;

/* ---- 网络时间共享结构体（V5F 写入，V3F 读取用于 RTC 对时） ---- */
typedef struct {
    uint8_t  time_seq;         /* 序列号，每次更新 +1 */
    uint8_t  time_valid;       /* 1=时间有效，0=未同步 */
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  week;             /* 星期几 1-7 */
    uint8_t  wifi_ok;          /* 1=WiFi已连接, 0=断开 */
    uint8_t  time_reserved2;
    uint16_t time_reserved;
} shared_time_data_t;

/* ---- 天气数据共享结构体（V5F 写入，V3F 读取更新 LVGL） ---- */
typedef struct {
    uint8_t  weather_seq;      /* 序列号，每次更新 +1 */
    uint8_t  weather_valid;    /* 1=天气有效，0=未获取 */
    char     city[16];         /* 城市名 */
    char     weather[16];      /* 天气描述 */
    int8_t   temp_outdoor;     /* 室外温度 */
    uint16_t weather_reserved;
} shared_weather_data_t;

extern volatile uint8_t Buffer_Sharing[4];
extern volatile uint32_t Data_Sharing;
extern volatile shared_sensor_data_t SharedSensorData;
extern volatile shared_time_data_t SharedTimeData;
extern volatile shared_weather_data_t SharedWeatherData;

/* ---- 设备控制共享结构体（V5F 写入，V3F 读取控制硬件） ---- */
typedef struct {
    uint8_t  device_seq;        /* 序列号，每次更新 +1 */
    uint8_t  led;               /* 1=开，0=关 */
    uint8_t  buzzer;            /* 1=开，0=关 */
    uint8_t  pump;              /* 1=开，0=关 */
} shared_device_data_t;

extern volatile shared_device_data_t SharedDeviceData;

/* ---- 阈值设置共享结构体（V5F 写入，V3F 读取用于超限告警） ---- */
typedef struct {
    uint8_t  threshold_seq;     /* 序列号，每次更新 +1 */
    uint16_t temp_threshold;    /* 温度下限 (°C) */
    uint16_t humi_threshold;    /* 湿度下限 (%) */
    uint16_t light_threshold;   /* 光照下限 (lux) */
    uint8_t  mute;              /* 1=静音模式, 0=正常 */
    uint8_t  tts_volume;        /* TTS音量 0-9，默认5 */
    uint8_t  tts_tone;          /* TTS语调 0-9，默认5 */
    uint8_t  tts_speed;         /* TTS语速 0-9，默认5 */
    uint16_t log_interval_s;    /* 日志保存间隔（秒），默认60，范围10-1800 */
} shared_threshold_data_t;

extern volatile shared_threshold_data_t SharedThresholdData;

/* ---- 病害检测 + K230 视觉共享结构体 ---- */
typedef struct {
    uint8_t  pest_seq;         /* 序列号，每次新检测 +1 */
    uint8_t  pest_valid;       /* 1=检测结果有效，0=无数据 */
    char     plant[16];        /* 植物名称，如 "玉米" */
    char     pest[16];         /* 病害名称，如 "蚜虫"，空串=健康 */
    uint8_t  confidence;       /* 置信度 0~100 */
    uint8_t  pest_detect_cmd;  /* V3F→V5F: 1=开始检测, 0=停止检测 */
    uint8_t  k230_cmd;         /* V3F→V5F: K230 命令字节 (0x01-0x11) */
    uint8_t  k230_cmd_seq;     /* 命令序列号，每次新命令 +1 */
    /* ---- 跟踪数据 (V5F 写入，V3F 读取) ---- */
    uint8_t  track_seq;        /* 跟踪数据序列号，每次更新 +1 */
    uint8_t  track_valid;      /* 1=跟踪数据有效，0=无数据 */
    uint8_t  track_status;     /* 0x01=锁定 0x00=丢失 */
    uint8_t  track_confidence; /* 跟踪置信度 0~100 */
    int16_t  track_dx;         /* 中心 X 偏移 (有符号) */
    int16_t  track_dy;         /* 中心 Y 偏移 (有符号) */
    uint8_t  track_id;         /* 跟踪目标 ID */
    uint8_t  k230_mode;        /* 当前模式: 0=病虫害 1=跟踪 */
} shared_pest_data_t;

extern volatile shared_pest_data_t SharedPestData;

/* ---- TTS 语音播报共享结构体（V3F 写入，V5F 读取播报） ---- */
typedef struct {
    uint8_t  tts_seq;         /* 序列号，每次更新 +1 */
    uint8_t  tts_pending;     /* 1=有播报请求，0=无请求 */
    char     text[128];       /* 播报文本 */
    uint16_t tts_reserved;
} shared_tts_data_t;

extern volatile shared_tts_data_t SharedTtsData;

/* ---- 小车控制共享结构体（V3F 写入，V5F 读取执行） ---- */
typedef struct {
    uint8_t  car_seq;          /* 序列号，每次更新 +1 */
    uint8_t  car_cmd;          /* 控制命令: 0=无 1=前进 2=后退 3=左转 4=右转 5=停止 */
    uint8_t  car_mode;         /* 模式: 0=避障 1=手动 2=跟随 */
    uint8_t  car_speed;        /* 速度档位: 1-4 */
    uint16_t car_reserved;
} shared_car_data_t;

extern volatile shared_car_data_t SharedCarData;

/* ---- 图像传输共享结构体（V5F 写入，V3F 读取显示） ---- */
#define IMAGE_BUF_SIZE (80 * 60 * 2)  /* 9600 bytes: 80x60 RGB565 */

typedef struct {
    uint8_t  img_seq;                     /* 序列号，每新帧 +1 */
    uint8_t  img_valid;                   /* 1=新帧就绪，0=无数据 */
    uint16_t img_reserved;
    uint8_t  img_data[IMAGE_BUF_SIZE];    /* 80x60 RGB565 像素数据 */
} shared_image_data_t;

extern volatile shared_image_data_t SharedImageData;

/* ---- WiFi 配网共享结构体（V3F 写入，V5F 读取执行连接） ---- */
typedef struct {
    uint8_t  wifi_cmd_seq;    /* V3F→V5F: 配网命令序号，变化时触发重连 */
    uint8_t  wifi_connected;  /* V5F→V3F: 1=WiFi已连接, 0=断开 */
    char     ssid[33];        /* WiFi名称，最长32字符+结束符 */
    char     password[65];    /* WiFi密码，最长64字符+结束符 */
} shared_wifi_data_t;

extern volatile shared_wifi_data_t SharedWiFiData;

/* ---- 传感器日志传输共享结构体（V3F 请求，V5F 响应） ---- */
#define SHARED_LOG_PAGE_SIZE  8   /* 每页传输条数（8×16=128字节） */

typedef struct {
    /* V3F→V5F 请求 */
    uint8_t  log_req_seq;      /* 请求序列号，变化时 V5F 读取新页 */
    uint8_t  log_page;         /* 请求页码（0=最新页） */
    uint8_t  log_req_count;    /* 每页请求条数（≤SHARED_LOG_PAGE_SIZE） */
    uint8_t  save_sensor_log;  /* V3F→V5F: 1=立即保存当前传感器数据到Flash */
    /* V5F→V3F 响应 */
    uint8_t  log_resp_seq;     /* 响应序列号，V5F 填充完成后 +1 */
    uint16_t log_total;        /* 日志总条数 */
    uint16_t log_resp_reserved;
    /* 日志数据（V5F 填充，V3F 读取） */
    uint8_t  log_data[SHARED_LOG_PAGE_SIZE * 16]; /* SensorLog_t×8 = 128字节 */
} shared_log_data_t;

extern volatile shared_log_data_t SharedLogData;

/* ---- 天气地点设置共享结构体（V3F 写入，V5F 读取执行） ---- */
typedef struct {
    uint8_t  weather_cmd_seq;   /* V3F→V5F: 命令序号，变化时触发地点更新 */
    uint8_t  weather_cmd_status; /* V5F→V3F: 0=空闲 1=处理中 2=成功 3=失败 */
    char     location[33];      /* 城市拼音，如 "nanning" */
    char     location_cn[16];   /* V5F→V3F: 中文城市名，如 "南宁" */
    uint16_t weather_cmd_reserved;
} shared_weather_cmd_t;

extern volatile shared_weather_cmd_t SharedWeatherCmd;

/* ---- 触摸校准数据共享结构体（V3F 写入，V5F 读取保存到 W25Q64） ---- */
typedef struct {
    uint8_t  cal_seq;           /* V3F→V5F: 校准命令序号，变化时触发保存 */
    uint8_t  cal_status;        /* V5F→V3F: 0=空闲 1=已加载 2=保存完成 */
    float    xfac;              /* X 轴比例因子 */
    float    yfac;              /* Y 轴比例因子 */
    short    xc;                /* 中心点 X 原始 ADC 值 */
    short    yc;                /* 中心点 Y 原始 ADC 值 */
    uint8_t  cal_reserved[2];
} shared_cal_data_t;

extern volatile shared_cal_data_t SharedCalData;


#ifdef __cplusplus
}
#endif

#endif 