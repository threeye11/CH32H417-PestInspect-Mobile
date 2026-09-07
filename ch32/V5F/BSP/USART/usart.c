/**
	************************************************************
	************************************************************
	************************************************************
	*	文件名： 	usart.c
	*	作者： 		zh
	*	日期： 		2016-11-23
	*	版本： 		V1.0
	*	说明： 		单片机串口外设初始化，格式化打印
	*	修改记录：	
	************************************************************
	************************************************************
	************************************************************
**/

//硬件驱动
#include "ch32h417.h"
#include "usart.h"

//C库
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

uint8_t rx_data;

/*
************************************************************
*	函数名称：	USART2_Init
*
*	函数功能：	串口2初始化（ESP8266 WiFi通信）
*
*	入口参数：	baudrate：设定的波特率
*
*	返回参数：	无
*
*	说明：		TX-PA2		RX-PA3
************************************************************
*/
void USART2_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef  GPIO_InitStruct = {0};
    USART_InitTypeDef USART_InitStruct = {0};

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_USART2, ENABLE);
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA | RCC_HB2Periph_AFIO, ENABLE);

    // PA2  USART2_TX（复用推挽输出）
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF7);
    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PA3  USART2_RX（浮空输入）
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF7);
    GPIO_InitStruct.GPIO_Pin  = GPIO_Pin_3;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    USART_InitStruct.USART_BaudRate            = baudrate;
    USART_InitStruct.USART_WordLength          = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits            = USART_StopBits_1;
    USART_InitStruct.USART_Parity              = USART_Parity_No;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStruct.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(USART2, &USART_InitStruct);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    NVIC_EnableIRQ(USART2_IRQn);
    USART_Cmd(USART2, ENABLE);
}

/*
************************************************************
*	函数名称：	Usart_SendString
*
*	函数功能：	串口数据发送
*
*	入口参数：	USARTx：串口号
*				str：要发送的数据
*				len：数据长度
*
*	返回参数：	无
*
*	说明：		
************************************************************
*/
void Usart_SendString(USART_TypeDef *USARTx, unsigned char *str, unsigned short len)
{
    unsigned short i;
    for(i = 0; i < len; i++)
    {
        while(USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET);
        USART_SendData(USARTx, str[i]);
    }
}

/*
************************************************************
*	函数名称：	UsartPrintf
*
*	函数功能：	格式化打印
*
*	入口参数：	USARTx：串口号
*				fmt：不定长参
*
*	返回参数：	无
*
*	说明：		
************************************************************
*/
void UsartPrintf(USART_TypeDef *USARTx, char *fmt, ...)
{
    unsigned char UsartPrintfBuf[296];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf((char *)UsartPrintfBuf, sizeof(UsartPrintfBuf), fmt, ap);
    va_end(ap);

    Usart_SendString(USARTx, UsartPrintfBuf, strlen((char *)UsartPrintfBuf));
}