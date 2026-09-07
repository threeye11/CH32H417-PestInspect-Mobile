/**
 * @file    led_task.h
 * @brief   LED 闪烁任务
 */
#ifndef __LED_TASK_H
#define __LED_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"

extern TaskHandle_t LedBlinkTask_Handler;

void LedTask_Create(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED_TASK_H */
