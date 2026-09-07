#ifndef __WATCHDOG_H
#define __WATCHDOG_H

#ifdef __cplusplus
 extern "C" {
#endif

void IWDG_FeedInit(uint16_t prer, uint16_t rlr);
void IWDG_Feed(void);

void WWDG_FeedInit(uint8_t tr, uint8_t wr, uint32_t prv);
void WWDG_Feed(void);

#ifdef __cplusplus
}
#endif

#endif
