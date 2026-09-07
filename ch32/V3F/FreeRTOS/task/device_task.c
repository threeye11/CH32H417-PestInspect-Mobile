/**
 * @file    device_task.c
 * @brief   V3F 设备控制任务
 *
 *          轮询 SharedDeviceData，通过 alarm.h 宏控制硬件
 *          同时监测传感器阈值，超限时自动告警（5秒后停止）并语音提醒
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "shared.h"
#include "alarm.h"
#include "device_task.h"
#include <string.h>

/* 外部引用 */
extern volatile uint8_t lvgl_ready;

/* 告警配置 */
#define ALARM_BLINK_MS    250        /* 告警闪烁间隔（250ms = 500ms 周期） */
#define ALARM_DURATION_MS 5000       /* 告警持续时间 5 秒 */

static void device_task(void *pvParameters)
{
    (void)pvParameters;

    alarm_init();

    SharedDeviceData.device_seq = 0;
    SharedDeviceData.led = 0;
    SharedDeviceData.buzzer = 0;
    SharedDeviceData.pump = 0;

    printf("V3F: 设备控制任务启动 (PF0蜂鸣器 PF1 LED PF2水泵)\r\n");

    /* 等待传感器初始化完成 AND LVGL 界面就绪 */
    printf("V3F: 等待传感器和LVGL就绪...\r\n");
    while (!SharedSensorData.valid || !lvgl_ready)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("V3F: 传感器和LVGL就绪，设备控制任务开始\r\n");

    uint8_t last_seq = 0;
    uint8_t last_threshold_seq = 0;

    /* 告警状态 */
    uint8_t alarm_active = 0;           /* 当前是否正在告警 */
    uint8_t alarm_blink_state = 0;      /* 告警闪烁状态 */
    uint32_t alarm_start_tick = 0;      /* 告警开始时间 */

    /* 上次检测状态（用于检测"从正常变为低于阈值"的边沿） */
    uint8_t last_temp_alarm = 0;
    uint8_t last_humi_alarm = 0;
    uint8_t last_light_alarm = 0;

    while (1)
    {
        /* ---- 1. 处理云端设备控制命令（优先级最高） ---- */
        uint8_t cur_seq = SharedDeviceData.device_seq;
        if (cur_seq != last_seq)
        {
            last_seq = cur_seq;

            if (SharedDeviceData.buzzer) BUZZER_ON
            else                         BUZZER_OFF

            if (SharedDeviceData.led)    LIGHT_ON
            else                         LIGHT_OFF

            if (SharedDeviceData.pump)   PUMP_ON
            else                         PUMP_OFF
        }

        /* ---- 2. 检查传感器阈值告警 ---- */
        uint8_t th_seq = SharedThresholdData.threshold_seq;
        if (th_seq != last_threshold_seq)
        {
            last_threshold_seq = th_seq;
        }

        /* 检测三个值各自是否低于阈值（当前状态） */
        uint8_t cur_temp_alarm = 0;
        uint8_t cur_humi_alarm = 0;
        uint8_t cur_light_alarm = 0;

        if (SharedSensorData.valid)
        {
            /* 温度低于阈值 */
            if (SharedThresholdData.temp_threshold > 0 &&
                SharedSensorData.temperature_x10 < (int16_t)(SharedThresholdData.temp_threshold * 10))
            {
                cur_temp_alarm = 1;
            }

            /* 湿度低于阈值 */
            if (SharedThresholdData.humi_threshold > 0 &&
                SharedSensorData.humidity_x10 < (int16_t)(SharedThresholdData.humi_threshold * 10))
            {
                cur_humi_alarm = 1;
            }

            /* 光照低于阈值 */
            if (SharedThresholdData.light_threshold > 0 &&
                SharedSensorData.light < SharedThresholdData.light_threshold)
            {
                cur_light_alarm = 1;
            }
        }

        /* 检测边沿：从正常变为低于阈值（下降沿触发告警） */
        uint8_t trigger_alarm = 0;
        char tts_text[64] = {0};

        if (cur_temp_alarm && !last_temp_alarm)
        {
            trigger_alarm = 1;
            snprintf(tts_text, sizeof(tts_text), "注意，温度低于阈值");
            printf("V3F: 温度低于阈值，触发告警\r\n");
        }
        if (cur_humi_alarm && !last_humi_alarm)
        {
            trigger_alarm = 1;
            snprintf(tts_text, sizeof(tts_text), "注意，湿度低于阈值");
            printf("V3F: 湿度低于阈值，触发告警\r\n");
        }
        if (cur_light_alarm && !last_light_alarm)
        {
            trigger_alarm = 1;
            snprintf(tts_text, sizeof(tts_text), "注意，光照低于阈值");
            printf("V3F: 光照低于阈值，触发告警\r\n");
        }

        /* 更新上次状态 */
        last_temp_alarm = cur_temp_alarm;
        last_humi_alarm = cur_humi_alarm;
        last_light_alarm = cur_light_alarm;

        /* 触发新告警 */
        if (trigger_alarm && !alarm_active)
        {
            alarm_active = 1;
            alarm_blink_state = 0;
            alarm_start_tick = xTaskGetTickCount();

            /* 发送语音提醒到 V5F TTS */
            if (tts_text[0] != '\0')
            {
                SharedTtsData.tts_seq++;
                SharedTtsData.tts_pending = 1;
                strncpy((char *)SharedTtsData.text, tts_text, sizeof(SharedTtsData.text) - 1);
                SharedTtsData.text[sizeof(SharedTtsData.text) - 1] = '\0';
            }
        }

        /* 告警超时检测（5秒后停止） */
        if (alarm_active)
        {
            uint32_t elapsed = (xTaskGetTickCount() - alarm_start_tick) * portTICK_PERIOD_MS;
            if (elapsed >= ALARM_DURATION_MS)
            {
                alarm_active = 0;
                alarm_blink_state = 0;
                /* 关闭告警设备（保留云端控制状态） */
                if (!SharedDeviceData.buzzer) BUZZER_OFF
                if (!SharedDeviceData.led)    LIGHT_OFF
            }
        }

        /* 告警激活时闪烁蜂鸣器和LED */
        if (alarm_active)
        {
            alarm_blink_state = !alarm_blink_state;
            if (alarm_blink_state)
            {
                BUZZER_ON
                if (!SharedDeviceData.led) LIGHT_ON
            }
            else
            {
                BUZZER_OFF
                if (!SharedDeviceData.led) LIGHT_OFF
            }
        }

        vTaskDelay(pdMS_TO_TICKS(ALARM_BLINK_MS));
    }
}

void DeviceTask_Create(void)
{
    xTaskCreate((TaskFunction_t)device_task,
                (const char *)"dev",
                (uint16_t)256,
                (void *)NULL,
                (UBaseType_t)2,
                NULL);
}
