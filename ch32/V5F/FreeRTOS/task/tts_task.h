/**
 * @file    tts_task.h
 * @brief   TTS 语音播报任务
 */
#ifndef __TTS_TASK_H
#define __TTS_TASK_H

#include "FreeRTOS.h"
#include "task.h"

typedef struct {
    char     text[128];
} tts_request_t;

void TtsTask_Create(void);
int  TTS_Speak(const char *text);

extern TaskHandle_t TtsTask_Handler;
extern volatile uint8_t TtsReady;

#endif
