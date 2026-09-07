/**
 * @file    touch.c
 * @brief   电阻触摸屏驱动 + 5点校准
 *
 *          校准数据通过共享内存中转，由 V5F 保存到 W25Q64 (0x4000)
 *          V3F 首次启动时等待 V5F 加载校准数据（超时后进入校准）
 */
#include "touch.h"
#include "lcd.h"
#include "math.h"
#include "shared.h"
#include "watchdog.h"

/* ======================== 全局变量 ======================== */
_m_tp_dev tp_dev =
{
    TP_Init,
    TP_Scan,
    TP_Adjust,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
};

volatile u8 tp_calibrating = 0;

static u8 CMD_RDX = 0x90;
static u8 CMD_RDY = 0xD0;

/* ======================== SPI 底层 ======================== */

static void TP_Write_Byte(u8 num)
{
    u8 count = 0;
    for (count = 0; count < 8; count++)
    {
        if (num & 0x80)
            TDIN(1);
        else
            TDIN(0);
        num <<= 1;
        TCLK(0);
        TCLK(1);
    }
}

static u16 TP_Read_AD(u8 CMD)
{
    u8  count = 0;
    u16 Num   = 0;

    TCLK(0);
    TDIN(0);
    TCS(0);
    TP_Write_Byte(CMD);
    Delay_Us(6);
    TCLK(0);
    Delay_Us(1);
    TCLK(1);
    TCLK(0);

    for (count = 0; count < 16; count++)
    {
        Num <<= 1;
        TCLK(0);
        TCLK(1);
        if (DOUT) Num++;
    }
    Num >>= 4;
    TCS(1);
    return Num;
}

/* ======================== 滤波读取 ======================== */

#define READ_TIMES  5
#define LOST_VAL    1

static u16 TP_Read_XOY(u8 xy)
{
    u16 i, j;
    u16 buf[READ_TIMES];
    u16 sum  = 0;
    u16 temp;

    for (i = 0; i < READ_TIMES; i++)
        buf[i] = TP_Read_AD(xy);

    for (i = 0; i < READ_TIMES - 1; i++)
    {
        for (j = i + 1; j < READ_TIMES; j++)
        {
            if (buf[i] > buf[j])
            {
                temp    = buf[i];
                buf[i]  = buf[j];
                buf[j]  = temp;
            }
        }
    }

    sum = 0;
    for (i = LOST_VAL; i < READ_TIMES - LOST_VAL; i++)
        sum += buf[i];
    temp = sum / (READ_TIMES - 2 * LOST_VAL);
    return temp;
}

u8 TP_Read_XY(u16 *x, u16 *y)
{
    *x = TP_Read_XOY(CMD_RDX);
    *y = TP_Read_XOY(CMD_RDY);
    return 1;
}

/* ======================== 双次读取滤波 ======================== */

#define ERR_RANGE  50

static u8 TP_Read_XY2(u16 *x, u16 *y)
{
    u16 x1, y1, x2, y2;
    if (!TP_Read_XY(&x1, &y1)) return 0;
    if (!TP_Read_XY(&x2, &y2)) return 0;

    if (((x2 <= x1 && x1 < x2 + ERR_RANGE) || (x1 <= x2 && x2 < x1 + ERR_RANGE))
     && ((y2 <= y1 && y1 < y2 + ERR_RANGE) || (y1 <= y2 && y2 < y1 + ERR_RANGE)))
    {
        *x = (x1 + x2) / 2;
        *y = (y1 + y2) / 2;
        return 1;
    }
    return 0;
}

