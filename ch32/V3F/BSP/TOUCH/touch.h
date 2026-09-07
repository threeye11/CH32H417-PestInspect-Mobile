#ifndef __TOUCH_H__
#define __TOUCH_H__

#include "debug.h"

#define TP_PRES_DOWN  0x80
#define TP_CATH_PRES  0x40
#define CT_MAX_TOUCH   5

/* 校准等待 V5F 加载超时（ms） */
#define TP_CAL_WAIT_MS  5000

typedef struct
{
    u8 (*init)(void);
    u8 (*scan)(u8);
    void (*adjust)(void);
    u16 x[CT_MAX_TOUCH];
    u16 y[CT_MAX_TOUCH];
    u8  sta;
    /* 5点校准参数 */
    float xfac;             /* X 轴比例因子 */
    float yfac;             /* Y 轴比例因子 */
    short xc;               /* 中心点 X 原始 ADC 值 */
    short yc;               /* 中心点 Y 原始 ADC 值 */
    u8 touchtype;           /* b0: 0=横屏, 1=竖屏; b7: 0=电阻, 1=电容 */
} _m_tp_dev;

extern _m_tp_dev tp_dev;
extern volatile u8 tp_calibrating;  /* 1=正在校准，LVGL 不应读取触摸 */

/* 触摸引脚 (GPIOF) */
#define PEN   GPIO_ReadInputDataBit(GPIOF, GPIO_Pin_10)
#define DOUT  GPIO_ReadInputDataBit(GPIOF, GPIO_Pin_9)
#define TDIN(n)  (n ? GPIO_WriteBit(GPIOF, GPIO_Pin_8, Bit_SET) : GPIO_WriteBit(GPIOF, GPIO_Pin_8, Bit_RESET))
#define TCLK(n)  (n ? GPIO_WriteBit(GPIOF, GPIO_Pin_7, Bit_SET) : GPIO_WriteBit(GPIOF, GPIO_Pin_7, Bit_RESET))
#define TCS(n)   (n ? GPIO_WriteBit(GPIOF, GPIO_Pin_6, Bit_SET) : GPIO_WriteBit(GPIOF, GPIO_Pin_6, Bit_RESET))

u8 TP_Init(void);
u8 TP_Scan(u8 tp);
void TP_Adjust(void);

#endif
