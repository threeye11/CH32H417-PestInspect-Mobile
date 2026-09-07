/********************************** (C) COPYRIGHT  *******************************
* File Name          : servo.h
* Description        : SG90舵机驱动头文件 (TIM4_CH2, PE4)
*******************************************************************************/
#ifndef __SERVO_H
#define __SERVO_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "ch32h417.h"

/* ==================== 引脚定义 ==================== */
#define SERVO_PORT               GPIOE
#define SERVO_PIN                GPIO_Pin_4
#define SERVO_TIM                TIM4

/* ==================== Function Declarations ==================== */
void Servo_Init(void);
void Servo_SetAngle(uint16_t angle);   /* 0° ~ 180° */
void Servo_Center(void);               /* 90° */
void Servo_LookLeft(void);             /* 0° */
void Servo_LookRight(void);            /* 180° */

#ifdef __cplusplus
}
#endif

#endif /* __SERVO_H */
