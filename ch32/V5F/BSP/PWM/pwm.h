/********************************** (C) COPYRIGHT  *******************************
* File Name          : PWM.h
* Description        : TB6612FNG dual motor driver header
*                     Motor A: PA0=PWMA(TIM2_CH2), PA1=AIN1, PC3=AIN2
*                     Motor B: PA4=BIN1, PA5=BIN2, PA6=PWMB(TIM3_CH1)
*                     System : PA7=STBY
*******************************************************************************/
#ifndef __PWM_H
#define __PWM_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "ch32h417.h"

/* ==================== Pin Definitions ==================== */

/* --- Motor A --- */
#define MOTORA_AIN1_PORT         GPIOA
#define MOTORA_AIN1_PIN          GPIO_Pin_1

#define MOTORA_AIN2_PORT         GPIOC
#define MOTORA_AIN2_PIN          GPIO_Pin_3

#define MOTORA_PWMA_PORT         GPIOA
#define MOTORA_PWMA_PIN          GPIO_Pin_0
#define MOTORA_PWMA_PINSOURCE    GPIO_PinSource0
#define MOTORA_PWMA_AF           GPIO_AF1              /* TIM2_CH1_ETR */

/* --- Motor B --- */
#define MOTORB_BIN1_PORT         GPIOA
#define MOTORB_BIN1_PIN          GPIO_Pin_4

#define MOTORB_BIN2_PORT         GPIOA
#define MOTORB_BIN2_PIN          GPIO_Pin_5

#define MOTORB_PWMB_PORT         GPIOA
#define MOTORB_PWMB_PIN          GPIO_Pin_6
#define MOTORB_PWMB_PINSOURCE    GPIO_PinSource6
#define MOTORB_PWMB_AF           GPIO_AF2              /* TIM3_CH1 */

/* --- Standby --- */
#define STBY_PORT                GPIOA
#define STBY_PIN                 GPIO_Pin_7

/* --- PWM Timer base --- */
#define TB6612_TIM_PWM_A         TIM2
#define TB6612_TIM_PWM_B         TIM3

/* ==================== PWM Parameters ==================== */
/*
 * PWM frequency = TIM_CLK / (PSC + 1) / (ARR + 1)
 * 400MHz / 80 / 100 = 50kHz
 */
#define TB6612_PWM_PSC           (80 - 1)
#define TB6612_PWM_ARR           (100 - 1)

/* ==================== Motor Direction Enum ==================== */
typedef enum
{
    MOTOR_STOP  = 0,
    MOTOR_CW    = 1,    /* Clockwise */
    MOTOR_CCW   = 2,    /* Counter-Clockwise */
    MOTOR_BRAKE = 3     /* Short brake */
} MotorDir;

/* ==================== Function Declarations ==================== */

/* ==================== Turn Mode Enum ==================== */
typedef enum
{
    TURN_PIVOT = 0,   /* 原地旋转：一侧正转、另一侧反转，绕底盘中心转 */
    TURN_SWING = 1,   /* 绕轮旋转：一侧停转、另一侧转动，绕静止轮转 */
    TURN_ARC   = 2    /* 弧线转弯：两侧同向差速，走大弧线 */
} TurnMode;

/* ==================== Function Declarations ==================== */

void TB6612FNG_Init(void);
void TB6612FNG_Standby(FunctionalState NewState);

void MotorA_SetSpeed(int16_t speed);    /* speed: -100 ~ +100, sign = direction */
void MotorB_SetSpeed(int16_t speed);

void MotorA_Ctrl(MotorDir dir, uint16_t duty);  /* duty: 0 ~ 100 */
void MotorB_Ctrl(MotorDir dir, uint16_t duty);

void Turn_Left(uint8_t speed, TurnMode mode);   /* 左转 */
void Turn_Right(uint8_t speed, TurnMode mode);  /* 右转 */

#ifdef __cplusplus
}
#endif

#endif /* __PWM_H */
