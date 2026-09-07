/**
 * @file    storage.c
 * @brief   Flash 存储管理 — 用户设置（双页轮换）+ 传感器日志（环形覆盖）
 */

#include "storage.h"
#include "w25qxx.h"
#include "debug.h"
#include <string.h>

/* ---- 常量 ---- */
#define SETTINGS_MAGIC      0xA55A
#define SETTINGS_VERSION    2
#define SETTINGS_SIZE       sizeof(UserSettings_t)

#define LOG_MAGIC           0x4C4F4731  /* "LOG1" */
#define LOG_INDEX_SIZE      sizeof(LogIndex_t)

#define LOG_AREA_SIZE       (W25QXX_TOTAL_SIZE - W25QXX_ADDR_LOG_START)
#define LOG_MAX_RECORDS     (LOG_AREA_SIZE / sizeof(SensorLog_t))

/* ---- 默认阈值 ---- */
#define DEFAULT_TEMP_THR    20
#define DEFAULT_HUMI_THR    50
#define DEFAULT_LIGHT_THR   0
#define DEFAULT_LOG_INTERVAL_S  60

/* ---- CRC16-CCITT ---- */
static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    uint32_t i;
    for(i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        uint8_t j;
        for(j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

/* ---- 内部：读写一个完整设置页 ---- */
static _Bool ReadSettingsPage(uint32_t addr, UserSettings_t *s)
{
    W25QXX_ReadBuffer(addr, (uint8_t *)s, SETTINGS_SIZE);
    if(s->magic != SETTINGS_MAGIC) return 0;
    uint16_t calc = crc16_ccitt((const uint8_t *)s, SETTINGS_SIZE - 2);
    return (calc == s->crc);
}

static void WriteSettingsPage(uint32_t addr, const UserSettings_t *s)
{
    UserSettings_t tmp = *s;
    tmp.crc = crc16_ccitt((const uint8_t *)&tmp, SETTINGS_SIZE - 2);

    W25QXX_EraseSector(addr);
    W25QXX_WritePage(addr, (const uint8_t *)&tmp, SETTINGS_SIZE);
}

/* ---- 日志索引 ---- */
static LogIndex_t g_log_idx;

static void LoadLogIndex(void)
{
    W25QXX_ReadBuffer(W25QXX_ADDR_LOG_INDEX, (uint8_t *)&g_log_idx, LOG_INDEX_SIZE);
    if(g_log_idx.magic != LOG_MAGIC)
    {
        /* 首次使用，初始化索引 */
        g_log_idx.magic     = LOG_MAGIC;
        g_log_idx.write_idx = 0;
        g_log_idx.count     = 0;
        g_log_idx.crc       = 0;

        W25QXX_EraseSector(W25QXX_ADDR_LOG_INDEX);
        W25QXX_WritePage(W25QXX_ADDR_LOG_INDEX,
                         (const uint8_t *)&g_log_idx, LOG_INDEX_SIZE);
        printf("[STORAGE] 日志索引已初始化\r\n");
    }
}

static void SaveLogIndex(void)
{
    g_log_idx.crc = crc16_ccitt((const uint8_t *)&g_log_idx, LOG_INDEX_SIZE - 4);
    W25QXX_EraseSector(W25QXX_ADDR_LOG_INDEX);
    W25QXX_WritePage(W25QXX_ADDR_LOG_INDEX,
                     (const uint8_t *)&g_log_idx, LOG_INDEX_SIZE);
}

/* ================================================================
 *  公开 API
 * ================================================================ */

void Storage_InitSPI(void)
{
    W25QXX_Init();
}

void Storage_Init(void)
{
    if(W25QXX_ReadID() == 0)
        W25QXX_Init();

    uint32_t id = W25QXX_ReadID();
    printf("[STORAGE] W25Q ID: 0x%06lX\r\n", (unsigned long)id);

    LoadLogIndex();
    printf("[STORAGE] 日志记录数: %lu, 写指针: %lu\r\n",
           (unsigned long)g_log_idx.count,
           (unsigned long)g_log_idx.write_idx);
}

/* ---- 用户设置 ---- */

_Bool Storage_LoadSettings(UserSettings_t *settings)
{
    UserSettings_t pageA, pageB;
    _Bool okA = ReadSettingsPage(W25QXX_ADDR_SETTINGS_A, &pageA);
    _Bool okB = ReadSettingsPage(W25QXX_ADDR_SETTINGS_B, &pageB);

    if(okA && okB)
    {
        /* 两个都有效，取序列号大的（更新的） */
        /* 用 count 做版本判断：写入次数越多越新 */
        *settings = (pageA.version >= pageB.version) ? pageA : pageB;
    }
    else if(okA)
    {
        *settings = pageA;
    }
    else if(okB)
    {
        *settings = pageB;
    }
    else
    {
        /* 两个都无效，使用默认值 */
        settings->magic           = SETTINGS_MAGIC;
        settings->version         = SETTINGS_VERSION;
        settings->temp_threshold  = DEFAULT_TEMP_THR;
        settings->humi_threshold  = DEFAULT_HUMI_THR;
        settings->light_threshold = DEFAULT_LIGHT_THR;
        settings->log_interval_s  = DEFAULT_LOG_INTERVAL_S;
        settings->crc             = 0;
        printf("[STORAGE] 无有效设置，使用默认值 T=%u H=%u L=%u\r\n",
               DEFAULT_TEMP_THR, DEFAULT_HUMI_THR, DEFAULT_LIGHT_THR);
        return 0;
    }

    printf("[STORAGE] 加载设置: T=%u H=%u L=%u I=%u\r\n",
           settings->temp_threshold, settings->humi_threshold,
           settings->light_threshold, settings->log_interval_s);
    return 1;
}

void Storage_SaveSettings(const UserSettings_t *settings)
{
    UserSettings_t tmp = *settings;
    tmp.magic   = SETTINGS_MAGIC;
    tmp.version++;

    /* 交替写入 A/B 页 */
    static uint8_t last_page = 0;
    uint32_t addr = (last_page == 0) ? W25QXX_ADDR_SETTINGS_A
                                     : W25QXX_ADDR_SETTINGS_B;
    WriteSettingsPage(addr, &tmp);
    last_page ^= 1;
    printf("[STORAGE] 保存设置到页%c: T=%u H=%u L=%u I=%u\r\n",
           (last_page == 0) ? 'B' : 'A',
           tmp.temp_threshold, tmp.humi_threshold,
           tmp.light_threshold, tmp.log_interval_s);
}

/* ---- WiFi 凭据 ---- */

#define WIFI_MAGIC  0xBEEF

static _Bool ReadWiFiPage(WiFiSettings_t *wifi)
{
    W25QXX_ReadBuffer(W25QXX_ADDR_WIFI, (uint8_t *)wifi, sizeof(WiFiSettings_t));
    if(wifi->magic != WIFI_MAGIC) return 0;
    uint16_t calc = crc16_ccitt((const uint8_t *)wifi, sizeof(WiFiSettings_t) - 2);
    return (calc == wifi->crc);
}

_Bool Storage_LoadWiFi(WiFiSettings_t *wifi)
{
    return ReadWiFiPage(wifi);
}

void Storage_SaveWiFi(const WiFiSettings_t *wifi)
{
    WiFiSettings_t tmp = *wifi;
    tmp.magic = WIFI_MAGIC;
    tmp.version = 1;
    tmp.crc = crc16_ccitt((const uint8_t *)&tmp, sizeof(WiFiSettings_t) - 2);

    W25QXX_EraseSector(W25QXX_ADDR_WIFI);
    W25QXX_WritePage(W25QXX_ADDR_WIFI, (const uint8_t *)&tmp, sizeof(WiFiSettings_t));
    printf("[STORAGE] 保存WiFi: SSID=%s\r\n", tmp.ssid);	/* 不打印密码 */
}

/* ---- 天气地点 ---- */

#define WEATHER_MAGIC  0xBEAD

static _Bool ReadWeatherPage(WeatherSettings_t *w)
{
    W25QXX_ReadBuffer(W25QXX_ADDR_WEATHER, (uint8_t *)w, sizeof(WeatherSettings_t));
    if(w->magic != WEATHER_MAGIC) return 0;
    uint16_t calc = crc16_ccitt((const uint8_t *)w, sizeof(WeatherSettings_t) - 2);
    return (calc == w->crc);
}

_Bool Storage_LoadWeather(WeatherSettings_t *w)
{
    return ReadWeatherPage(w);
}

void Storage_SaveWeather(const WeatherSettings_t *w)
{
    WeatherSettings_t tmp = *w;
    tmp.magic = WEATHER_MAGIC;
    tmp.version = 1;
    tmp.crc = crc16_ccitt((const uint8_t *)&tmp, sizeof(WeatherSettings_t) - 2);

    W25QXX_EraseSector(W25QXX_ADDR_WEATHER);
    W25QXX_WritePage(W25QXX_ADDR_WEATHER, (const uint8_t *)&tmp, sizeof(WeatherSettings_t));
    printf("[STORAGE] 保存天气地点: %s\r\n", tmp.location);
}

/* ---- 触摸校准数据 ---- */

_Bool Storage_LoadTPCal(TPCalData_t *cal)
{
    W25QXX_ReadBuffer(W25QXX_ADDR_TPCAL, (uint8_t *)cal, sizeof(TPCalData_t));
    if (cal->magic != TP_CAL_MAGIC) return 0;
    uint16_t calc = crc16_ccitt((const uint8_t *)cal, sizeof(TPCalData_t) - 4);
    return (calc == cal->crc);
}

void Storage_SaveTPCal(const TPCalData_t *cal)
{
    TPCalData_t tmp = *cal;
    tmp.magic = TP_CAL_MAGIC;
    tmp.crc = crc16_ccitt((const uint8_t *)&tmp, sizeof(TPCalData_t) - 4);

    W25QXX_EraseSector(W25QXX_ADDR_TPCAL);
    W25QXX_WritePage(W25QXX_ADDR_TPCAL, (const uint8_t *)&tmp, sizeof(TPCalData_t));
    printf("[STORAGE] 保存触摸校准数据: xfac=%.4f yfac=%.4f xc=%d yc=%d\r\n",
           tmp.xfac, tmp.yfac, tmp.xc, tmp.yc);
}

/* ---- 传感器日志 ---- */

void Storage_SaveSensorLog(int16_t temp_x10, uint16_t humi_x10, uint16_t light,
                           uint8_t year_off, uint8_t month, uint8_t day,
                           uint8_t hour, uint8_t minute)
{
    SensorLog_t rec;
    rec.year_off = year_off;
    rec.month    = month;
    rec.day      = day;
    rec.hour     = hour;
    rec.minute   = minute;
    /* 四舍五入到整数：242→240, 245→250, 247→250 */
    rec.temp_x10 = (temp_x10 >= 0) ? ((temp_x10 + 5) / 10 * 10)
                                   : ((temp_x10 - 5) / 10 * 10);
    rec.humi_x10 = (humi_x10 + 5) / 10 * 10;
    rec.light_h  = (light >> 8) & 0xFF;
    rec.light_l  = light & 0xFF;

    /* 计算写入地址：从日志数据区起始位置按记录大小偏移 */
    uint32_t addr = W25QXX_ADDR_LOG_START + g_log_idx.write_idx * sizeof(SensorLog_t);

    /* 每个扇区(4KB)容纳256条记录(16B/条)，写入新扇区前先擦除 */
    if ((g_log_idx.write_idx % 256) == 0)
    {
        W25QXX_EraseSector(addr);
    }

    /* 写入数据（Flash 按页256字节写，记录16字节，16整除256不跨页） */
    W25QXX_WritePage(addr, (const uint8_t *)&rec, sizeof(SensorLog_t));

    g_log_idx.write_idx++;
    g_log_idx.count++;

    /* 环形覆盖：写满后回到起始位置 */
    if(g_log_idx.write_idx >= LOG_MAX_RECORDS)
    {
        g_log_idx.write_idx = 0;
        printf("[STORAGE] 日志区已满，开始覆盖旧数据\r\n");
    }

    /* 每次写入都持久化索引，确保复位后数据不丢失 */
    SaveLogIndex();
}

uint32_t Storage_GetLogCount(void)
{
    return (g_log_idx.count > LOG_MAX_RECORDS) ? LOG_MAX_RECORDS
                                                : g_log_idx.count;
}

void Storage_ReadLogs(uint32_t start, uint32_t count, SensorLog_t *buf)
{
    uint32_t total = Storage_GetLogCount();
    if(start >= total) return;
    if(start + count > total) count = total - start;

    uint32_t data_area = W25QXX_ADDR_LOG_START;
    /* 最老记录的索引：已覆盖过则从 write_idx 开始，否则从 0 开始 */
    uint32_t oldest = (g_log_idx.count > LOG_MAX_RECORDS) ? g_log_idx.write_idx : 0;

    uint32_t i;
    for(i = 0; i < count; i++)
    {
        uint32_t pos = (oldest + start + i) % LOG_MAX_RECORDS;
        uint32_t addr = data_area + pos * sizeof(SensorLog_t);
        W25QXX_ReadBuffer(addr, (uint8_t *)&buf[i], sizeof(SensorLog_t));
    }
}

void Storage_ClearLogs(void)
{
    /* 先计算当前写入扇区地址（擦除索引前读取） */
    uint32_t sector_addr = W25QXX_ADDR_LOG_START +
                           (g_log_idx.write_idx * sizeof(SensorLog_t) / W25QXX_SECTOR_SIZE) * W25QXX_SECTOR_SIZE;

    /* 擦除日志索引扇区 */
    W25QXX_EraseSector(W25QXX_ADDR_LOG_INDEX);

    /* 擦除当前写入位置所在扇区 */
    W25QXX_EraseSector(sector_addr);

    /* 重新初始化索引 */
    g_log_idx.magic     = LOG_MAGIC;
    g_log_idx.write_idx = 0;
    g_log_idx.count     = 0;
    g_log_idx.crc       = 0;
    SaveLogIndex();

    printf("[STORAGE] 历史数据已清空（日志区%luKB）\r\n",
           (unsigned long)(W25QXX_TOTAL_SIZE - W25QXX_ADDR_LOG_START) / 1024);
}
