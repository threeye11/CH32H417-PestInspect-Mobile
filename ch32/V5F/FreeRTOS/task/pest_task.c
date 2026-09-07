/**
 * @file    pest_task.c
 * @brief   K230 病害检测任务 — 轮询检测结果、查询知识库、TTS 播报
 *
 *          从 net_task 中拆分出来，独立管理 K230 检测结果的处理流程。
 *          K230 命令发送（k230_cmd_seq → K230_SendCmd）仍由 net_task 负责，
 *          因为命令来源于云端/V3F GUI，通过共享内存传递。
 *
 *          防重复机制：
 *          1. 需连续 5 帧同一病害（滤除单帧误检）
 *          2. 播报后 30 秒冷却期内同病害不再播报
 *          3. 切换病害类型时重置计数
 *          4. 超时 5 秒无数据自动清除过期结果
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

#include "shared.h"
#include "k230.h"
#include "disease_db.h"
#include "tts_task.h"
#include "onenet.h"

/* ======================== 任务配置 ======================== */
#define PEST_TASK_PRIO      2       /* 低于 net_task(4) 和 lvgl_task(3) */
#define PEST_TASK_STK_SIZE  1024

/* ======================== 检测参数 ======================== */
#define PEST_CONSEC_COUNT   5       /* 连续检测次数阈值 */
#define PEST_COOLDOWN_MS    30000   /* 冷却期 30 秒 */
#define PEST_TIMEOUT_CNT    1000    /* 超时计数（1000 × 5ms = 5s） */
#define PEST_IDLE_DELAY_MS  100     /* 空闲时休眠间隔 */

/* ======================== 任务句柄 ======================== */
TaskHandle_t PestTask_Handler;

/* ======================== 静态辅助函数 ======================== */

/**
 * @brief  将 K230 pest_type 映射为中文植物名和病害名
 * @return  true=有效映射, false=未知类型
 */
static bool pest_map_type(uint8_t pest_type, const char **plant, const char **pest)
{
    switch (pest_type) {
        case 0:  *plant = "玉米"; *pest = "非生物病害";     return true;
        case 1:  *plant = "玉米"; *pest = "蚜虫";           return true;
        case 2:  *plant = "玉米"; *pest = "弯孢霉叶斑病";   return true;
        case 4:  *plant = "玉米"; *pest = "蠕孢菌叶斑病";   return true;
        case 5:  *plant = "玉米"; *pest = "健康";           return true;
        case 6:  *plant = "玉米"; *pest = "锈病";           return true;
        case 7:  *plant = "玉米"; *pest = "草地贪夜蛾";     return true;
        case 8:  *plant = "玉米"; *pest = "草地贪夜蛾";     return true;
        case 9:  *plant = "玉米"; *pest = "条纹病";         return true;
        default: *plant = "未知"; *pest = "";               return false;
    }
}

/**
 * @brief  播报病害检测结果（知识库查询 + TTS）
 * @param  pest_name  病害中文名
 */
static void pest_announce(const char *pest_name)
{
    const DiseaseEntry *de = DiseaseDB_Find(pest_name);
    if (de) {
        /* 播报检测结果 + 应对措施 */
        TTS_Speak(de->tts_announce);
        /* 有推荐药物时追加播报 */
        if (de->drug[0] != '\0' && strncmp(de->drug, "无", 2) != 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            char drug_buf[64];
            snprintf(drug_buf, sizeof(drug_buf), "推荐药物：%s", de->drug);
            TTS_Speak(drug_buf);
        }
    } else {
        /* 知识库未覆盖，仅播报检测结果 */
        char tts_buf[48];
        snprintf(tts_buf, sizeof(tts_buf), "检测到%s", pest_name);
        TTS_Speak(tts_buf);
    }
}

