#include "debug.h"
#include "watchdog.h"

/*********************************************************************
 * @fn      IWDG_FeedInit
 *
 * @brief   初始化独立看门狗并启动
 *
 * @param   prer - 预分频系数 (IWDG_Prescaler_4/8/16/32/64/128/256)
 *          rlr  - 重装载值 (0~0x0FFF)
 *
 * @return  none
 */
void IWDG_FeedInit(uint16_t prer, uint16_t rlr)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(prer);
    IWDG_SetReload(rlr);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

/*********************************************************************
 * @fn      IWDG_Feed
 *
 * @brief   喂独立看门狗
 *
 * @return  none
 */
void IWDG_Feed(void)
{
    IWDG_ReloadCounter();
}

/*********************************************************************
 * @fn      WWDG_FeedInit
 *
 * @brief   初始化窗口看门狗并启动（含 EWI 中断）
 *
 * @param   tr  - 计数器初值 (0x7F~0x40)
 *          wr  - 窗口值 (0x7F~0x40)
 *          prv - 预分频系数 (WWDG_Prescaler_1/2/4/8)
 *
 * @return  none
 */
void WWDG_FeedInit(uint8_t tr, uint8_t wr, uint32_t prv)
{
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_WWDG, ENABLE);

    WWDG_SetCounter(tr);
    WWDG_SetPrescaler(prv);
    WWDG_SetWindowValue(wr);
    WWDG_Enable(tr);
    WWDG_ClearFlag();

    NVIC_SetPriority(WWDG_IRQn, 0);
    NVIC_EnableIRQ(WWDG_IRQn);
    WWDG_EnableIT();
}

/*********************************************************************
 * @fn      WWDG_Feed
 *
 * @brief   喂窗口看门狗
 *
 * @return  none
 */
void WWDG_Feed(void)
{
    WWDG_SetCounter(0x7F);
}
