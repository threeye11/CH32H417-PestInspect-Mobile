/********************************** (C) COPYRIGHT  *******************************
* File Name          : HC_SR04.h
* Description        : HC-SR04 ultrasonic ranging module header
*                     PC8 = Trig (output), PC9 = Echo (input)
*                     TIM5 for 1MHz microsecond timing
*******************************************************************************/
#ifndef __HC_SR04_H
#define __HC_SR04_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "ch32h417.h"

/* ==================== Pin Definitions ==================== */
#define HC_TRIG_PORT             GPIOC
#define HC_TRIG_PIN              GPIO_Pin_8

#define HC_ECHO_PORT             GPIOC
#define HC_ECHO_PIN              GPIO_Pin_9

/* ==================== Function Declarations ==================== */

void HC_SR04_Init(void);
float HC_SR04_GetDistance(void);    /* Returns distance in cm, -1.0f on timeout */

#ifdef __cplusplus
}
#endif

#endif /* __HC_SR04_H */