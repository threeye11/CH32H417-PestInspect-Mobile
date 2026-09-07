/********************************** (C) COPYRIGHT *******************************
 * File Name          : rtc.c
 * Description        : RTC driver for V3F core.
 *
 * RTC functions ported from CH32H417 EVT RTC_Calendar example (hardware.c).
 * Network time sync is handled by V5F and written to shared memory.
 *******************************************************************************/
#include "rtc.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 *  Global Variables
 * ================================================================ */
_calendar_obj calendar;

/* ---- drift compensation ---- */
rtc_drift_comp_t rtc_drift = {0};

/* ---- month-length table & week-lookup table (from official EVT) ---- */
u8 const table_week[12] = {0, 3, 3, 6, 1, 4, 6, 2, 5, 0, 3, 5};
const u8 mon_table[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/* ---- forward declarations ---- */
static void RTC_NVIC_Config(void);

/* ================================================================
 *  RTC_NVIC_Config  (static helper)
 * ================================================================ */
static void RTC_NVIC_Config(void)
{
    NVIC_SetPriority(RTC_IRQn, 0);
    NVIC_EnableIRQ(RTC_IRQn);
}

/* ================================================================
 *  RTC_Init  —  modified: no default time set, wait for network
 * ================================================================ */
u8 RTC_Init(void)
{
    u8 temp = 0;

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR | RCC_HB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_ClearITPendingBit(RTC_IT_ALR);
    RTC_ClearITPendingBit(RTC_IT_SEC);

    RCC_LSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET)
    {
        temp++;
        Delay_Ms(10);
    }
    if (temp >= 250)
        return 1;                   /* LSI timeout */

    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);

    for (int t = 0; t < 10; t++) { __NOP(); }

    RCC_RTCCLKCmd(ENABLE);
    RTC_WaitForLastTask();
    RTC_WaitForSynchro();

    /* NOTE: second interrupt disabled — sensor task polls RTC via RTC_Get() */
    RTC_ClearITPendingBit(RTC_IT_SEC);
    RTC_WaitForLastTask();

    RTC_EnterConfigMode();
    RTC_SetPrescaler(40000);            /* 1 Hz tick (LSI ~40 kHz / 40000 = 1) */
    RTC_WaitForLastTask();
    RTC_Set(2026, 6, 14, 12, 0, 0);     /* default time; will be synced from network later */
    RTC_ExitConfigMode();

    /* RTC_NVIC_Config() skipped — no second interrupt needed */
    RTC_Get();                          /* prime calendar struct */
    return 0;
}

/* ================================================================
 *  Is_Leap_Year
 * ================================================================ */
u8 Is_Leap_Year(u16 year)
{
    if (year % 4 == 0)
    {
        if (year % 100 == 0)
        {
            if (year % 400 == 0)
                return 1;
            else
                return 0;
        }
        else
            return 1;
    }
    else
        return 0;
}

/* ================================================================
 *  RTC_Set  —  set RTC counter from Y/M/D h:m:s
 * ================================================================ */
u8 RTC_Set(u16 syear, u8 smon, u8 sday, u8 hour, u8 min, u8 sec)
{
    u16 t;
    u32 seccount = 0;

    if (syear < 1970 || syear > 2099)
        return 1;

    for (t = 1970; t < syear; t++)
    {
        if (Is_Leap_Year(t))
            seccount += 31622400;
        else
            seccount += 31536000;
    }

    smon -= 1;
    for (t = 0; t < smon; t++)
    {
        seccount += (u32)mon_table[t] * 86400;
        if (Is_Leap_Year(syear) && t == 1)
            seccount += 86400;
    }
    seccount += (u32)(sday - 1) * 86400;
    seccount += (u32)hour * 3600;
    seccount += (u32)min * 60;
    seccount += sec;

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR | RCC_HB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetCounter(seccount);
    RTC_WaitForLastTask();
    return 0;
}

/* ================================================================
 *  RTC_Get  —  read counter, decompose into calendar struct
 * ================================================================ */
