#ifndef __W25QXX_H
#define __W25QXX_H

#include "ch32h417.h"
#include <stdint.h>

/* W25Q64 命令定义 */
#define W25QXX_CMD_WRITE_ENABLE        0x06
#define W25QXX_CMD_WRITE_DISABLE       0x04
#define W25QXX_CMD_READ_STATUS_REG1    0x05
#define W25QXX_CMD_WRITE_STATUS_REG    0x01
#define W25QXX_CMD_READ_DATA           0x03
#define W25QXX_CMD_PAGE_PROGRAM        0x02
#define W25QXX_CMD_SECTOR_ERASE        0x20    /* 4KB */
#define W25QXX_CMD_BLOCK_ERASE_32K     0x52
#define W25QXX_CMD_BLOCK_ERASE_64K     0xD8
#define W25QXX_CMD_CHIP_ERASE          0xC7
#define W25QXX_CMD_JEDEC_ID            0x9F
#define W25QXX_CMD_POWER_DOWN          0xB9
#define W25QXX_CMD_RELEASE_PD          0xAB

/* W25Q64 参数 */
#define W25QXX_PAGE_SIZE               256
#define W25QXX_SECTOR_SIZE             4096
#define W25QXX_TOTAL_SIZE              (8 * 1024 * 1024)  /* 8MB */

/* Flash 内存布局 — 配置在前，日志从 0x010000 开始 */
#define W25QXX_ADDR_SETTINGS_A         0x000000  /* 设置页A（4KB扇区） */
#define W25QXX_ADDR_SETTINGS_B         0x001000  /* 设置页B（4KB扇区） */
#define W25QXX_ADDR_WIFI               0x002000  /* WiFi凭据（4KB扇区内） */
#define W25QXX_ADDR_WEATHER            0x003000  /* 天气地点设置（4KB扇区内） */
#define W25QXX_ADDR_TPCAL              0x004000  /* 触摸校准数据（4KB扇区内） */
#define W25QXX_ADDR_LOG_INDEX          0x010000  /* 日志索引（4KB扇区） */
#define W25QXX_ADDR_LOG_START          0x011000  /* 传感器日志起始 */

/* API */
void     W25QXX_Init(void);
uint32_t W25QXX_ReadID(void);
uint8_t  W25QXX_ReadByte(uint32_t addr);
void     W25QXX_ReadBuffer(uint32_t addr, uint8_t *buf, uint32_t len);
void     W25QXX_WritePage(uint32_t addr, const uint8_t *buf, uint16_t len);
void     W25QXX_EraseSector(uint32_t addr);
void     W25QXX_EraseChip(void);

#endif
