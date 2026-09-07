#ifndef __CAR_TASK_H
#define __CAR_TASK_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"

extern TaskHandle_t CarTask_Handler;

void CarTask_Create(void);

#ifdef __cplusplus
}
#endif

#endif /* __CAR_TASK_H */
