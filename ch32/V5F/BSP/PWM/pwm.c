/********************************** (C) COPYRIGHT  *******************************
* File Name          : PWM.c
* Description        : TB6612FNG dual motor driver implementation
*                     Motor A: PA0=PWMA(TIM2_CH2), PA1=AIN1, PC3=AIN2
*                     Motor B: PA4=BIN1, PA5=BIN2, PA6=PWMB(TIM3_CH1)
*                     System : PA7=STBY
*******************************************************************************/
#include "PWM.h"

/* ==================== Local Variables ==================== */
static uint16_t g_motorA_duty = 0;
static uint16_t g_motorB_duty = 0;

/* ==================== Private Functions ==================== */

/**
 * @brief  Initialize GPIO for direction control
 */
static void TB6612FNG_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    
    /* Enable GPIOA and GPIOC clock */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA | RCC_HB2Periph_GPIOC, ENABLE);
    
    /* Configure PA direction pins as output push-pull */
    GPIO_InitStructure.GPIO_Pin = MOTORA_AIN1_PIN |
                                  MOTORB_BIN1_PIN | MOTORB_BIN2_PIN |
                                  STBY_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* Configure PC3 (AIN2) as output push-pull */
    GPIO_InitStructure.GPIO_Pin = MOTORA_AIN2_PIN;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    
    /* Default: standby mode, all direction pins low */
    GPIO_ResetBits(MOTORA_AIN1_PORT, MOTORA_AIN1_PIN);
    GPIO_ResetBits(MOTORA_AIN2_PORT, MOTORA_AIN2_PIN);
    GPIO_ResetBits(MOTORB_BIN1_PORT, MOTORB_BIN1_PIN);
    GPIO_ResetBits(MOTORB_BIN2_PORT, MOTORB_BIN2_PIN);
    GPIO_ResetBits(STBY_PORT, STBY_PIN);  /* Standby mode */
}

/**
 * @brief  Initialize TIM2 for Motor A PWM (PA0 = TIM2_CH1_ETR)
 */
static void TIM2_PWM_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};
    
    /* Enable TIM2 and AFIO clock */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_TIM2, ENABLE);
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO, ENABLE);
    
    /* Configure PA0 as alternate function push-pull (TIM2_CH1_ETR) */
    GPIO_PinAFConfig(MOTORA_PWMA_PORT, MOTORA_PWMA_PINSOURCE, MOTORA_PWMA_AF);
    
    GPIO_InitStructure.GPIO_Pin = MOTORA_PWMA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(MOTORA_PWMA_PORT, &GPIO_InitStructure);
    
    /* Time base configuration */
    TIM_TimeBaseInitStructure.TIM_Period = TB6612_PWM_ARR;
    TIM_TimeBaseInitStructure.TIM_Prescaler = TB6612_PWM_PSC;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TB6612_TIM_PWM_A, &TIM_TimeBaseInitStructure);
    
    /* PWM1 Mode configuration: CH1 */
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;  /* Initial duty 0% */
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TB6612_TIM_PWM_A, &TIM_OCInitStructure);
    
    TIM_OC1PreloadConfig(TB6612_TIM_PWM_A, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TB6612_TIM_PWM_A, ENABLE);
    TIM_Cmd(TB6612_TIM_PWM_A, ENABLE);
}

/**
 * @brief  Initialize TIM3 for Motor B PWM (PA6 = TIM3_CH1)
 */
static void TIM3_PWM_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};
    
    /* Enable TIM3 and AFIO clock */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_TIM3, ENABLE);
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO, ENABLE);
    
    /* Configure PA6 as alternate function push-pull (TIM3_CH1) */
    GPIO_PinAFConfig(MOTORB_PWMB_PORT, MOTORB_PWMB_PINSOURCE, MOTORB_PWMB_AF);
    
    GPIO_InitStructure.GPIO_Pin = MOTORB_PWMB_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(MOTORB_PWMB_PORT, &GPIO_InitStructure);
    
    /* Time base configuration */
    TIM_TimeBaseInitStructure.TIM_Period = TB6612_PWM_ARR;
    TIM_TimeBaseInitStructure.TIM_Prescaler = TB6612_PWM_PSC;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TB6612_TIM_PWM_B, &TIM_TimeBaseInitStructure);
    
    /* PWM1 Mode configuration: CH1 */
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 0;  /* Initial duty 0% */
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TB6612_TIM_PWM_B, &TIM_OCInitStructure);
    
    TIM_OC1PreloadConfig(TB6612_TIM_PWM_B, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TB6612_TIM_PWM_B, ENABLE);
    TIM_Cmd(TB6612_TIM_PWM_B, ENABLE);
}

/**
 * @brief  Set direction pins for Motor A
 */
