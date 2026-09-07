/**
 * @file    car_control.h
 * @brief   云平台小车控制接口
 *
 *          云平台下发 car_control 指令 → OneNet_RevPro 解析 → 设置 cmd
 *          car_task 检查 cmd → 执行对应动作
 */
#ifndef __CAR_CONTROL_H
#define __CAR_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    CAR_CMD_NONE    = 0,
    CAR_CMD_FORWARD,
    CAR_CMD_BACKWARD,
    CAR_CMD_LEFT,
    CAR_CMD_RIGHT,
    CAR_CMD_STOP,
    CAR_CMD_AVOID,
    CAR_CMD_MANUAL,
    CAR_CMD_FOLLOW
} CarCmd;

typedef enum {
    CAR_MODE_AVOID = 0,
    CAR_MODE_MANUAL,
    CAR_MODE_FOLLOW
} CarMode;

typedef enum {
    CAR_SPEED_1 = 1,   /* 25% */
    CAR_SPEED_2 = 2,   /* 50% */
    CAR_SPEED_3 = 3,   /* 75% */
    CAR_SPEED_4 = 4    /* 100% */
} CarSpeedGear;

extern volatile CarCmd g_car_cmd;
extern volatile uint8_t g_car_mode;
extern volatile uint8_t g_car_speed_gear;

CarCmd CarControl_GetCmd(void);
void CarControl_SetCmd(CarCmd cmd);
uint8_t CarControl_GetMode(void);
uint8_t CarControl_GetSpeedGear(void);
void CarControl_SetSpeedGear(uint8_t gear);

#ifdef __cplusplus
}
#endif

#endif /* __CAR_CONTROL_H */