/* ======================== 触摸扫描 ======================== */
/* tp=0: 屏幕坐标（校准后）, tp=1: 原始坐标（校准用） */
u8 TP_Scan(u8 tp)
{
    if (PEN == 0)
    {
        if (tp)
            TP_Read_XY2(&tp_dev.x[0], &tp_dev.y[0]);
        else if (TP_Read_XY2(&tp_dev.x[0], &tp_dev.y[0]))
        {
            if (tp_dev.xfac != 0.0f && tp_dev.yfac != 0.0f)
            {
                short dx = (short)(tp_dev.x[0] - tp_dev.xc);
                short dy = (short)(tp_dev.y[0] - tp_dev.yc);
                int sx = (int)(dx / tp_dev.xfac) + lcddev.width / 2;
                int sy = (int)(dy / tp_dev.yfac) + lcddev.height / 2;
                if (sx < 0) sx = 0;
                if (sy < 0) sy = 0;
                if (sx >= lcddev.width) sx = lcddev.width - 1;
                if (sy >= lcddev.height) sy = lcddev.height - 1;
                tp_dev.x[0] = (u16)sx;
                tp_dev.y[0] = (u16)sy;
            }
        }

        if ((tp_dev.sta & TP_PRES_DOWN) == 0)
        {
            tp_dev.sta  = TP_PRES_DOWN | TP_CATH_PRES;
            tp_dev.x[4] = tp_dev.x[0];
            tp_dev.y[4] = tp_dev.y[0];
        }
    }
    else
    {
        if (tp_dev.sta & TP_PRES_DOWN)
            tp_dev.sta &= ~(1 << 7);
        else
        {
            tp_dev.x[4] = 0;
            tp_dev.y[4] = 0;
            tp_dev.x[0] = 0xffff;
            tp_dev.y[0] = 0xffff;
        }
    }
    return tp_dev.sta & TP_PRES_DOWN;
}

/* ======================== 共享内存校准数据读写 ======================== */

static u8 TP_Load_FromShared(void)
{
    if (SharedCalData.cal_status == 0)
        return 0;   /* 无有效数据 */

    tp_dev.xfac = SharedCalData.xfac;
    tp_dev.yfac = SharedCalData.yfac;
    tp_dev.xc   = SharedCalData.xc;
    tp_dev.yc   = SharedCalData.yc;
    tp_dev.touchtype = 1;
    CMD_RDX = 0x90;
    CMD_RDY = 0xD0;

    printf("触摸: 从共享内存加载校准 (xfac=%.4f yfac=%.4f xc=%d yc=%d status=%d)\r\n",
           tp_dev.xfac, tp_dev.yfac, tp_dev.xc, tp_dev.yc, SharedCalData.cal_status);
    return 1;
}

static void TP_Save_ToShared(void)
{
    SharedCalData.xfac = tp_dev.xfac;
    SharedCalData.yfac = tp_dev.yfac;
    SharedCalData.xc   = tp_dev.xc;
    SharedCalData.yc   = tp_dev.yc;
    SharedCalData.cal_status = 2;   /* 数据有效（待 V5F 持久化到 Flash） */
    SharedCalData.cal_seq++;        /* 通知 V5F 保存到 Flash */
    printf("触摸: 校准数据已写入共享内存，等待 V5F 保存到 W25Q64\r\n");
}

/* ======================== 绘制校准十字 ======================== */

static void TP_Drow_Touch_Point(u16 x, u16 y, u16 color)
{
    POINT_COLOR = color;
    LCD_DrawLine(x - 12, y, x + 13, y);
    LCD_DrawLine(x, y - 12, x, y + 13);
    LCD_DrawPoint(x + 1, y + 1);
    LCD_DrawPoint(x - 1, y + 1);
    LCD_DrawPoint(x + 1, y - 1);
    LCD_DrawPoint(x - 1, y - 1);
    LCD_Draw_Circle(x, y, 6);
}

/* ======================== 5点校准算法 ======================== */
/*
 * 注意：V5F 调度器已启动，校准期间必须禁中断防止任务干扰
 * 直接操作 RISC-V mstatus CSR（不依赖 FreeRTOS）
 * Delay_Ms 使用独立硬件定时器，禁中断后仍正常工作
 */