static void MotorA_SetDirection(MotorDir dir)
{
    switch(dir)
    {
        case MOTOR_STOP:
            GPIO_ResetBits(MOTORA_AIN1_PORT, MOTORA_AIN1_PIN);
            GPIO_ResetBits(MOTORA_AIN2_PORT, MOTORA_AIN2_PIN);
            break;
            
        case MOTOR_CW:
            GPIO_SetBits(MOTORA_AIN1_PORT, MOTORA_AIN1_PIN);
            GPIO_ResetBits(MOTORA_AIN2_PORT, MOTORA_AIN2_PIN);
            break;
            
        case MOTOR_CCW:
            GPIO_ResetBits(MOTORA_AIN1_PORT, MOTORA_AIN1_PIN);
            GPIO_SetBits(MOTORA_AIN2_PORT, MOTORA_AIN2_PIN);
            break;
            
        case MOTOR_BRAKE:
            GPIO_SetBits(MOTORA_AIN1_PORT, MOTORA_AIN1_PIN);
            GPIO_SetBits(MOTORA_AIN2_PORT, MOTORA_AIN2_PIN);
            break;
    }
}

/**
 * @brief  Set direction pins for Motor B
 */
static void MotorB_SetDirection(MotorDir dir)
{
    switch(dir)
    {
        case MOTOR_STOP:
            GPIO_ResetBits(MOTORB_BIN1_PORT, MOTORB_BIN1_PIN);
            GPIO_ResetBits(MOTORB_BIN2_PORT, MOTORB_BIN2_PIN);
            break;
            
        case MOTOR_CW:
            GPIO_SetBits(MOTORB_BIN1_PORT, MOTORB_BIN1_PIN);
            GPIO_ResetBits(MOTORB_BIN2_PORT, MOTORB_BIN2_PIN);
            break;
            
        case MOTOR_CCW:
            GPIO_ResetBits(MOTORB_BIN1_PORT, MOTORB_BIN1_PIN);
            GPIO_SetBits(MOTORB_BIN2_PORT, MOTORB_BIN2_PIN);
            break;
            
        case MOTOR_BRAKE:
            GPIO_SetBits(MOTORB_BIN1_PORT, MOTORB_BIN1_PIN);
            GPIO_SetBits(MOTORB_BIN2_PORT, MOTORB_BIN2_PIN);
            break;
    }
}

/* ==================== Public Functions ==================== */

/**
 * @brief  Initialize TB6612FNG driver
 */
void TB6612FNG_Init(void)
{
    /* Initialize GPIOs */
    TB6612FNG_GPIO_Init();
    
    /* Initialize PWM timers */
    TIM2_PWM_Init();
    TIM3_PWM_Init();
    
    /* Default: standby mode */
    TB6612FNG_Standby(DISABLE);
}

/**
 * @brief  Control standby mode
 * @param  NewState: ENABLE = active, DISABLE = standby
 */
void TB6612FNG_Standby(FunctionalState NewState)
{
    if(NewState == ENABLE)
    {
        /* Active mode: STBY = HIGH */
        GPIO_SetBits(STBY_PORT, STBY_PIN);
    }
    else
    {
        /* Standby mode: STBY = LOW */
        GPIO_ResetBits(STBY_PORT, STBY_PIN);
    }
}

/**
 * @brief  Set Motor A speed (-100 ~ +100)
 * @param  speed: negative = CCW, positive = CW, 0 = stop
 */
void MotorA_SetSpeed(int16_t speed)
{
    MotorDir dir;
    uint16_t duty;
    
    /* Clamp speed to [-100, 100] */
    if(speed > 100) speed = 100;
    if(speed < -100) speed = -100;
    
    /* Determine direction and duty */
    if(speed == 0)
    {
        dir = MOTOR_STOP;    /* Coast: AIN1=0, AIN2=0, motor freewheels to stop */
        duty = 0;
    }
    else if(speed > 0)
    {
        dir = MOTOR_CW;
        duty = (uint16_t)speed;
    }
    else
    {
        dir = MOTOR_CCW;
        duty = (uint16_t)(-speed);
    }
    
    /* Apply control */
    MotorA_Ctrl(dir, duty);
}

/**
 * @brief  Set Motor B speed (-100 ~ +100)
 * @param  speed: negative = CCW, positive = CW, 0 = stop
 */
void MotorB_SetSpeed(int16_t speed)
{
    MotorDir dir;
    uint16_t duty;
    
    /* Clamp speed to [-100, 100] */
    if(speed > 100) speed = 100;
    if(speed < -100) speed = -100;
    
    /* Determine direction and duty */
    if(speed == 0)
    {
        dir = MOTOR_STOP;    /* Coast: BIN1=0, BIN2=0, motor freewheels to stop */
        duty = 0;
    }
    else if(speed > 0)
    {
        dir = MOTOR_CW;
        duty = (uint16_t)speed;
    }
    else
    {
        dir = MOTOR_CCW;
        duty = (uint16_t)(-speed);
    }
    
    /* Apply control */
    MotorB_Ctrl(dir, duty);
}

