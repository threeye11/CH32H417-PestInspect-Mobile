/********************************** (C) COPYRIGHT *******************************
 * File Name          : rtc.h
 * Description        : RTC real-time clock driver header for V3F.
 *******************************************************************************/
#ifndef __RTC_H
#define __RTC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32h417.h"
#include <stdint.h>

/* ---- global calendar struct ---- */
typedef struct
{
    vu8  hour;
    vu8  min;
    vu8  sec;
    vu16 w_year;
    vu8  w_month;
    vu8  w_date;
    vu8  week;
} _calendar_obj;

extern _calendar_obj calendar;

/* ---- RTC drift compensation structure ---- */
typedef struct
{
    uint32_t last_sync_timestamp;
    int32_t  drift_accumulated;
    uint32_t sync_count;
    int32_t  last_drift;
} rtc_drift_comp_t;

extern rtc_drift_comp_t rtc_drift;

/* ---- RTC hardware functions ---- */
u8  RTC_Init(void);
u8  RTC_Set(u16 syear, u8 smon, u8 sday, u8 hour, u8 min, u8 sec);
u8  RTC_Get(void);
u8  RTC_Get_Week(u16 year, u8 month, u8 day);
u8  Is_Leap_Year(u16 year);

/* ---- utility ---- */
void RTC_FormatString(char *buf, uint8_t size);

/* ---- drift compensation ---- */
void RTC_InitDriftComp(void);
int32_t RTC_GetEstimatedDrift(void);
void RTC_ApplyDriftCompensation(void);

#ifdef __cplusplus
}
#endif

#endif /* __RTC_H */
