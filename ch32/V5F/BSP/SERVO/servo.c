/********************************** (C) COPYRIGHT  *******************************
* File Name          : servo.c
* Description        : SG90舵机驱动，使用 TIM4_CH2 (PE4) 输出 PWM
*                     TIM4: 100MHz / 100 = 1MHz, ARR=20000 → 50Hz (20ms)
*                     脉冲: 500us(0°) ~ 1500us(90°) ~ 2500us(180°)
*******************************************************************************/
#include "servo.h"

/* ==================== 宏定义 ==================== */
#define SERVO_PERIOD             (20000 - 1)    /* 20ms @ 1MHz */
#define SERVO_MIN_US             500            /* 0° */
#define SERVO_MAX_US             2500           /* 180° */
#define SERVO_CENTER_US          1500           /* 90° */

/* ==================== 公开函数 ==================== */

/**
 * @brief  初始化舵机 PWM (PE4, TIM4_CH2)
 *         TIM4 时钟约 100MHz (与 TIM5 同总线), PSC=99 → 1MHz
 *         ARR=19999 → 50Hz, PWM1 模式
 */
void Servo_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};

    /* --- GPIO: PE4 复用推挽 --- */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOE, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = SERVO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(SERVO_PORT, &GPIO_InitStructure);

    GPIO_PinAFConfig(SERVO_PORT, GPIO_PinSource4, GPIO_AF2);  /* PE4 → TIM4_CH2 */

    /* --- TIM4: 1MHz, 50Hz PWM --- */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_TIM4, ENABLE);

    TIM_TimeBaseInitStructure.TIM_Period        = SERVO_PERIOD;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = 99;  /* 100MHz / 100 = 1MHz */
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(SERVO_TIM, &TIM_TimeBaseInitStructure);

    /* --- PWM CH2: PWM1 模式, 高电平有效 --- */
    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = SERVO_CENTER_US;   /* 初始居中 90° */
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC2Init(SERVO_TIM, &TIM_OCInitStructure);

    TIM_CtrlPWMOutputs(SERVO_TIM, ENABLE);
    TIM_ARRPreloadConfig(SERVO_TIM, ENABLE);   /* 防止舵机微动：ARR 预装载，脉冲更新在周期边界生效 */
    TIM_Cmd(SERVO_TIM, ENABLE);
}

/**
 * @brief  设置舵机角度 (0° ~ 180°)
 *         线性映射到脉冲宽度: 500us + angle × (2000us / 180)
 */
void Servo_SetAngle(uint16_t angle)
{
    uint16_t pulse;

    if(angle > 180) angle = 180;

    pulse = SERVO_MIN_US + (uint32_t)angle * (SERVO_MAX_US - SERVO_MIN_US) / 180;
    TIM_SetCompare2(SERVO_TIM, pulse);
}

void Servo_Center(void)
{
    TIM_SetCompare2(SERVO_TIM, SERVO_CENTER_US);
}

void Servo_LookLeft(void)
{
    Servo_SetAngle(0);
}

void Servo_LookRight(void)
{
    Servo_SetAngle(180);
}