/**
 * @file    sensor_task.c
 * @brief   传感器采集任务
 *
 *          读取 BH1750 光照 + SHT30 温湿度
 *          写入共享内存（供 V5F 上传云平台）
 *          发送到队列（供 V3F LVGL 显示）
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "shared.h"
#include "bh1750.h"
#include "sht30.h"

/* ======================== 任务配置 ======================== */
#define SENSOR_TASK_PRIO    4
#define SENSOR_TASK_STK_SIZE 1024

/* ======================== 任务句柄 ======================== */
TaskHandle_t SensorTask_Handler;

/* ======================== 外部引用 ======================== */
extern QueueHandle_t SensorData_Queue;
extern volatile uint8_t sensor_ready;

/* ======================== 传感器数据包 ======================== */
typedef struct {
    int16_t temperature_x10;
    int16_t humidity_x10;
    uint16_t light;
} sensor_data_t;

/* ======================== 任务主函数 ======================== */
static void sensor_task(void *pvParameters)
{
    printf("V3F SENSOR: 初始化传感器...\r\n");

    float temperature, humidity;
    float light_lux;
    uint16_t bh1750_raw;
    uint8_t bh1750_ok, sht30_ok, sht30_ret;

    BH1750_Init();
    SHT30_Init();
    vTaskDelay(pdMS_TO_TICKS(150));

    /* 首次读取 */
    bh1750_raw = BH1750_ReadData();
    light_lux = (float)bh1750_raw / 1.2f;
    bh1750_ok = (bh1750_raw != 0xFFFF) ? 1 : 0;
    sht30_ret = SHT30_GetTempHum(&temperature, &humidity);
    sht30_ok = (sht30_ret == 0) ? 1 : 0;

    SharedSensorData.light = (uint16_t)light_lux;
    SharedSensorData.temperature_x10 = (int16_t)(temperature * 10);
    SharedSensorData.humidity_x10 = (int16_t)(humidity * 10);
    SharedSensorData.bh1750_ok = bh1750_ok;
    SharedSensorData.sht30_ok = sht30_ok;
    SharedSensorData.seq = 1;
    SharedSensorData.valid = 1;

    sensor_data_t pkt;
    pkt.temperature_x10 = SharedSensorData.temperature_x10;
    pkt.humidity_x10 = SharedSensorData.humidity_x10;
    pkt.light = SharedSensorData.light;
    xQueueSend(SensorData_Queue, &pkt, 0);

    sensor_ready = 1;
    printf("V3F SENSOR: 数据就绪, 温度=%d 湿度=%d 光照=%u\r\n",
           SharedSensorData.temperature_x10,
           SharedSensorData.humidity_x10,
           SharedSensorData.light);

    while (1)
    {
        bh1750_raw = BH1750_ReadData();
        if (bh1750_raw == 0xFFFF) {
            BH1750_Init();
            vTaskDelay(pdMS_TO_TICKS(150));
            bh1750_raw = BH1750_ReadData();
        }
        light_lux = (float)bh1750_raw / 1.2f;
        bh1750_ok = (bh1750_raw != 0xFFFF) ? 1 : 0;
        sht30_ret = SHT30_GetTempHum(&temperature, &humidity);
        sht30_ok = (sht30_ret == 0) ? 1 : 0;

        if (HSEM_FastTake(HSEM_ID0) == READY)
        {
            SharedSensorData.light = (uint16_t)light_lux;
            SharedSensorData.temperature_x10 = (int16_t)(temperature * 10);
            SharedSensorData.humidity_x10 = (int16_t)(humidity * 10);
            SharedSensorData.bh1750_ok = bh1750_ok;
            SharedSensorData.sht30_ok = sht30_ok;
            SharedSensorData.seq++;
            HSEM_ReleaseOneSem(HSEM_ID0, 0);
        }

        pkt.temperature_x10 = (int16_t)(temperature * 10);
        pkt.humidity_x10 = (int16_t)(humidity * 10);
        pkt.light = (uint16_t)light_lux;
        xQueueSend(SensorData_Queue, &pkt, 0);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ======================== 公开接口 ======================== */
void SensorTask_Create(void)
{
    xTaskCreate((TaskFunction_t)sensor_task,
                (const char *)"sensor",
                (uint16_t)SENSOR_TASK_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)SENSOR_TASK_PRIO,
                (TaskHandle_t *)&SensorTask_Handler);
}
