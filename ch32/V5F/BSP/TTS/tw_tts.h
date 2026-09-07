/**
 * @file    tw_tts.h
 * @brief   TW-TTS 文字转语音模块驱动（串口通信）
 *
 *          帧格式：FD + 数据长度(2B) + 命令字(1B) + 编码参数(1B) + 文本
 *          数据长度 = 命令字(1) + 编码参数(1) + 文本长度 = text_len + 2
 *          默认波特率 9600，8N1
 */

#ifndef __TW_TTS_H
#define __TW_TTS_H

#include "ch32h417.h"
#include <stdint.h>

/* ---- 帧头 ---- */
#define TTS_FRAME_HEADER        0xFD

/* ---- 命令字 ---- */
#define TTS_CMD_SPEAK           0x01
#define TTS_CMD_STOP            0x02
#define TTS_CMD_PAUSE           0x03
#define TTS_CMD_RESUME          0x04
#define TTS_CMD_QUERY           0x21

/* ---- 编码参数 ---- */
#define TTS_ENCODE_GB2312       0x00
#define TTS_ENCODE_UTF8         0x04

/* ---- 模块返回状态 ---- */
#define TTS_STATE_PLAYING       0x4E
#define TTS_STATE_IDLE          0x4F

/* ---- 公开接口 ---- */
void TW_TTS_Init(void);
void TW_TTS_Play(const char *str);
void TW_TTS_Stop(void);
void TW_TTS_Pause(void);
void TW_TTS_Resume(void);
void TW_TTS_SetSpeed(uint8_t speed);  /* 0-9，0最慢 9最快，默认5 */
void TW_TTS_SetVolume(uint8_t vol);   /* 0-9，0最小 9最大，默认5 */
void TW_TTS_SetTone(uint8_t tone);    /* 0-9，0最低 9最高，默认5 */

#endif /* __TW_TTS_H */
