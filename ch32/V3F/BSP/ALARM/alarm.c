/**
 * @file    alarm.c
 * @brief   声光报警模块驱动
 * @note    PF11-蜂鸣器, PC4-LED指示灯, PF13-水泵（低电平触发）
 */

#include "alarm.h"

void alarm_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /* 使能 GPIOF 和 GPIOC 时钟 */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOF | RCC_HB2Periph_GPIOC, ENABLE);

    /* PF11(PF13) 初始化为推挽输出 */
    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_11 | GPIO_Pin_13;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_High;
    GPIO_Init(GPIOF, &GPIO_InitStruct);

    /* PC4 (LED指示灯) 初始化为推挽输出 */
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_4;
    GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* 默认全部关闭：蜂鸣器(PF11)和水泵(PF13)拉低，LED(PC4)拉高 */
    GPIO_ResetBits(GPIOF, GPIO_Pin_11 | GPIO_Pin_13);
    GPIO_SetBits(GPIOC, GPIO_Pin_4);
}
