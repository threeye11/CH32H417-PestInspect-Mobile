/**
 * @file    car_control.c
 * @brief   云平台小车控制接口实现
 */
#include "car_control.h"
#include "tts_task.h"
#include "shared.h"
#include <stdio.h>

volatile CarCmd g_car_cmd = CAR_CMD_NONE;
volatile uint8_t g_car_mode = 0;
volatile uint8_t g_car_speed_gear = CAR_SPEED_2;

CarCmd CarControl_GetCmd(void)
{
    CarCmd cmd = g_car_cmd;
    g_car_cmd = CAR_CMD_NONE;
    return cmd;
}

void CarControl_SetCmd(CarCmd cmd)
{
    g_car_cmd = cmd;
}

uint8_t CarControl_GetMode(void)
{
    return g_car_mode;
}

uint8_t CarControl_GetSpeedGear(void)
{
    return g_car_speed_gear;
}

void CarControl_SetSpeedGear(uint8_t gear)
{
    if (gear >= CAR_SPEED_1 && gear <= CAR_SPEED_4 && gear != g_car_speed_gear)
    {
        g_car_speed_gear = gear;
        SharedCarData.car_speed = gear;
        SharedCarData.car_seq++;
        printf("CAR: 速度档位 → 前进%d\r\n", gear);
        static const char *gear_names[] = {"", "前进一", "前进二", "前进三", "前进四"};
        char tts_buf[32];
        snprintf(tts_buf, sizeof(tts_buf), "自然选择号，%s", gear_names[gear]);
        TTS_Speak(tts_buf);
    }
}
