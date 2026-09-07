/**
 * @file    main.c
 * @brief   V3F 主程序 — 仅负责系统初始化和任务创建
 *
 *          任务分工（详见 V3F/task/ 目录）：
 *          - init_task  : LVGL + RTC + 显示驱动初始化（完成后自动删除）
 *          - lvgl_task  : 时间 / 天气 / 传感器数据的 LVGL 界面更新
 *          - sensor_task: BH1750 光照 + SHT30 温湿度采集
 *          - led_task   : PE6 LED 闪烁
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "shared.h"

#include "init_task.h"
#include "lvgl_task.h"
#include "sensor_task.h"
#include "led_task.h"
#include "device_task.h"

/* ======================== 传感器数据包 ======================== */
typedef struct {
    int16_t temperature_x10;
    int16_t humidity_x10;
    uint16_t light;
} sensor_data_t;

/* ======================== 全局变量 ======================== */
QueueHandle_t SensorData_Queue;
volatile uint8_t sensor_ready = 0;
volatile uint8_t lvgl_ready = 0;

/* ======================== FreeRTOS 钩子 ======================== */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("!!! STACK OVERFLOW in task: %s\r\n", pcTaskName);
    while(1);
}

void vApplicationMallocFailedHook(void)
{
    printf("!!! MALLOC FAILED\r\n");
    while(1);
}

/* ======================== main ======================== */
int main(void)
{
    SystemInit();
    SystemAndCoreClockUpdate();
    Delay_Init();

    USART_Printf_Init(115200);
    printf("V3F SystemClk:%d\r\n", SystemCoreClock);
    printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());

    NVIC_WakeUp_V5F(Core_V5F_StartAddr);
    HSEM_ITConfig(HSEM_ID0, ENABLE);
    NVIC->SCTLR |= 1<<4;
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE);
    HSEM_ClearFlag(HSEM_ID0);
    printf("V3F: 已唤醒\r\n");

    printf("FreeRTOS Kernel Version:%s\r\n", tskKERNEL_VERSION_NUMBER);

    SensorData_Queue = xQueueCreate(4, sizeof(sensor_data_t));
    Printf_Mutex_Init();

    /* 清零共享内存标志（.shared_data 段是 NOLOAD，上电值不确定） */
    SharedTimeData.time_valid = 0;
    SharedTimeData.time_seq = 0;
    SharedWeatherData.weather_valid = 0;
    SharedWeatherData.weather_seq = 0;
    SharedWeatherCmd.weather_cmd_seq = 0;
    SharedWeatherCmd.weather_cmd_status = 0;
    SharedWeatherCmd.location[0] = '\0';
    SharedWeatherCmd.location_cn[0] = '\0';

    /* 创建所有任务 */
    InitTask_Create();
    LvglTask_Create();
    SensorTask_Create();
    LedTask_Create();
    DeviceTask_Create();

    vTaskStartScheduler();

    while (1) 
    {
        printf("Should not run to here !!!");
    }
}