/**
 * @brief  Control Motor A with direction and duty cycle
 * @param  dir: direction (STOP/CW/CCW/BRAKE)
 * @param  duty: duty cycle 0~100
 */
void MotorA_Ctrl(MotorDir dir, uint16_t duty)
{
    /* Clamp duty to 0~100 */
    if(duty > 100) duty = 100;
    
    /* Set direction */
    MotorA_SetDirection(dir);
    
    /* Set PWM duty */
    if(duty == 0)
    {
        TIM_SetCompare1(TB6612_TIM_PWM_A, 0);
    }
    else
    {
        /* Convert percentage to compare value */
        uint16_t compare = (duty * (TB6612_PWM_ARR + 1)) / 100;
        TIM_SetCompare1(TB6612_TIM_PWM_A, compare);
    }
    
    g_motorA_duty = duty;
}

/**
 * @brief  Control Motor B with direction and duty cycle
 * @param  dir: direction (STOP/CW/CCW/BRAKE)
 * @param  duty: duty cycle 0~100
 */
void MotorB_Ctrl(MotorDir dir, uint16_t duty)
{
    /* Clamp duty to 0~100 */
    if(duty > 100) duty = 100;
    
    /* Set direction */
    MotorB_SetDirection(dir);
    
    /* Set PWM duty */
    if(duty == 0)
    {
        TIM_SetCompare1(TB6612_TIM_PWM_B, 0);
    }
    else
    {
        /* Convert percentage to compare value */
        uint16_t compare = (duty * (TB6612_PWM_ARR + 1)) / 100;
        TIM_SetCompare1(TB6612_TIM_PWM_B, compare);
    }
    
    g_motorB_duty = duty;
}

/* ==================== Turn Functions ==================== */

/**
 * @brief  Turn left with specified speed and mode
 * @param  speed: base speed (1~100)
 * @param  mode:  TURN_PIVOT (原地) / TURN_SWING (绕轮) / TURN_ARC (弧线)
 *
 *   Motor A = left wheel, Motor B = right wheel
 *
 *   TURN_PIVOT:  A reverse,  B forward  → spins in place CCW
 *   TURN_SWING:  A stopped,  B forward  → pivots around left wheel
 *   TURN_ARC:    A slow,     B fast     → curves left while moving forward
 */
void Turn_Left(uint8_t speed, TurnMode mode)
{
    if(speed == 0) speed = 1;
    if(speed > 100) speed = 100;

    switch(mode)
    {
        case TURN_PIVOT:
            MotorA_SetSpeed(-(int16_t)speed);  /* A reverse */
            MotorB_SetSpeed((int16_t)speed);   /* B forward */
            break;

        case TURN_SWING:
            MotorA_SetSpeed(0);                /* A stopped */
            MotorB_SetSpeed((int16_t)speed);   /* B forward */
            break;

        case TURN_ARC:
        {
            uint8_t slow = speed / 2;          /* inner wheel at 50% */
            if(slow < 1) slow = 1;
            MotorA_SetSpeed((int16_t)slow);    /* A slow forward */
            MotorB_SetSpeed((int16_t)speed);   /* B fast forward */
            break;
        }
    }
}

/**
 * @brief  Turn right with specified speed and mode
 * @param  speed: base speed (1~100)
 * @param  mode:  TURN_PIVOT (原地) / TURN_SWING (绕轮) / TURN_ARC (弧线)
 *
 *   Motor A = left wheel, Motor B = right wheel
 *
 *   TURN_PIVOT:  A forward, B reverse  → spins in place CW
 *   TURN_SWING:  A forward, B stopped  → pivots around right wheel
 *   TURN_ARC:    A fast,    B slow     → curves right while moving forward
 */
void Turn_Right(uint8_t speed, TurnMode mode)
{
    if(speed == 0) speed = 1;
    if(speed > 100) speed = 100;

    switch(mode)
    {
        case TURN_PIVOT:
            MotorA_SetSpeed((int16_t)speed);   /* A forward */
            MotorB_SetSpeed(-(int16_t)speed);  /* B reverse */
            break;

        case TURN_SWING:
            MotorA_SetSpeed((int16_t)speed);   /* A forward */
            MotorB_SetSpeed(0);                /* B stopped */
            break;

        case TURN_ARC:
        {
            uint8_t slow = speed / 2;          /* inner wheel at 50% */
            if(slow < 1) slow = 1;
            MotorA_SetSpeed((int16_t)speed);   /* A fast forward */
            MotorB_SetSpeed((int16_t)slow);    /* B slow forward */
            break;
        }
    }
}
