/**
 * @file    init_task.h
 * @brief   初始化任务 — LVGL + RTC + 显示驱动初始化
 */
#ifndef __INIT_TASK_H
#define __INIT_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"

extern TaskHandle_t InitTask_Handler;

void InitTask_Create(void);

#ifdef __cplusplus
}
#endif

#endif /* __INIT_TASK_H */
