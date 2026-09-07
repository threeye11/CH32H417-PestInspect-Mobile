/**
 * @file    sensor_task.h
 * @brief   传感器采集任务 — BH1750 光照 + SHT30 温湿度
 */
#ifndef __SENSOR_TASK_H
#define __SENSOR_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"

extern TaskHandle_t SensorTask_Handler;

void SensorTask_Create(void);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_TASK_H */