/* ======================== 任务主函数 ======================== */
static void pest_task(void *pvParameters)
{
    (void)pvParameters;

    /* 状态变量 */
    uint8_t  last_seq = 0;
    uint32_t timeout_cnt = 0;
    uint8_t  pest_consec_count = 0;
    uint8_t  pest_announced_type = 0xFF;
    uint8_t  pest_last_type = 0xFF;
    TickType_t pest_announced_tick = 0;

    printf("PEST: 任务启动\r\n");

    while (1)
    {
        /* ---- 检测未启动时休眠 ---- */
        if (SharedPestData.pest_detect_cmd == 0) {
            /* 清空 K230 接收缓冲区 */
            while (K230_GetResult(NULL));
            last_seq = K230_GetSeq();
            timeout_cnt = 0;
            pest_last_type = 0xFF;
            pest_consec_count = 0;
            pest_announced_type = 0xFF;
            vTaskDelay(pdMS_TO_TICKS(PEST_IDLE_DELAY_MS));
            continue;
        }

        /* ---- 轮询 K230 数据 ---- */
        k230_rx_frame_t frame;
        if (K230_GetResult(&frame)) {
            uint8_t s = K230_GetSeq();
            if (s != last_seq) {
                last_seq = s;
                timeout_cnt = 0;

                /* ========== 病虫害检测帧 ========== */
                if (frame.frame_type == K230_TYPE_PEST) {
                    const k230_pest_data_t *p = &frame.data.pest;
                    const char *pn, *dn;
                    pest_map_type(p->pest_type, &pn, &dn);

                    /* 写入共享内存（V3F GUI + 云端上报） */
                    strncpy((char *)SharedPestData.plant, pn, 15);
                    SharedPestData.plant[15] = '\0';
                    if (p->confidence == 0) {
                        SharedPestData.pest[0] = '\0';
                    } else {
                        strncpy((char *)SharedPestData.pest, dn, 15);
                        SharedPestData.pest[15] = '\0';
                    }
                    SharedPestData.confidence = p->confidence;
                    SharedPestData.pest_valid = 1;
                    SharedPestData.pest_seq++;
                    SharedPestData.k230_mode = 0;
                    Pest_SetResult(pn, (p->confidence == 0) ? "" : dn,
                                   p->confidence);

                    /* 连续检测 + 防重复播报 */
                    if (p->confidence > 0 &&
                        p->pest_type != 0 &&   /* 非生物病害跳过 */
                        p->pest_type != 5) {   /* 健康跳过 */
                        TickType_t now_tick = xTaskGetTickCount();

                        /* 冷却期内不重复播报 */
                        if (p->pest_type == pest_announced_type &&
                            (now_tick - pest_announced_tick) <
                            pdMS_TO_TICKS(PEST_COOLDOWN_MS)) {
                            /* 冷却中，跳过 */
                        }
                        /* 同类型连续检测计数 */
                        else if (p->pest_type == pest_last_type) {
                            pest_consec_count++;
                            if (pest_consec_count >= PEST_CONSEC_COUNT) {
                                pest_announced_type = p->pest_type;
                                pest_announced_tick = xTaskGetTickCount();
                                pest_announce(dn);
                                pest_consec_count = 0;
                            }
                        }
                        /* 新病害类型，重置计数 */
                        else {
                            pest_last_type = p->pest_type;
                            pest_consec_count = 1;
                            pest_announced_type = 0xFF;
                        }
                    } else {
                        /* 健康或非生物胁迫，重置状态 */
                        pest_last_type = 0xFF;
                        pest_consec_count = 0;
                    }
                }
                /* ========== 人体跟踪帧 ========== */
                else if (frame.frame_type == K230_TYPE_TRACK) {
                    const k230_track_data_t *t = &frame.data.track;
                    SharedPestData.track_dx = t->dx;
                    SharedPestData.track_dy = t->dy;
                    SharedPestData.track_confidence = t->confidence;
                    SharedPestData.track_status = t->status;
                    SharedPestData.track_id = t->track_id;
                    SharedPestData.track_valid = 1;
                    SharedPestData.track_seq++;
                    SharedPestData.k230_mode = 1;
                }
            }
        } else {
            /* ---- 无数据，超时检测 ---- */
            timeout_cnt++;
            if (timeout_cnt >= PEST_TIMEOUT_CNT) {
                if (SharedPestData.pest_valid) {
                    SharedPestData.plant[0] = '-';
                    SharedPestData.plant[1] = '\0';
                    SharedPestData.pest[0] = '-';
                    SharedPestData.pest[1] = '\0';
                    SharedPestData.confidence = 0;
                    SharedPestData.pest_valid = 0;
                    SharedPestData.pest_seq++;
                    Pest_SetResult("-", "-", 0);
                }
                if (SharedPestData.track_valid) {
                    SharedPestData.track_valid = 0;
                    SharedPestData.track_status = 0;
                }
                /* 超时重置播报状态 */
                pest_last_type = 0xFF;
                pest_consec_count = 0;
                pest_announced_type = 0xFF;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ======================== 公开接口 ======================== */
void PestTask_Create(void)
{
    xTaskCreate((TaskFunction_t)pest_task,
                (const char *)"pest",
                (uint16_t)PEST_TASK_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)PEST_TASK_PRIO,
                (TaskHandle_t *)&PestTask_Handler);
}
