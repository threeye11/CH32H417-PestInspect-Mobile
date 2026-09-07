/**
 * @file    pest_task.h
 * @brief   K230 病害检测任务 — 轮询 K230 检测结果、查询知识库、TTS 播报
 *
 *          职责：
 *          - 轮询 K230_GetResult() 获取病虫害/跟踪帧
 *          - 病害类型映射（pest_type → 中文名称）
 *          - 写入 SharedPestData 供 V3F GUI 和云端上报
 *          - 查询本地知识库 DiseaseDB_Find() 获取应对措施
 *          - 通过 TTS_Speak() 播报检测结果和推荐药物
 *          - 连续检测去重 + 冷却期防重复触发
 *          - 超时自动清除过期数据
 */
#ifndef __PEST_TASK_H
#define __PEST_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"

extern TaskHandle_t PestTask_Handler;

void PestTask_Create(void);

#ifdef __cplusplus
}
#endif

#endif /* __PEST_TASK_H */
