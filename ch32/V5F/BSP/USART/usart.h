#ifndef _MYUSART_H_
#define _MYUSART_H_


#include "ch32h417.h"

void USART2_Init(uint32_t baudrate);
void Usart_SendString(USART_TypeDef *USARTx, unsigned char *str, unsigned short len);
void UsartPrintf(USART_TypeDef *USARTx, char *fmt, ...);
	
#endif
