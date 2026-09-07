/**
 * @file    main.c
 * @brief   V5F 主程序 — 仅负责系统初始化和任务创建
 *
 *          任务分工（详见 V5F/task/ 目录）：
 *          - net_task  : ESP8266 + OneNET MQTT + 天气 + 网络对时
 *          - pest_task : K230 病害检测结果处理 + 知识库查询 + TTS 播报
 *          - led_task  : PA7 LED 闪烁
 *          - tts_task  : TW-TTS 语音播报（串口通信）
 *          - car_task  : 小车避障（超声波 + 舵机 + 电机）
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "shared.h"

#include "net_task.h"
#include "pest_task.h"
#include "led_task.h"
#include "car_task.h"
#include "tts_task.h"

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

void Queue_WeatherUpdate(const char *city, const char *weather, int8_t temp)
{
    (void)city; (void)weather; (void)temp;
}

/* ======================== main ======================== */
int main(void)
{
    SystemAndCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    printf("V5F SystemCoreClk:%d\r\n", SystemCoreClock);

    HSEM_FastTake(HSEM_ID0);
    HSEM_ReleaseOneSem(HSEM_ID0, 0);

    printf("FreeRTOS Kernel Version:%s\r\n", tskKERNEL_VERSION_NUMBER);
    Printf_Mutex_Init();

    /* 创建所有任务 */
    NetTask_Create();
    PestTask_Create();
    LedTask_Create();
    TtsTask_Create();
    CarTask_Create();

    vTaskStartScheduler();

    while (1) 
    {
        printf("Should not run to here !!!");
    }
}
