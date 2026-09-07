/**
 * @file    net_task.h
 * @brief   网络任务 — ESP8266 + OneNET MQTT + 天气 + 网络对时
 */
#ifndef __NET_TASK_H
#define __NET_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"

extern TaskHandle_t NetTask_Handler;

void NetTask_Create(void);

#ifdef __cplusplus
}
#endif

#endif /* __NET_TASK_H */
