/********************************** (C) COPYRIGHT  *******************************
* File Name          : HC_SR04.c
* Description        : HC-SR04 ultrasonic ranging module driver
*                     PC8 = Trig (output), PC9 = Echo (input)
*                     TIM5 = 1MHz timer for echo pulse width and timeout
*
*   Distance formula:  distance(cm) = echo_time(us) * 0.034 / 2
*                                   ≈ echo_time(us) / 58
*
*   NOTE: PE4/PE5 are standard GPIO, no special config needed.
*******************************************************************************/
#include "HC_SR04.h"

/* ==================== Macros ==================== */
#define TIM_ECHO                 TIM5
#define HC_TIMEOUT_US            (38000UL)       /* ~6.5m max, HC-SR04 spec 4m */
#define HC_US_PER_CM             58U             /* 1cm = 58us round trip (340m/s) */
#define HC_TRIG_US               10U             /* Trig pulse width */

/* ==================== Local Functions ==================== */

/**
 * @brief  Send 10us trigger pulse on PC8 using TIM5 microsecond delay
 */
static void HC_SR04_Trig(void)
{
    uint16_t start;

    /* Pull low first */
    GPIO_ResetBits(HC_TRIG_PORT, HC_TRIG_PIN);

    /* Wait 2us using TIM5 */
    start = TIM_GetCounter(TIM_ECHO);
    while((uint16_t)(TIM_GetCounter(TIM_ECHO) - start) < 2);

    /* Pulse high for 10us */
    GPIO_SetBits(HC_TRIG_PORT, HC_TRIG_PIN);
    start = TIM_GetCounter(TIM_ECHO);
    while((uint16_t)(TIM_GetCounter(TIM_ECHO) - start) < HC_TRIG_US);

    /* Pull low */
    GPIO_ResetBits(HC_TRIG_PORT, HC_TRIG_PIN);
}

/* ==================== Public Functions ==================== */

/**
 * @brief  Initialize HC-SR04 GPIO and TIM5 for echo timing
 *         PC8 = Trig (PP output), PC9 = Echo (floating input)
 *         TIM5 = 1MHz counter (SystemCoreClock-based, 1us per tick)
 */
void HC_SR04_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    uint32_t psc_val;

    /* --- GPIO --- */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOC, ENABLE);

    /* PC8: Trig → push-pull output */
    GPIO_InitStructure.GPIO_Pin   = HC_TRIG_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(HC_TRIG_PORT, &GPIO_InitStructure);
    GPIO_ResetBits(HC_TRIG_PORT, HC_TRIG_PIN);

    /* PC9: Echo → floating input */
    GPIO_InitStructure.GPIO_Pin   = HC_ECHO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(HC_ECHO_PORT, &GPIO_InitStructure);

    /* --- TIM5: 1MHz count, 1 tick = 1us --- */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_TIM5, ENABLE);

    /* Self-test shows TIM5 input clock ≈ 100MHz despite SystemCoreClock=400MHz.
       PSC=99 gives 100MHz/100 = 1MHz (1 tick = 1us) */
    psc_val = 100;

    TIM_TimeBaseInitStructure.TIM_Period        = 65535;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = psc_val - 1;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM_ECHO, &TIM_TimeBaseInitStructure);
    TIM_GenerateEvent(TIM_ECHO, TIM_EventSource_Update);  /* force PSC load */

    TIM_Cmd(TIM_ECHO, ENABLE);
}

/**
 * @brief  Trigger ultrasonic ranging and return distance
 * @return Distance in centimeters (2cm ~ 400cm typical)
 *         Returns -1.0f on timeout / out of range
 *
 *  Timing flow:
 *    1. Send 10us Trig pulse
 *    2. Poll Echo pin + TIM5 counter → wait for high (timeout = 38ms)
 *    3. Reset TIM5 counter at rising edge
 *    4. Poll Echo pin + TIM5 counter → wait for low (timeout = 38ms)
 *    5. Read TIM5 counter = echo pulse width in us
 *    6. Convert: distance = ticks / 58
 */
float HC_SR04_GetDistance(void)
{
    uint16_t start, now;
    uint16_t echo_ticks;

    /* 1. Send trigger pulse */
    HC_SR04_Trig();

    /* 2. Wait for Echo to go high, TIM5-based timeout */
    start = TIM_GetCounter(TIM_ECHO);
    while(GPIO_ReadInputDataBit(HC_ECHO_PORT, HC_ECHO_PIN) == RESET)
    {
        now = TIM_GetCounter(TIM_ECHO);
        if((uint16_t)(now - start) > HC_TIMEOUT_US)
        {
            return -1.0f;   /* Timeout: no echo */
        }
    }

    /* 3. Echo rising edge detected → reset counter for pulse width measurement */
    TIM_SetCounter(TIM_ECHO, 0);

    /* 4. Wait for Echo to go low, TIM5-based timeout */
    while(GPIO_ReadInputDataBit(HC_ECHO_PORT, HC_ECHO_PIN) != RESET)
    {
        now = TIM_GetCounter(TIM_ECHO);
        if(now > HC_TIMEOUT_US)
        {
            return -1.0f;   /* Timeout: echo pulse too long */
        }
    }

    /* 5. Echo falling edge → read pulse width */
    echo_ticks = TIM_GetCounter(TIM_ECHO);

    /* 6. Convert to centimeters: distance = time_us / 58 */
    if(echo_ticks == 0)
    {
        return -1.0f;
    }

    return (float)echo_ticks / HC_US_PER_CM;
}