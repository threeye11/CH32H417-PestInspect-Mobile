/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32h417_it.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "ch32h417_it.h"
#include "FreeRTOS.h"
#include "task.h"
void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
  while (1)
  {
    
  }
}

/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  none
 */
void HardFault_Handler(void)
{
  // NVIC_SystemReset();
    /* 1. ��ȡ�ؼ� CSR */
    uint32_t mepc    = __get_MEPC();      /* �����쳣��ָ���ַ */
    uint32_t mcause  = __get_MCAUSE();    /* �쳣ԭ�� */
    uint32_t mtval   = __get_MTVAL();     /* �쳣��ص�ַ */
    uint32_t mstatus = __get_MSTATUS();   /* ����״̬ */
    uint32_t sp      = __get_SP();        /* ��ǰջָ�� */

    /* 2. ��ȡ��ǰ������ (FreeRTOS) */
    const char *task_name = pcTaskGetName(NULL);

    /* 3. ����ؼ���Ϣ */
    printf("\r\n========== HARDFAULT ==========\r\n");
    printf("  Task    : %s\r\n", task_name);
    printf("  MEPC    : 0x%08X\r\n", mepc);
    printf("  MCAUSE  : 0x%08X\r\n", mcause);
    printf("  MTVAL   : 0x%08X\r\n", mtval);
    printf("  MSTATUS : 0x%08X\r\n", mstatus);
    printf("  SP      : 0x%08X\r\n", sp);

    /* 4. ת��ջ��������ԭʼ���� (��ѡ���������߷���) */
    printf("  Stack[0..7]: ");
    for (int i = 0; i < 8; i++) {
        printf("%08X ", ((uint32_t *)sp)[i]);
    }
    printf("\r\n");

    NVIC_SystemReset();
  while (1)
  {
  }
}


