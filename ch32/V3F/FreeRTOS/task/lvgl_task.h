/**
 * @file    lvgl_task.h
 * @brief   LVGL 显示任务 — 时间更新 + 天气更新 + 传感器数据显示
 */
#ifndef __LVGL_TASK_H
#define __LVGL_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"

extern TaskHandle_t LvglTask_Handler;

void LvglTask_Create(void);

#ifdef __cplusplus
}
#endif

#endif /* __LVGL_TASK_H */
