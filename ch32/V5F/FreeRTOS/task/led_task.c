/**
 * @file    led_task.c
 * @brief   LED 闪烁任务 — PD6 (V5F 板载 LED)
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"

/* ======================== 任务配置 ======================== */
#define LED_TASK_PRIO       2
#define LED_TASK_STK_SIZE   256

/* ======================== 任务句柄 ======================== */
TaskHandle_t LedBlinkTask_Handler;

/* ======================== 任务主函数 ======================== */
static void led_blink_task(void *pvParameters)
{
    while (1)
    {
        GPIOD->OUTDR ^= GPIO_Pin_6;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ======================== 公开接口 ======================== */
void LedTask_Create(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOD, ENABLE);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_High;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    xTaskCreate((TaskFunction_t)led_blink_task,
                (const char *)"led",
                (uint16_t)LED_TASK_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)LED_TASK_PRIO,
                (TaskHandle_t *)&LedBlinkTask_Handler);
}
