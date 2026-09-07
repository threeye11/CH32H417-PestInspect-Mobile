/**
 * @file    init_task.c
 * @brief   初始化任务 — LVGL + RTC + 显示驱动初始化
 *
 *          完成硬件初始化后自动删除自身
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "hardware.h"
#include "lcd.h"
#include "touch.h"
#include "rtc.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "my_gui.h"

/* ======================== 任务配置 ======================== */
#define INIT_TASK_PRIO      5
#define INIT_TASK_STK_SIZE  4096

/* ======================== 任务句柄 ======================== */
TaskHandle_t InitTask_Handler;

/* ======================== GPIO 初始化 ======================== */
static void GPIO_Toggle_INIT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /* PD7 - 调试用 LED 闪烁 */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOD, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_High;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    /* PC7 - 触摸校准按键（输入上拉，按下为低电平） */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_High;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}

/* ======================== 任务主函数 ======================== */
static void init_task(void *pvParameters)
{
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    PWR_VIO18ModeCfg(PWR_VIO18CFGMODE_SW);
    PWR_VIO18LevelCfg(PWR_VIO18Level_MODE3);

    GPIO_Toggle_INIT();

    RTC_Init();
    RTC_InitDriftComp();

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    show_boot_animation();
    lv_timer_handler();

    vTaskDelete(NULL);
}

/* ======================== 公开接口 ======================== */
void InitTask_Create(void)
{
    xTaskCreate((TaskFunction_t)init_task,
                (const char *)"init",
                (uint16_t)INIT_TASK_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)INIT_TASK_PRIO,
                (TaskHandle_t *)&InitTask_Handler);
}