void TP_Adjust(void)
{
    u16 pxy[5][2];
    u8  cnt = 0;
    short s1, s2, s3, s4;
    double px, py;
    u16 outtime = 0;

    LCD_Clear(WHITE);
    POINT_COLOR = BLUE;
    LCD_ShowString(40, 40, 160, 100, 16, (u8 *)"Please click the cross");
    LCD_ShowString(40, 60, 160, 100, 16, (u8 *)"on screen to calibrate");

    TP_Drow_Touch_Point(20, 20, RED);
    tp_dev.sta = 0;
    tp_calibrating = 1;

    __asm volatile ("li t0, 0x1800; csrw mstatus, t0" ::: "t0", "memory");   /* 禁中断 */

    while (1)
    {
        TP_Scan(1);

        if ((tp_dev.sta & 0xC0) == TP_CATH_PRES)
        {
            outtime = 0;
            tp_dev.sta &= ~TP_CATH_PRES;

            pxy[cnt][0] = tp_dev.x[0];
            pxy[cnt][1] = tp_dev.y[0];
            cnt++;

            /* 等待手指松开，防止重复记录 */
            {
                u16 release_timeout = 0;
                while (PEN == 0 && release_timeout < 200)
                {
                    Delay_Ms(10);
                    IWDG_Feed();  /* 禁中断期间手动喂狗，防止 V3F 复位 */
                    release_timeout++;
                }
                tp_dev.sta = 0;
                /* 消抖：等待触摸屏电平稳定，防止残留电荷产生幻触 */
                Delay_Ms(80);
                IWDG_Feed();
                /* 确保 PEN 完全释放后再继续 */
                while (PEN == 0) {
                    Delay_Ms(10);
                    IWDG_Feed();
                }
            }

            switch (cnt)
            {
            case 1:
                TP_Drow_Touch_Point(20, 20, WHITE);
                TP_Drow_Touch_Point(lcddev.width - 20, 20, RED);
                break;

            case 2:
                TP_Drow_Touch_Point(lcddev.width - 20, 20, WHITE);
                TP_Drow_Touch_Point(20, lcddev.height - 20, RED);
                break;

            case 3:
                TP_Drow_Touch_Point(20, lcddev.height - 20, WHITE);
                TP_Drow_Touch_Point(lcddev.width - 20, lcddev.height - 20, RED);
                break;

            case 4:
                TP_Drow_Touch_Point(lcddev.width - 20, lcddev.height - 20, WHITE);
                LCD_Clear(WHITE);
                TP_Drow_Touch_Point(lcddev.width / 2, lcddev.height / 2, RED);
                break;

            case 5:
                s1 = pxy[1][0] - pxy[0][0];
                s3 = pxy[3][0] - pxy[2][0];
                s2 = pxy[3][1] - pxy[1][1];
                s4 = pxy[2][1] - pxy[0][1];

                px = (double)s1 / s3;
                py = (double)s2 / s4;

                if (px < 0) px = -px;
                if (py < 0) py = -py;

                if (px < 0.95 || px > 1.05 || py < 0.95 || py > 1.05 ||
                    abs(s1) > 4095 || abs(s2) > 4095 || abs(s3) > 4095 || abs(s4) > 4095 ||
                    abs(s1) == 0 || abs(s2) == 0 || abs(s3) == 0 || abs(s4) == 0)
                {
                    cnt = 0;
                    TP_Drow_Touch_Point(lcddev.width / 2, lcddev.height / 2, WHITE);
                    LCD_Clear(WHITE);
                    POINT_COLOR = RED;
                    LCD_ShowString(40, 100, 200, 16, 16, (u8 *)"Calibrate failed!");
                    LCD_ShowString(40, 120, 200, 16, 16, (u8 *)"Please try again...");
                    Delay_Ms(1000);
                    IWDG_Feed();  /* 禁中断期间手动喂狗 */
                    LCD_Clear(WHITE);
                    POINT_COLOR = BLUE;
                    LCD_ShowString(40, 40, 160, 100, 16, (u8 *)"Please click the cross");
                    LCD_ShowString(40, 60, 160, 100, 16, (u8 *)"on screen to calibrate");
                    TP_Drow_Touch_Point(20, 20, RED);
                    continue;
                }

                tp_dev.xfac = (float)(s1 + s3) / (2 * (lcddev.width - 40));
                tp_dev.yfac = (float)(s2 + s4) / (2 * (lcddev.height - 40));
                tp_dev.xc   = pxy[4][0];
                tp_dev.yc   = pxy[4][1];
                tp_dev.touchtype = 1;

                CMD_RDX = 0x90;
                CMD_RDY = 0xD0;

                /* 写入共享内存，通知 V5F 保存到 W25Q64 */
                TP_Save_ToShared();

                LCD_Clear(WHITE);
                POINT_COLOR = BLUE;
                LCD_ShowString(35, 110, 200, 16, 16, (u8 *)"Calibrate OK!");
                Delay_Ms(1000);
                IWDG_Feed();  /* 禁中断期间手动喂狗 */
                LCD_Clear(WHITE);
                tp_calibrating = 0;
                __asm volatile ("li t0, 0x1888; csrw mstatus, t0" ::: "t0", "memory");   /* 恢复中断 */
                return;
            }
        }

        Delay_Ms(10);
        IWDG_Feed();  /* 禁中断期间手动喂狗，防止 V3F 复位 */
        outtime++;
        if (outtime > 1000)
        {
            printf("触摸: 校准超时\r\n");
            __asm volatile ("li t0, 0x1888; csrw mstatus, t0" ::: "t0", "memory");   /* 恢复中断 */
            break;
        }
    }

    tp_calibrating = 0;
}

