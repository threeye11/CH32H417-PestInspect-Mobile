/**
 * @file    tts_task.c
 * @brief   TTS 语音播报任务
 *
 *          通过消息队列接收播报请求，驱动 TW-TTS 模块播放。
 *          其他任务调用 TTS_Speak() 异步发送播报请求。
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>
#include <stdio.h>
#include "shared.h"
#include "tw_tts.h"
#include "tts_task.h"
#include "weather.h"

/* ======================== 任务配置 ======================== */
#define TTS_TASK_PRIO       3
#define TTS_TASK_STK_SIZE   512

/* ======================== 队列配置 ======================== */
#define TTS_QUEUE_LEN       8

/* ======================== 播报时长估算 ======================== */
/* 中文语速4时，每字符约 180ms；英文/数字约 120ms；基线开销 500ms */
#define TTS_BASE_MS         500
#define TTS_CHAR_CN_MS      180
#define TTS_CHAR_EN_MS      120

TaskHandle_t TtsTask_Handler;
volatile uint8_t TtsReady = 0;
static QueueHandle_t TTS_Queue;
extern weather_info_t g_weather;

static uint8_t CalcWeekDay(uint16_t y, uint8_t m, uint8_t d)
{
    if (m < 3) { y--; m += 12; }
    int w = (d + 2*m + 3*(m+1)/5 + y + y/4 - y/100 + y/400 + 1) % 7;
    return (uint8_t)((w == 0) ? 7 : w);
}

static uint32_t TTS_EstimateDuration(const char *text)
{
    uint32_t ms = TTS_BASE_MS;
    const unsigned char *p = (const unsigned char *)text;
    while (*p)
    {
        if (*p < 0x80) ms += TTS_CHAR_EN_MS;
        else ms += TTS_CHAR_CN_MS;
        p++;
    }
    return ms;
}

static void tts_task(void *pvParameters)
{
    tts_request_t req;
    (void)pvParameters;

    printf("TTS: 初始化串口...\r\n");
    TW_TTS_Init();
    TtsReady = 1;

    vTaskDelay(pdMS_TO_TICKS(1000));

    TW_TTS_SetSpeed(4);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 主循环 */
    uint8_t last_tts_seq = SharedTtsData.tts_seq;
    uint8_t time_announced = 0;
    while (1)
    {
        /* 报时：网络时间有效后播报一次（异步，不阻塞初始化） */
        if (!time_announced && SharedTimeData.time_valid)
        {
            time_announced = 1;
            const char *week_str[] = {"", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"};
            char buf[96];
            uint8_t wk = CalcWeekDay(SharedTimeData.year, SharedTimeData.month, SharedTimeData.day);

            snprintf(buf, sizeof(buf), "现在是%d月%d日%d点%d分，%s",
                     SharedTimeData.month, SharedTimeData.day,
                     SharedTimeData.hour, SharedTimeData.minute,
                     week_str[wk]);
            vTaskDelay(pdMS_TO_TICKS(200));
            if (!SharedThresholdData.mute)
                TW_TTS_Play(buf);
            else
                printf("TTS: 静音模式，跳过报时\r\n");
            vTaskDelay(TTS_EstimateDuration(buf));

            /* 报时后播报天气 */
            if (!SharedThresholdData.mute && g_weather.valid)
            {
                vTaskDelay(pdMS_TO_TICKS(100));
                snprintf(buf, sizeof(buf), "今日天气%s，温度%s摄氏度",
                         g_weather.weather, g_weather.temp);
                TW_TTS_Play(buf);
                vTaskDelay(TTS_EstimateDuration(buf));
            }
        }

        /* 检查共享内存中的 TTS 请求（V3F 写入） */
        uint8_t current_tts_seq = SharedTtsData.tts_seq;
        if (current_tts_seq != last_tts_seq && SharedTtsData.tts_pending)
        {
            last_tts_seq = current_tts_seq;
            SharedTtsData.tts_pending = 0;
            if (SharedThresholdData.mute &&
                strstr((const char *)SharedTtsData.text, "静音") == NULL &&
                strstr((const char *)SharedTtsData.text, "已设") == NULL)
            {
                printf("TTS: 静音模式，跳过播报\r\n");
            }
            else if (SharedTtsData.text[0] != '\0')
            {
                uint32_t wait_ms;
                printf("TTS: 播报(共享) [%s]\r\n", (const char *)SharedTtsData.text);
                TW_TTS_Play((const char *)SharedTtsData.text);
                wait_ms = TTS_EstimateDuration((const char *)SharedTtsData.text);
                printf("TTS: 等待 %ums\r\n", wait_ms);
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
            }
        }

        /* 从队列取播报请求 */
        if (xQueueReceive(TTS_Queue, &req, pdMS_TO_TICKS(100)) == pdPASS)
        {
            if (SharedThresholdData.mute &&
                strstr(req.text, "静音") == NULL &&
                strstr(req.text, "已设") == NULL)
            {
                printf("TTS: 静音模式，跳过播报\r\n");
            }
            else if (req.text[0] != '\0')
            {
                uint32_t wait_ms;
                printf("TTS: 播报 [%s]\r\n", req.text);
                TW_TTS_Play(req.text);

                wait_ms = TTS_EstimateDuration(req.text);
                printf("TTS: 等待 %ums\r\n", wait_ms);
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
            }
        }
    }
}

void TtsTask_Create(void)
{
    TTS_Queue = xQueueCreate(TTS_QUEUE_LEN, sizeof(tts_request_t));
    xTaskCreate((TaskFunction_t)tts_task, (const char *)"tts",
                (uint16_t)TTS_TASK_STK_SIZE, (void *)NULL,
                (UBaseType_t)TTS_TASK_PRIO, (TaskHandle_t *)&TtsTask_Handler);
}

int TTS_Speak(const char *text)
{
    tts_request_t req;
    uint16_t len;
    if (text == NULL) return -1;
    len = strlen(text);
    if (len == 0 || len >= sizeof(req.text)) return -1;
    memset(&req, 0, sizeof(req));
    memcpy(req.text, text, len + 1);
    if (xQueueSend(TTS_Queue, &req, pdMS_TO_TICKS(100)) != pdPASS) return -1;
    return 0;
}
