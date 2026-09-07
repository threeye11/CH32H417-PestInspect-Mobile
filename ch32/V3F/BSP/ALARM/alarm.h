/**
 * @file    alarm.h
 * @brief   声光报警模块驱动头文件
 * @note    提供蜂鸣器/LED/水泵的宏定义控制接口
 */

#ifndef _ALARM_H_
#define _ALARM_H_

#include "ch32h417.h"

/* PF11 - 蜂鸣器控制（高电平触发） */
#define    BUZZER_ON    GPIO_WriteBit(GPIOF, GPIO_Pin_11, Bit_SET);
#define    BUZZER_OFF   GPIO_WriteBit(GPIOF, GPIO_Pin_11, Bit_RESET);

/* PC4 - LED 指示灯控制（低电平触发） */
#define    LIGHT_ON     GPIO_WriteBit(GPIOC, GPIO_Pin_4, Bit_RESET);
#define    LIGHT_OFF    GPIO_WriteBit(GPIOC, GPIO_Pin_4, Bit_SET);

/* PF13 - 水泵控制（高电平触发） */
#define    PUMP_ON      GPIO_WriteBit(GPIOF, GPIO_Pin_13, Bit_SET);
#define    PUMP_OFF     GPIO_WriteBit(GPIOF, GPIO_Pin_13, Bit_RESET);

void alarm_init(void);

#endif