/* ======================== 初始化 ======================== */

u8 TP_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOF | RCC_HB2Periph_AFIO, ENABLE);

    /* 输出引脚：TCS(PF6), TCLK(PF7), TDIN(PF8) */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOF, &GPIO_InitStructure);

    /* 输入引脚：DOUT(PF9), PEN(PF10) */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9 | GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOF, &GPIO_InitStructure);

    tp_dev.touchtype = 1;
    CMD_RDX = 0x90;
    CMD_RDY = 0xD0;

    TP_Read_XY(&tp_dev.x[0], &tp_dev.y[0]);

    /* 等待 V5F 从 W25Q64 加载校准数据到共享内存
     * cal_status: 0=无数据, 1=V5F已从Flash加载, 2=已保存到Flash */
    {
        u32 waited = 0;
        while (SharedCalData.cal_status == 0 && waited < TP_CAL_WAIT_MS)
        {
            Delay_Ms(50);
            waited += 50;
        }

        if (TP_Load_FromShared())
        {
            printf("触摸: 校准数据加载成功\r\n");
            return 0;
        }
    }

    /* 无校准数据，执行校准 */
    printf("触摸: 无校准数据，开始校准\r\n");
    TP_Adjust();

    /* 校准完成后尝试加载 */
    if (TP_Load_FromShared())
    {
        printf("触摸: 校准完成并加载成功\r\n");
        /* 通知 V5F 播报校准完成 */
        SharedTtsData.tts_seq++;
        SharedTtsData.tts_pending = 1;
        strncpy((char *)SharedTtsData.text, "屏幕校准完成",
                sizeof(SharedTtsData.text) - 1);
        SharedTtsData.text[sizeof(SharedTtsData.text) - 1] = '\0';
        return 1;
    }

    /* 校准失败，优先使用 W25Q64 中保存的旧校准值 */
    if (SharedCalData.cal_status != 0) {
        tp_dev.xfac = SharedCalData.xfac;
        tp_dev.yfac = SharedCalData.yfac;
        tp_dev.xc   = SharedCalData.xc;
        tp_dev.yc   = SharedCalData.yc;
        printf("触摸: 校准失败，沿用旧校准值 (xfac=%.4f yfac=%.4f)\r\n",
               tp_dev.xfac, tp_dev.yfac);
    } else {
        /* 无任何有效数据，使用安全默认值防止除零 */
        printf("触摸: 警告 - 无旧数据，使用默认值\r\n");
        tp_dev.xfac = 1.0f;
        tp_dev.yfac = 1.0f;
        tp_dev.xc   = 2048;
        tp_dev.yc   = 2048;
    }
    /* 通知 V5F 播报 */
    SharedTtsData.tts_seq++;
    SharedTtsData.tts_pending = 1;
    if (SharedCalData.cal_status != 0) {
        strncpy((char *)SharedTtsData.text, "校准失败，沿用原有设置",
                sizeof(SharedTtsData.text) - 1);
    } else {
        strncpy((char *)SharedTtsData.text, "校准失败，使用默认设置",
                sizeof(SharedTtsData.text) - 1);
    }
    SharedTtsData.text[sizeof(SharedTtsData.text) - 1] = '\0';
    return 1;
}