u8 RTC_Get(void)
{
    static u16 daycnt = 0;
    u32        timecount = 0;
    u32        temp = 0;
    u16        temp1 = 0;

    timecount = RTC_GetCounter();
    temp = timecount / 86400;

    if (daycnt != temp)
    {
        daycnt = temp;
        temp1 = 1970;
        while (temp >= 365)
        {
            if (Is_Leap_Year(temp1))
            {
                if (temp >= 366)
                    temp -= 366;
                else
                    break;
            }
            else
                temp -= 365;
            temp1++;
        }
        calendar.w_year = temp1;
        temp1 = 0;
        while (temp >= 28)
        {
            if (Is_Leap_Year(calendar.w_year) && temp1 == 1)
            {
                if (temp >= 29)
                    temp -= 29;
                else
                    break;
            }
            else
            {
                if (temp >= mon_table[temp1])
                    temp -= mon_table[temp1];
                else
                    break;
            }
            temp1++;
        }
        calendar.w_month = temp1 + 1;
        calendar.w_date = temp + 1;
    }

    temp = timecount % 86400;
    calendar.hour = temp / 3600;
    calendar.min  = (temp % 3600) / 60;
    calendar.sec  = (temp % 3600) % 60;
    calendar.week = RTC_Get_Week(calendar.w_year, calendar.w_month, calendar.w_date);

    return 0;
}

/* ================================================================
 *  RTC_Get_Week  —  Zeller-like weekday calculation (0=Sun)
 * ================================================================ */
u8 RTC_Get_Week(u16 year, u8 month, u8 day)
{
    u16 temp2;
    u8  yearH, yearL;

    yearH = year / 100;
    yearL = year % 100;
    if (yearH > 19)
        yearL += 100;

    temp2 = yearL + yearL / 4;
    temp2 = temp2 % 7;
    temp2 = temp2 + day + table_week[month - 1];
    if (yearL % 4 == 0 && month < 3)
        temp2--;

    return (temp2 % 7);
}

/* ================================================================
 *  RTC_FormatString  —  "HH:MM YYYY年M月D日 星期X"
 *  格式匹配 my_gui.c 中 update_time_info() 的解析逻辑：
 *   第一个空格前 → 时钟文本（顶部栏 + 首页大时钟）
 *   第一个空格后 → 日期文本（首页日期标签）
 * ================================================================ */
void RTC_FormatString(char *buf, uint8_t size)
{
    static const char *week_cn[] = {"日","一","二","三","四","五","六"};
    snprintf(buf, size, "%02d:%02d:%02d %d年%d月%d日 星期%s",
             calendar.hour, calendar.min, calendar.sec,
             calendar.w_year, calendar.w_month, calendar.w_date,
             week_cn[calendar.week]);
    buf[size - 1] = '\0';  /* 确保字符串以 '\0' 结尾 */
}

/* ================================================================
 *  Drift Compensation Functions
 * ================================================================ */

/* Initialize drift compensation tracking */
void RTC_InitDriftComp(void)
{
    rtc_drift.last_sync_timestamp = 0;
    rtc_drift.drift_accumulated = 0;
    rtc_drift.sync_count = 0;
    rtc_drift.last_drift = 0;
}

/* Get estimated drift in seconds since last sync */
int32_t RTC_GetEstimatedDrift(void)
{
    if (rtc_drift.sync_count == 0 || rtc_drift.last_sync_timestamp == 0)
    {
        return 0;
    }
    
    uint32_t elapsed = RTC_GetCounter() - rtc_drift.last_sync_timestamp;
    
    if (rtc_drift.sync_count >= 2 && elapsed > 0)
    {
        int32_t estimated_drift = (int32_t)((int64_t)rtc_drift.drift_accumulated * elapsed / 
                                            (int64_t)(rtc_drift.last_drift + 1));
        return estimated_drift;
    }
    
    return 0;
}

/* Apply drift compensation to calendar values */
void RTC_ApplyDriftCompensation(void)
{
    if (rtc_drift.sync_count < 2)
    {
        return;
    }
    
    int32_t estimated_drift = RTC_GetEstimatedDrift();
    
    if (estimated_drift == 0)
    {
        return;
    }
    
    int32_t total_seconds = calendar.hour * 3600 + calendar.min * 60 + calendar.sec;
    total_seconds -= estimated_drift;
    
    while (total_seconds < 0) total_seconds += 86400;
    while (total_seconds >= 86400) total_seconds -= 86400;
    
    calendar.hour = (uint8_t)(total_seconds / 3600);
    calendar.min = (uint8_t)((total_seconds % 3600) / 60);
    calendar.sec = (uint8_t)(total_seconds % 60);
}
