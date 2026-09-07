/********************************** (C) COPYRIGHT  *******************************
* File Name          : debug.c
* Author             : WCH
* Version            : V1.0.1
* Date               : 2025/07/16
* Description        : This file contains all the functions prototypes for UART
*                      Printf , Delay functions.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "debug.h"
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t printf_mutex = NULL;

/*********************************************************************
 * @fn      Delay_Init
 *
 * @brief   Initializes Delay Funcation.
 *
 * @return  none
 */
void Delay_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_TIM6, ENABLE);
    TIM_Cmd(TIM6, DISABLE);

    TIM_TimeBaseStructure.TIM_Period        = 65535;
    TIM_TimeBaseStructure.TIM_Prescaler     = 100 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM6, &TIM_TimeBaseStructure);

    TIM_Cmd(TIM6, ENABLE);

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_TIM7, ENABLE);
    TIM_Cmd(TIM7, DISABLE);

    TIM_TimeBaseStructure.TIM_Period        = 65535;
    TIM_TimeBaseStructure.TIM_Prescaler     = 100 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM7, &TIM_TimeBaseStructure);

    TIM_Cmd(TIM7, ENABLE);
}

/*********************************************************************
 * @fn      Delay_Us
 *
 * @brief   Microsecond Delay Time.
 *
 * @param   n - Microsecond number.
 *
 * @return  none
 */
void Delay_Us(uint32_t us)
{
#ifdef Core_V3F
    uint16_t cnt_before = TIM_GetCounter(TIM7);

    while ((uint16_t)(TIM_GetCounter(TIM7) - cnt_before) < us);

#elif defined(Core_V5F)
    uint16_t cnt_before = TIM_GetCounter(TIM6);

    while ((uint16_t)(TIM_GetCounter(TIM6) - cnt_before) < us);
#endif
}

/*********************************************************************
 * @fn      Delay_Ms
 *
 * @brief   Millisecond Delay Time.
 *
 * @param   n - Millisecond number.
 *
 * @return  none
 */
void Delay_Ms(uint32_t ms)
{
#ifdef Core_V3F
    while (ms--)
    {
        Delay_Us(1000);
    }
#elif defined(Core_V5F)
    while (ms--)
    {
        Delay_Us(1000);
    }
#endif
}

/*********************************************************************
 * @fn      USART_Printf_Init
 *
 * @brief   Initializes the USARTx peripheral.
 *
 * @param   baudrate - USART communication baud rate.
 *
 * @return  none
 */
void USART_Printf_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};

#if(DEBUG == DEBUG_UART1)  
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_USART1 | RCC_HB2Periph_GPIOA, ENABLE);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF7);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

#elif(DEBUG == DEBUG_UART8)
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_USART8, ENABLE);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource4, GPIO_AF11);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

#elif(DEBUG == DEBUG_UART6)
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_USART6, ENABLE);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF8);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

#endif

    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx;

#if(DEBUG == DEBUG_UART1)
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);

#elif(DEBUG == DEBUG_UART8)
    USART_Init(USART8, &USART_InitStructure);
    USART_Cmd(USART8, ENABLE);
    
#elif(DEBUG == DEBUG_UART6)
    USART_Init(USART6, &USART_InitStructure);
    USART_Cmd(USART6, ENABLE);

#endif
}

/*********************************************************************
 * @fn      Printf_Mutex_Init
 *
 * @brief   Initialize printf mutex (call after FreeRTOS kernel created)
 *
 * @return  none
 */
void Printf_Mutex_Init(void)
{
    printf_mutex = xSemaphoreCreateMutex();
}

/*********************************************************************
 * @fn      _write
 *
 * @brief   Support Printf Function
 *
 * @param   *buf - UART send Data.
 *          size - Data length
 *
 * @return  size: Data length
 */
__attribute__((used)) int _write(int fd, char *buf, int size)
{
    int i = 0;

    if(printf_mutex != NULL)
        xSemaphoreTake(printf_mutex, portMAX_DELAY);

    for(i = 0; i < size; i++)
    {
#if(DEBUG == DEBUG_UART1)
        while(USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
        USART_SendData(USART1, *buf++);
#elif(DEBUG == DEBUG_UART8)
        while(USART_GetFlagStatus(USART8, USART_FLAG_TC) == RESET);
        USART_SendData(USART8, *buf++);
#elif(DEBUG == DEBUG_UART6)
        while(USART_GetFlagStatus(USART6, USART_FLAG_TC) == RESET);
        USART_SendData(USART6, *buf++);
#endif
    }

    if(printf_mutex != NULL)
        xSemaphoreGive(printf_mutex);

    return size;
}

/*********************************************************************
 * @fn      _sbrk
 *
 * @brief   Change the spatial position of data segment.
 *
 * @return  size: Data length
 */
__attribute__((used)) void *_sbrk(ptrdiff_t incr)
{
    extern char _end[];
    extern char _heap_end[];
    static char *curbrk = _end;

    if ((curbrk + incr < _end) || (curbrk + incr > _heap_end))
    return NULL - 1;

    curbrk += incr;
    return curbrk - incr;
}



