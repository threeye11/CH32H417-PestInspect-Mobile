/**
 * @file    k230.h
 * @brief   K230 视觉模块串口驱动 (USART5, PE2 RX AF4, PE3 TX AF11)
 *
 *          RX 帧 (K230→H417):
 *            病虫害帧: 0xAA 0x55 0x01 0x08 Data[8] CRC 0x55 0xAA (15字节)
 *            跟踪帧:   0xAA 0x55 0x03 0x10 Data[16] CRC 0x55 0xAA (23字节)
 *          TX 帧 (H417→K230): 0xAA 0x55 Cmd 0x55 0xAA (5字节)
 */
#ifndef __K230_H
#define __K230_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "ch32h417.h"
#include <stdint.h>
#include <stdbool.h>

/* ==================== 帧协议常量 ==================== */
#define K230_HEADER1        0xAA
#define K230_HEADER2        0x55
#define K230_FOOTER1        0x55
#define K230_FOOTER2        0xAA

/* 帧类型 */
#define K230_TYPE_PEST      0x01    /* 病虫害检测帧 */
#define K230_TYPE_HEARTBEAT 0x02    /* 心跳帧 */
#define K230_TYPE_TRACK     0x03    /* 人体跟踪帧 */
#define K230_TYPE_IMAGE     0x10    /* 图像帧 */

/* 数据长度 */
#define K230_PEST_DATA_LEN  8       /* 病虫害数据载荷 */
#define K230_TRACK_DATA_LEN 16      /* 跟踪数据载荷 */

/* 帧总长度 */
#define K230_PEST_FRAME_LEN   15    /* 2+1+1+8+1+2 */
#define K230_TRACK_FRAME_LEN  23    /* 2+1+1+16+1+2 */
#define K230_TX_FRAME_LEN     5     /* 2+1+2 */

/* ==================== K230 命令定义 (CH32→K230, 5字节帧) ==================== */
#define K230_CMD_START       0x01    /* 开始采集 */
#define K230_CMD_STOP        0x02    /* 停止采集 */
#define K230_CMD_MUTE_TX     0x03    /* 静默串口发送 */
#define K230_CMD_SWITCH_CROP 0x04    /* 切换作物模型 */
#define K230_CMD_MODE_PEST   0x05    /* 切换到病虫害模式 */
#define K230_CMD_MODE_TRACK  0x06    /* 切换到人体跟踪模式 */
#define K230_CMD_SERVO_HOME  0x07    /* 舵机归中 */
#define K230_CMD_SERVO_LEFT  0x08    /* 舵机左转 -15° */
#define K230_CMD_SERVO_RIGHT 0x09    /* 舵机右转 +15° */
#define K230_CMD_SERVO_DOWN  0x0A    /* 舵机下仰 -15° */
#define K230_CMD_SERVO_UP    0x0B    /* 舵机上仰 +15° */
#define K230_CMD_SERVO_PATROL 0x0C   /* 舵机巡逻模式 */
#define K230_CMD_CROP_CORN    0x0D    /* 直接切换到玉米 */
#define K230_CMD_CROP_POTATO  0x0E    /* 直接切换到马铃薯 */
#define K230_CMD_CROP_TOMATO  0x0F    /* 直接切换到番茄 */
#define K230_CMD_SEARCH_START 0x10    /* 触发搜索 */
#define K230_CMD_SEARCH_ABORT 0x11    /* 中止搜索/强制IDLE */

/* ==================== 接收缓冲区 ==================== */
#define K230_RX_BUF_SIZE  4096

/* ==================== 图像帧参数 ==================== */
#define K230_IMAGE_W          80
#define K230_IMAGE_H          60
#define K230_IMAGE_FRAME_SIZE (K230_IMAGE_W * K230_IMAGE_H * 2)  /* 9600 bytes */
#define K230_IMAGE_MAX_CHUNKS 64
#define K230_IMAGE_CHUNK_MAX  256   /* destuff 缓冲大小 */

/* ==================== 数据结构 ==================== */

/* 病虫害检测结果 */
typedef struct {
    uint8_t  pest_type;             /* 虫害类型 ID */
    uint8_t  confidence;            /* 置信度 0-100 */
    uint16_t recognition_count;     /* 累计识别次数 (LE) */
    uint8_t  crop_type;             /* 作物类型 0=玉米 1=马铃薯 */
} k230_pest_data_t;

/* 人体跟踪结果 */
typedef struct {
    uint16_t x;                     /* 左上角 X */
    uint16_t y;                     /* 左上角 Y */
    uint16_t w;                     /* 宽度 */
    uint16_t h;                     /* 高度 */
    int16_t  dx;                    /* 中心 X 偏移 (有符号) */
    int16_t  dy;                    /* 中心 Y 偏移 (有符号) */
    uint8_t  confidence;            /* 置信度 0-100 */
    uint8_t  track_id;              /* 跟踪目标 ID */
    uint8_t  status;                /* 0x01=锁定 0x00=丢失 */
    uint8_t  reserved;
} k230_track_data_t;

/* 通用接收帧 */
typedef struct {
    uint8_t  frame_type;            /* K230_TYPE_PEST 或 K230_TYPE_TRACK */
    uint8_t  data_len;              /* 8 或 16 */
    union {
        k230_pest_data_t  pest;
        k230_track_data_t track;
    } data;
} k230_rx_frame_t;

/* ==================== 公开接口 ==================== */
void K230_Init(uint32_t baudrate);
void K230_DeInit(void);
void K230_SendCmd(uint8_t cmd);
bool K230_GetResult(k230_rx_frame_t *frame);
uint8_t K230_GetSeq(void);
uint16_t K230_GetErrorCount(void);
uint32_t K230_GetByteCount(void);
uint16_t K230_GetBufUsage(void);
const char* K230_GetFrameTypeName(uint8_t type);
const char* K230_GetCmdName(uint8_t cmd);

#ifdef __cplusplus
}
#endif

#endif
