/**
 * @file    net_task.c
 * @brief   网络任务 — ESP8266 + OneNET MQTT + 天气 + 网络对时
 *
 *          心跳策略：
 *          - 正常：60s 一次
 *          - 失败：15s 快速重试
 *          - 连续失败 3 次：触发重连
 *          - 心跳第2次失败后停止发送数据
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "shared.h"
#include "usart.h"
#include "esp8266.h"
#include "onenet.h"
#include "weather.h"
#include "tts_task.h"
#include "tw_tts.h"
#include "k230.h"
#include "storage.h"

extern weather_info_t g_weather;
extern volatile _Bool g_weather_announce;
extern char g_log_control_status[16];
extern volatile _Bool g_cloud_interval_pending;
char g_reported_ssid[33] = {0};  /* 当前WiFi SSID，供 onenet.c 上报 */
char g_weather_location[33] = {0};  /* 天气查询地点，供 Weather_Fetch 使用 */

/* ======================== 任务配置 ======================== */
#define NET_TASK_PRIO       4
#define NET_TASK_STK_SIZE   4096

/* ======================== 心跳参数 ======================== */
#define HB_INTERVAL_NORMAL   12000   /* 60s @ 5ms tick */
#define HB_INTERVAL_FAST     3000    /* 15s @ 5ms tick */
#define HB_FAIL_MAX          2       /* 连续失败次数触发重连 */
#define HB_RECONNECT_WAIT    4800    /* 重连失败后等待 24s */

/* ======================== 任务句柄 ======================== */
TaskHandle_t NetTask_Handler;

/* ======================== V5F 本地时间副本 ======================== */
/* 由 Time_ToShared / Periodic_SharedUpdate 初始化，主循环每秒自增 */
static uint16_t local_year = 0;
static uint8_t  local_month = 0, local_day = 0;
static uint8_t  local_hour = 0, local_minute = 0, local_second = 0;

/**
 * @brief  独立时间同步 — 天气HTTP失败时用轻量请求获取Date头
 */
static void SyncTime_Independent(void)
{
    extern char esp8266_http_date[];
    extern _Bool ESP8266_HTTP_Get(char *host, char *path, char *response, unsigned short max_len);

    if (SharedTimeData.time_valid != 0) return;
    if (esp8266_http_date[0] != '\0') return;

    /* 用 worldtimeapi 获取时间（只关心Date头，不解析body） */
    char resp[128];
    if (ESP8266_HTTP_Get("worldtimeapi.org", "/api/timezone/Asia/Shanghai", resp, sizeof(resp)) != 0)
    {
        printf("NET: 独立时间同步失败，使用本地RTC\r\n");
        /* 兜底：通知V3F时间无效但不阻塞，V3F会用本地RTC */
        SharedTimeData.time_valid = 0;
    }
}

/* ======================== 天气获取 + 播报 ======================== */
static void FetchAndAnnounceWeather(void)
{
    if (g_weather_location[0] == '\0')
    {
        /* 无地点：用默认城市获取，同步设置 g_weather_location 以便云端上报 */
        strncpy(g_weather_location, "北京", sizeof(g_weather_location) - 1);
        g_weather_location[sizeof(g_weather_location) - 1] = '\0';
        Weather_Fetch(g_weather_location);
        /* 播报提醒用户设置地点 */
        static _Bool no_loc_announced = 0;
        if (!no_loc_announced && TtsReady)
        {
            no_loc_announced = 1;
            TTS_Speak("请在网页端设置天气查询地点");
        }
        return;
    }
    /* 地点变更或数据无效时获取天气，避免不必要的 HTTP 请求 */
    if (g_weather_announce || !g_weather.valid)
        Weather_Fetch(g_weather_location);
    /* 云平台下发地点变更后首次获取成功时播报 */
    _Bool should_announce = g_weather_announce;
    g_weather_announce = 0;  /* 无论成功失败都清除，防止无限重试 */
    if (should_announce && g_weather.valid)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "天气地点已更新为%s", g_weather.city);
        TTS_Speak(buf);
        vTaskDelay(pdMS_TO_TICKS(1500));
        snprintf(buf, sizeof(buf), "今日天气%s，温度%s摄氏度", g_weather.weather, g_weather.temp);
        TTS_Speak(buf);
    }
}

/* ======================== 内部函数 ======================== */

/**
 * @brief  将天气数据写入共享内存供 V3F 显示
 */
static void Weather_ToShared(void)
{
    if (g_weather.valid)
    {
        strncpy((char *)SharedWeatherData.city, g_weather.city, sizeof(SharedWeatherData.city) - 1);
        SharedWeatherData.city[sizeof(SharedWeatherData.city) - 1] = '\0';
        strncpy((char *)SharedWeatherData.weather, g_weather.weather, sizeof(SharedWeatherData.weather) - 1);
        SharedWeatherData.weather[sizeof(SharedWeatherData.weather) - 1] = '\0';
        SharedWeatherData.temp_outdoor = (int8_t)atoi(g_weather.temp);
        SharedWeatherData.weather_valid = 1;
        SharedWeatherData.weather_seq++;
        printf("NET: 天气已写入共享内存\r\n");
    }
    else
    {
        /* 无地点信息时写入占位数据，防止 V3F 超时进入离线模式 */
        strncpy((char *)SharedWeatherData.city, "-", sizeof(SharedWeatherData.city) - 1);
        SharedWeatherData.city[sizeof(SharedWeatherData.city) - 1] = '\0';
        strncpy((char *)SharedWeatherData.weather, "-", sizeof(SharedWeatherData.weather) - 1);
        SharedWeatherData.weather[sizeof(SharedWeatherData.weather) - 1] = '\0';
        SharedWeatherData.temp_outdoor = 0;
        SharedWeatherData.weather_valid = 1;
        SharedWeatherData.weather_seq++;
        printf("NET: 无地点，写入占位数据\r\n");
    }
}

/**
 * @brief  将网络时间写入共享内存供 V3F RTC 对时
 */
static void Time_ToShared(void)
{
    extern char esp8266_http_date[];

    if (SharedTimeData.time_valid != 0)
        return;

    if (esp8266_http_date[0] == '\0')
    {
        return;
    }

    unsigned int y, mo, d, h, mi, s;
    char mon_str[4];
    int ret = sscanf(esp8266_http_date, "%*[^,], %2u %3s %4u %2u:%2u:%2u", &d, mon_str, &y, &h, &mi, &s);
    if (ret == 6)
    {
        const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        for (mo = 0; mo < 12; mo++) {
            if (strncmp(mon_str, months[mo], 3) == 0) { mo++; break; }
        }
        if (mo > 12) mo = 0;  /* 未匹配到月份，设为0表示无效 */
        h = (h + 8) % 24;

        SharedTimeData.year = (uint16_t)y;
        SharedTimeData.month = (uint8_t)mo;
        SharedTimeData.day = (uint8_t)d;
        SharedTimeData.hour = (uint8_t)h;
        SharedTimeData.minute = (uint8_t)mi;
        SharedTimeData.second = (uint8_t)s;
        SharedTimeData.time_valid = 1;
        SharedTimeData.time_seq++;
        /* 同步到本地副本 */
        local_year = (uint16_t)y;
        local_month = (uint8_t)mo;
        local_day = (uint8_t)d;
        local_hour = (uint8_t)h;
        local_minute = (uint8_t)mi;
        printf("NET: 网络时间已写入 %04u-%02u-%02u %02u:%02u:%02u\r\n", y, mo, d, h, mi, s);
    }
    else
    {
        printf("NET: 时间解析失败\r\n");
    }
}

/**
 * @brief  定期刷新天气和时间到共享内存
 */
static void Periodic_SharedUpdate(unsigned short *weather_cnt)
{
    extern char esp8266_http_date[];
    (*weather_cnt)++;
    if (*weather_cnt >= 8)
    {
        *weather_cnt = 0;
        FetchAndAnnounceWeather();
        Weather_ToShared();

        if (esp8266_http_date[0] == '\0' && SharedTimeData.time_valid == 0)
        {
            /* 天气HTTP失败，尝试独立时间同步 */
            SyncTime_Independent();
        }
        if (esp8266_http_date[0] != '\0')
        {
            unsigned int y, mo, d, h, mi, s;
            char mon_str[4];
            if (sscanf(esp8266_http_date, "%*[^,], %2u %3s %4u %2u:%2u:%2u", &d, mon_str, &y, &h, &mi, &s) == 6)
            {
                const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
                for (mo = 0; mo < 12; mo++) {
                    if (strncmp(mon_str, months[mo], 3) == 0) { mo++; break; }
                }
                if (mo > 12) mo = 0;
                h = (h + 8) % 24;
                SharedTimeData.year = (uint16_t)y;
                SharedTimeData.month = (uint8_t)mo;
                SharedTimeData.day = (uint8_t)d;
                SharedTimeData.hour = (uint8_t)h;
                SharedTimeData.minute = (uint8_t)mi;
                SharedTimeData.second = (uint8_t)s;
                SharedTimeData.time_seq++;
                /* 同步到本地副本 */
                local_year = (uint16_t)y;
                local_month = (uint8_t)mo;
                local_day = (uint8_t)d;
                local_hour = (uint8_t)h;
                local_minute = (uint8_t)mi;
            }
        }
    }
}

/* ======================== 任务主函数 ======================== */
static void net_task(void *pvParameters)
{
    TickType_t last_send_tick = 0;
    unsigned short hb_cnt = 0;
    unsigned short hb_threshold = HB_INTERVAL_NORMAL;
    unsigned short weather_cnt = 0;
    unsigned char  hb_fail_cnt = 0;
    static char saved_pass[65] = {0};
    static char saved_ssid[33] = {0};

    /* ---- 网络初始化 ---- */
    printf("NET: 初始化USART2...\r\n");
    USART2_Init(115200);

    printf("NET: 初始化设备引脚...\r\n");
    Device_GPIO_Init();

    /* 仅初始化 SPI3，用于提前读取 WiFi 凭据和触摸校准 */
    printf("NET: 初始化存储模块(SPI)...\r\n");
    Storage_InitSPI();
    Delay_Ms(200);  /* 等待 AFIO remap 稳定后再操作 USART2 */

    /* 提前加载触摸校准数据到共享内存（必须在网络操作之前，否则 V3F 5 秒超时） */
    {
        TPCalData_t cal;
        if (Storage_LoadTPCal(&cal))
        {
            SharedCalData.xfac = cal.xfac;
            SharedCalData.yfac = cal.yfac;
            SharedCalData.xc   = cal.xc;
            SharedCalData.yc   = cal.yc;
            SharedCalData.cal_status = 1;   /* 已加载 */
            printf("NET: 触摸校准数据已加载 (xfac=%.4f yfac=%.4f xc=%d yc=%d)\r\n",
                   cal.xfac, cal.yfac, cal.xc, cal.yc);
        }
        else
        {
            SharedCalData.cal_status = 0;   /* 无数据 */
            printf("NET: 无触摸校准数据\r\n");
        }
    }

    {
        WiFiSettings_t wifi;
        if(Storage_LoadWiFi(&wifi) && wifi.ssid[0] != '\0')
        {
            printf("NET: 从Flash加载WiFi SSID=%s\r\n", wifi.ssid);	/* 不打印密码 */
            strncpy(saved_ssid, wifi.ssid, sizeof(saved_ssid) - 1);
            strncpy(saved_pass, wifi.password, sizeof(saved_pass) - 1);
            strncpy(g_reported_ssid, saved_ssid, sizeof(g_reported_ssid) - 1);
        }
        else
        {
            printf("NET: 无WiFi凭据，进入离线模式\r\n");
            SharedTimeData.wifi_ok = 0;
            SharedWiFiData.wifi_connected = 0;
            saved_ssid[0] = '\0';
        }
    }

    if(saved_ssid[0] != '\0')
    {
        printf("NET: 初始化ESP8266...\r\n");
        ESP8266_Init(saved_ssid, saved_pass);

        printf("NET: 连接OneNET...\r\n");
        if (OneNet_DevLink() == 0)
        {
            SharedTimeData.wifi_ok = 1;
            SharedWiFiData.wifi_connected = 1;
            OneNet_SetConnected(1);

            OneNET_Subscribe();

            /* 加载天气地点设置（必须在首次天气获取之前） */
            {
                WeatherSettings_t ws;
                if (Storage_LoadWeather(&ws) && ws.location[0] != '\0')
                {
                    strncpy(g_weather_location, ws.location, sizeof(g_weather_location) - 1);
                    printf("NET: 天气地点=%s\r\n", g_weather_location);
                }
                else
                {
                    printf("NET: 无天气地点设置\r\n");
                }
            }

            /* 首次获取天气和时间 */
            printf("NET: 获取天气...\r\n");
            FetchAndAnnounceWeather();
            Weather_ToShared();

            extern char esp8266_http_date[];
            if (esp8266_http_date[0] == '\0')
            {
                /* 天气HTTP失败，立即尝试独立时间同步 */
                SyncTime_Independent();
            }
            Time_ToShared();
        }
        else
        {
            printf("WARN: OneNET连接失败\r\n");
            SharedTimeData.wifi_ok = 0;
            SharedWiFiData.wifi_connected = 0;
            OneNet_SetConnected(0);
        }
    }

    /* 完整存储初始化（WiFi 连接完成后再做，避免 SPI 干扰） */
    Storage_Init();

    {
        UserSettings_t saved;
        memset(&saved, 0, sizeof(saved));
        if(Storage_LoadSettings(&saved))
        {
            SharedThresholdData.temp_threshold  = saved.temp_threshold;
            SharedThresholdData.humi_threshold  = saved.humi_threshold;
            SharedThresholdData.light_threshold = saved.light_threshold;
            SharedThresholdData.mute = saved.mute;
            SharedThresholdData.tts_volume = saved.tts_volume;
            SharedThresholdData.tts_tone = saved.tts_tone;
            SharedThresholdData.tts_speed = saved.tts_speed;
            SharedThresholdData.log_interval_s = saved.log_interval_s;
            /* TTS硬件设置由主循环检测变化后应用，此处不直接调用 */
        }
        else
        {
            SharedThresholdData.temp_threshold  = 20;
            SharedThresholdData.humi_threshold  = 50;
            SharedThresholdData.light_threshold = 0;
            SharedThresholdData.mute = 0;
            SharedThresholdData.tts_volume = 5;
            SharedThresholdData.tts_tone = 5;
            SharedThresholdData.tts_speed = 5;
            SharedThresholdData.log_interval_s = 60;
        }
        SharedThresholdData.threshold_seq = 1;
        printf("NET: 阈值 T=%u H=%u L=%u M=%u V=%u Tn=%u Sp=%u I=%u\r\n",
               SharedThresholdData.temp_threshold,
               SharedThresholdData.humi_threshold,
               SharedThresholdData.light_threshold,
               SharedThresholdData.mute,
               SharedThresholdData.tts_volume,
               SharedThresholdData.tts_tone,
               SharedThresholdData.tts_speed,
               SharedThresholdData.log_interval_s);
    }

    printf("NET: 初始化K230...\r\n");
    K230_Init(921600);

    /* 初始化病害检测共享内存默认值 */
    memset((void *)&SharedPestData, 0, sizeof(SharedPestData));
    strncpy((char *)SharedPestData.plant, "-", sizeof(SharedPestData.plant) - 1);
    SharedPestData.pest_valid = 0;
    SharedPestData.pest_seq = 0;
    printf("NET: 病害检测共享内存初始化完成\r\n");

    /* ---- 主循环 ---- */
    uint8_t last_threshold_seq = SharedThresholdData.threshold_seq;
    /* 保存 Flash 加载的阈值，用于云推送覆盖后恢复 */
    uint16_t flash_temp_thr  = SharedThresholdData.temp_threshold;
    uint16_t flash_humi_thr  = SharedThresholdData.humi_threshold;
    uint16_t flash_light_thr = SharedThresholdData.light_threshold;
    _Bool threshold_synced = 0;  /* 0=未同步, 1=已同步（云平台已有正确值） */
    uint32_t sync_start_tick = xTaskGetTickCount();  /* 超时保护计时起点 */
    uint8_t last_wifi_cmd_seq = SharedWiFiData.wifi_cmd_seq;
    uint8_t last_cal_seq = SharedCalData.cal_seq;
    uint8_t offline_announced = 0;  /* 防止重复播报网络断开 */
    while (1)
    {
        /* 检测 V3F 配网命令：保存凭据并重连 WiFi */
        uint8_t cur_wifi_cmd_seq = SharedWiFiData.wifi_cmd_seq;
        if (cur_wifi_cmd_seq != last_wifi_cmd_seq)
        {
            last_wifi_cmd_seq = cur_wifi_cmd_seq;
            if (SharedWiFiData.ssid[0] != '\0')
            {
                /* 保存到 Flash */
                WiFiSettings_t wifi;
                memset(&wifi, 0, sizeof(wifi));
                strncpy(wifi.ssid, SharedWiFiData.ssid, sizeof(wifi.ssid) - 1);
                strncpy(wifi.password, SharedWiFiData.password, sizeof(wifi.password) - 1);
                Storage_SaveWiFi(&wifi);

                /* 更新缓存供后续重连使用 */
                strncpy(saved_ssid, wifi.ssid, sizeof(saved_ssid) - 1);
                strncpy(saved_pass, wifi.password, sizeof(saved_pass) - 1);
                strncpy(g_reported_ssid, saved_ssid, sizeof(g_reported_ssid) - 1);

                /* 重连 WiFi */
                SharedWiFiData.wifi_connected = 0;
                SharedTimeData.wifi_ok = 0;
                printf("NET: WiFi重连 SSID=%s\r\n", wifi.ssid);	/* 不打印密码 */
                TTS_Speak("正在重新连接网络");
                ESP8266_Init(wifi.ssid, wifi.password);
                if (OneNet_DevLink() == 0)
                {
                    SharedTimeData.wifi_ok = 1;
                    OneNET_Subscribe();
                    SharedWiFiData.wifi_connected = 1;
                    OneNet_SetConnected(1);
                    FetchAndAnnounceWeather();
                    Weather_ToShared();
                    Time_ToShared();
                    printf("NET: WiFi重连成功\r\n");
                    TTS_Speak("网络连接成功");
                }
                else
                {
                    SharedTimeData.wifi_ok = 0;
                    SharedWiFiData.wifi_connected = 0;
                    OneNet_SetConnected(0);
                    TTS_Speak("网络连接失败");
                }
            }
        }

        unsigned char *revData = ESP8266_GetIPD(0);
        if (revData != NULL)
        {
            OneNet_RevPro(revData);
        }

        /* 地点变更后立即获取天气 + 播报 + 更新共享内存（不等待心跳周期） */
        if (g_weather_announce)
        {
            FetchAndAnnounceWeather();
            Weather_ToShared();
        }

        /* 检测 V3F 天气地点设置命令 */
        {
            static uint8_t last_weather_cmd_seq = 0;
            static _Bool weather_cmd_inited = 0;
            if (!weather_cmd_inited)
            {
                /* 首次运行：同步为当前共享内存值，跳过残留的旧命令 */
                last_weather_cmd_seq = SharedWeatherCmd.weather_cmd_seq;
                weather_cmd_inited = 1;
            }
            uint8_t cur_seq = SharedWeatherCmd.weather_cmd_seq;
            if (cur_seq != last_weather_cmd_seq && SharedWeatherCmd.location[0] != '\0')
            {
                last_weather_cmd_seq = cur_seq;
                SharedWeatherCmd.weather_cmd_status = 1;  /* 处理中 */

                strncpy(g_weather_location, SharedWeatherCmd.location, sizeof(g_weather_location) - 1);
                g_weather_location[sizeof(g_weather_location) - 1] = '\0';
                printf("NET: V3F天气地点设置=%s\r\n", g_weather_location);

                /* 保存到 Flash */
                WeatherSettings_t ws;
                ws.magic = 0xBEAD;
                ws.version = 1;
                strncpy(ws.location, g_weather_location, sizeof(ws.location) - 1);
                ws.location[sizeof(ws.location) - 1] = '\0';
                Storage_SaveWeather(&ws);

                /* 获取天气并播报 */
                g_weather_announce = 1;
                FetchAndAnnounceWeather();
                Weather_ToShared();

                /* 回写中文城市名到 V3F */
                if (g_weather.valid && g_weather.city[0] != '\0')
                {
                    strncpy(SharedWeatherCmd.location_cn, g_weather.city,
                            sizeof(SharedWeatherCmd.location_cn) - 1);
                    SharedWeatherCmd.location_cn[sizeof(SharedWeatherCmd.location_cn) - 1] = '\0';
                }

                SharedWeatherCmd.weather_cmd_status = 2;  /* 完成 */
                printf("NET: 天气地点设置完成\r\n");
            }
        }

        /* 检测 V3F 触摸校准命令：保存校准数据到 W25Q64 */
        {
            uint8_t cur_cal_seq = SharedCalData.cal_seq;
            if (cur_cal_seq != last_cal_seq)
            {
                last_cal_seq = cur_cal_seq;
                TPCalData_t cal;
                cal.magic = TP_CAL_MAGIC;
                cal.xfac  = SharedCalData.xfac;
                cal.yfac  = SharedCalData.yfac;
                cal.xc    = SharedCalData.xc;
                cal.yc    = SharedCalData.yc;
                cal.crc   = 0;  /* Storage_SaveTPCal 内部计算 CRC */
                Storage_SaveTPCal(&cal);
                SharedCalData.cal_status = 2;   /* 保存完成 */
                printf("NET: 触摸校准数据已保存到 W25Q64\r\n");
            }
        }

        /* 检测阈值变化：可能是云推送覆盖，也可能是用户网页修改 */
        uint8_t cur_threshold_seq = SharedThresholdData.threshold_seq;
        if (cur_threshold_seq != last_threshold_seq && hb_fail_cnt < 2)
        {
            last_threshold_seq = cur_threshold_seq;

            if(!threshold_synced)
            {
                /* 云推送覆盖了 Flash 值，恢复并重新上传覆盖云平台 */
                SharedThresholdData.temp_threshold  = flash_temp_thr;
                SharedThresholdData.humi_threshold  = flash_humi_thr;
                SharedThresholdData.light_threshold = flash_light_thr;
                SharedThresholdData.threshold_seq++;
                threshold_synced = 1;
                printf("NET: 恢复Flash阈值并同步云平台 T=%u H=%u L=%u\r\n",
                       flash_temp_thr, flash_humi_thr, flash_light_thr);
            }
            else
            {
                /* 用户通过网页修改阈值，更新本地缓存并保存到 Flash */
                flash_temp_thr  = SharedThresholdData.temp_threshold;
                flash_humi_thr  = SharedThresholdData.humi_threshold;
                flash_light_thr = SharedThresholdData.light_threshold;
                printf("NET: 网页修改阈值，保存 T=%u H=%u L=%u\r\n",
                       flash_temp_thr, flash_humi_thr, flash_light_thr);
            }

            float t = SharedSensorData.temperature_x10 / 10.0f;
            float h = SharedSensorData.humidity_x10 / 10.0f;
            float l = (float)SharedSensorData.light;
            OneNet_SendData(t, h, l);

            /* 保存到 Flash */
            UserSettings_t saved;
            saved.magic           = 0xA55A;
            saved.version         = 2;
            saved.temp_threshold  = SharedThresholdData.temp_threshold;
            saved.humi_threshold  = SharedThresholdData.humi_threshold;
            saved.light_threshold = SharedThresholdData.light_threshold;
            saved.mute            = SharedThresholdData.mute;
            saved.tts_volume      = SharedThresholdData.tts_volume;
            saved.tts_tone        = SharedThresholdData.tts_tone;
            saved.tts_speed       = SharedThresholdData.tts_speed;
            saved.log_interval_s  = SharedThresholdData.log_interval_s;
            Storage_SaveSettings(&saved);

            /* 检测间隔变化并更新 log_control 状态供云端上报 */
            {
                static uint16_t last_interval = 0;
                uint16_t cur = SharedThresholdData.log_interval_s;
                if (cur != last_interval)
                {
                    last_interval = cur;
                    snprintf(g_log_control_status, sizeof(g_log_control_status),
                             "interval=%umin", cur / 60);
                    printf("NET: 间隔变更 status=%s\r\n", g_log_control_status);
                    /* 云端下发间隔命令时播报 TTS（V3F LVGL 修改由 V3F 自行播报） */
                    if (g_cloud_interval_pending && TtsReady)
                    {
                        g_cloud_interval_pending = 0;
                        char tts_buf[32];
                        snprintf(tts_buf, sizeof(tts_buf), "保存间隔已设为%d分钟", cur / 60);
                        TTS_Speak(tts_buf);
                    }
                }
            }
        }

        /* 检测 TTS 设置变化：应用硬件 + 播报提示 */
        if (TtsReady)
        {
            static uint8_t last_mute = 0xFF, last_vol = 0xFF, last_tone = 0xFF, last_spd = 0xFF;
            static uint8_t tts_inited = 0;
            if (!tts_inited)
            {
                tts_inited = 1;
                last_mute = SharedThresholdData.mute;
                last_vol = SharedThresholdData.tts_volume;
                last_tone = SharedThresholdData.tts_tone;
                last_spd = SharedThresholdData.tts_speed;
                /* 首次同步：应用Flash值到TTS硬件，不播报 */
                TW_TTS_SetVolume(last_vol);
                TW_TTS_SetTone(last_tone);
                TW_TTS_SetSpeed(last_spd);
            }
            if (SharedThresholdData.mute != last_mute)
            {
                last_mute = SharedThresholdData.mute;
                if (last_mute)
                    TTS_Speak("已进入静音模式");
                else
                    TTS_Speak("已关闭静音模式");
            }
            if (SharedThresholdData.tts_volume != last_vol)
            {
                last_vol = SharedThresholdData.tts_volume;
                TW_TTS_SetVolume(last_vol);
                char buf[24];
                snprintf(buf, sizeof(buf), "音量已设为%d", last_vol);
                TTS_Speak(buf);
            }
            if (SharedThresholdData.tts_tone != last_tone)
            {
                last_tone = SharedThresholdData.tts_tone;
                TW_TTS_SetTone(last_tone);
                char buf[24];
                snprintf(buf, sizeof(buf), "语调已设为%d", last_tone);
                TTS_Speak(buf);
            }
            if (SharedThresholdData.tts_speed != last_spd)
            {
                last_spd = SharedThresholdData.tts_speed;
                TW_TTS_SetSpeed(last_spd);
                char buf[24];
                snprintf(buf, sizeof(buf), "语速已设为%d", last_spd);
                TTS_Speak(buf);
            }
        }

        /* K230 通用命令轮询（所有命令统一走 k230_cmd_seq） */
        {
            static uint8_t last_k230_cmd_seq = 0;
            uint8_t cmd_seq = SharedPestData.k230_cmd_seq;
            if (cmd_seq != last_k230_cmd_seq) {
                last_k230_cmd_seq = cmd_seq;
                uint8_t kcmd = SharedPestData.k230_cmd;
                K230_SendCmd(kcmd);
                printf("K230: 发送命令 0x%02X\r\n", kcmd);
                /* k230_cmd=1 开始检测, k230_cmd=2 停止检测 → 同步 pest_detect_cmd */
                if (kcmd == K230_CMD_START) {
                    SharedPestData.pest_detect_cmd = 1;
                    TTS_Speak("开始检测");
                }
                else if (kcmd == K230_CMD_STOP) {
                    SharedPestData.pest_detect_cmd = 0;
                    TTS_Speak("已停止检测");
                }
            }
        }

        /* 传感器数据上传（5s），仅在连接正常时发送 */
        {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_send_tick) >= pdMS_TO_TICKS(5000))
            {
                last_send_tick = now;
                if (hb_fail_cnt < 2)
                {
                    float t = SharedSensorData.temperature_x10 / 10.0f;
                    float h = SharedSensorData.humidity_x10 / 10.0f;
                    float l = (float)SharedSensorData.light;
                    OneNet_SendData(t, h, l);
                }
            }
        }

        /* 本地时钟每秒自增（基于 tick 计时） */
        {
            static TickType_t last_sec_tick = 0;
            TickType_t now = xTaskGetTickCount();
            if ((now - last_sec_tick) >= pdMS_TO_TICKS(1000) && local_year >= 2024)
            {
                last_sec_tick = now;
                local_second++;
                if (local_second >= 60) { local_second = 0; local_minute++; }
                if (local_minute >= 60) { local_minute = 0; local_hour++; }
                if (local_hour   >= 24) { local_hour   = 0; local_day++; }
                /* 简化处理：每月按31天算，日志精度足够 */
                if (local_day    >= 31) { local_day    = 0; local_month++; }
                if (local_month  >= 13) { local_month  = 1; local_year++; }
            }
        }

        /* 按设定间隔记录传感器数据到 Flash */
        {
            static TickType_t last_log_tick = 0;
            uint16_t interval = SharedThresholdData.log_interval_s;
            if (interval < 10) interval = 10;  /* 最小10秒 */
            TickType_t now = xTaskGetTickCount();
            if ((now - last_log_tick) >= pdMS_TO_TICKS((uint32_t)interval * 1000))
            {
                last_log_tick = now;

                /* 仅在传感器数据有效时保存 */
                if (SharedSensorData.valid)
                {
                    /* 优先用 SharedTimeData（V3F RTC），否则用本地时钟兜底 */
                    uint16_t yr;
                    uint8_t mo, dy, hr, mi;
                    if (SharedTimeData.year >= 2024)
                    {
                        yr = SharedTimeData.year;
                        mo = SharedTimeData.month;
                        dy = SharedTimeData.day;
                        hr = SharedTimeData.hour;
                        mi = SharedTimeData.minute;
                    }
                    else
                    {
                        yr = local_year;
                        mo = local_month;
                        dy = local_day;
                        hr = local_hour;
                        mi = local_minute;
                    }

                    if (yr >= 2024)
                    {
                        int16_t t_rec  = SharedSensorData.temperature_x10;
                        uint16_t h_rec = SharedSensorData.humidity_x10;
                        uint16_t l_rec = SharedSensorData.light;
                        uint8_t year_off = (uint8_t)(yr - 2024);

                        Storage_SaveSensorLog(t_rec, h_rec, l_rec,
                                              year_off, mo, dy, hr, mi);
                    }
                }
                else
                {
                    printf("NET: 跳过日志（time=%u sensor=%u）\r\n",
                           SharedTimeData.time_valid, SharedSensorData.valid);
                }
            }
        }

        /* V3F 手动触发保存当前传感器数据 */
        if (SharedLogData.save_sensor_log == 1)
        {
            SharedLogData.save_sensor_log = 0;
            if (SharedSensorData.valid)
            {
                uint16_t yr;
                uint8_t mo, dy, hr, mi;
                if (SharedTimeData.year >= 2024)
                {
                    yr = SharedTimeData.year;
                    mo = SharedTimeData.month;
                    dy = SharedTimeData.day;
                    hr = SharedTimeData.hour;
                    mi = SharedTimeData.minute;
                }
                else
                {
                    yr = local_year;
                    mo = local_month;
                    dy = local_day;
                    hr = local_hour;
                    mi = local_minute;
                }
                if (yr >= 2024)
                {
                    int16_t t_rec  = SharedSensorData.temperature_x10;
                    uint16_t h_rec = SharedSensorData.humidity_x10;
                    uint16_t l_rec = SharedSensorData.light;
                    uint8_t year_off = (uint8_t)(yr - 2024);
                    Storage_SaveSensorLog(t_rec, h_rec, l_rec,
                                          year_off, mo, dy, hr, mi);
                    printf("NET: V3F手动保存传感器数据完成 T=%d H=%u L=%u\r\n",
                           t_rec, h_rec, l_rec);
                }
            }
            SharedLogData.save_sensor_log = 2;  /* 通知V3F保存完成 */
            if (TtsReady) TTS_Speak("数据保存完成");
        }

        /* V3F 手动触发清空所有历史数据 */
        if (SharedLogData.save_sensor_log == 3)
        {
            SharedLogData.save_sensor_log = 0;
            Storage_ClearLogs();
            SharedLogData.log_resp_seq++;
            SharedLogData.log_total = 0;
            SharedLogData.save_sensor_log = 4;  /* 通知V3F清空完成 */
            printf("NET: V3F清空历史数据完成\r\n");
            if (TtsReady) TTS_Speak("历史数据已清空");
        }

        /* 发送失败时立即触发重连 */
        if (esp8266_send_fail)
        {
            esp8266_send_fail = 0;
            SharedTimeData.wifi_ok = 0;
            SharedWiFiData.wifi_connected = 0;
            OneNet_SetConnected(0);
            if (!offline_announced)
            {
                TTS_Speak("网络连接异常");
                offline_announced = 1;
            }
            printf("NET: 发送失败，尝试重连...\r\n");
            if (Net_Reconnect(saved_ssid, saved_pass) == 0)
            {
                printf("NET: 重连成功\r\n");
                SharedTimeData.wifi_ok = 1;
                SharedWiFiData.wifi_connected = 1;
                OneNet_SetConnected(1);
                offline_announced = 0;
                hb_fail_cnt = 0;
                hb_threshold = HB_INTERVAL_NORMAL;
                FetchAndAnnounceWeather();
                Weather_ToShared();
                Time_ToShared();
                TTS_Speak("网络已恢复连接");
            }
            else
            {
                printf("NET: 重连失败\r\n");
                SharedTimeData.wifi_ok = 0;
                SharedWiFiData.wifi_connected = 0;
                OneNet_SetConnected(0);
                hb_threshold = HB_RECONNECT_WAIT;
            }
        }

        /* 心跳检测 */
        hb_cnt++;
        if (hb_cnt >= hb_threshold)
        {
            hb_cnt = 0;

            if (OneNet_HeartBeat() == 0)
            {
                if (hb_fail_cnt > 0)
                    printf("NET: 心跳恢复，重新连接成功\r\n");
                else
                    printf("NET: 心跳正常\r\n");
                hb_fail_cnt = 0;
                hb_threshold = HB_INTERVAL_NORMAL;
                OneNet_SetConnected(1);

                Periodic_SharedUpdate(&weather_cnt);
            }
            else
            {
                hb_fail_cnt++;
                printf("WARN: 心跳检测失败 (第%d次)\r\n", hb_fail_cnt);

                if (hb_fail_cnt >= HB_FAIL_MAX)
                {
                    printf("NET: 连续心跳失败，尝试重连...\r\n");
                    hb_fail_cnt = 0;
                    OneNet_SetConnected(0);

                    if (Net_Reconnect(saved_ssid, saved_pass) != 0)
                    {
                        printf("NET: 重连失败，60s后重试\r\n");
                        SharedTimeData.wifi_ok = 0;
                        SharedWiFiData.wifi_connected = 0;
                        OneNet_SetConnected(0);
                        if (!offline_announced)
                        {
                            TTS_Speak("云平台连接异常");
                            offline_announced = 1;
                        }
                        hb_threshold = HB_RECONNECT_WAIT;
                    }
                    else
                    {
                        printf("NET: 重连成功\r\n");
                        SharedTimeData.wifi_ok = 1;
                        SharedWiFiData.wifi_connected = 1;
                        OneNet_SetConnected(1);
                        offline_announced = 0;
                        hb_threshold = HB_INTERVAL_NORMAL;
                        FetchAndAnnounceWeather();
                        Weather_ToShared();
                        Time_ToShared();
                        TTS_Speak("云平台已恢复连接");
                    }
                }
                else
                {
                    hb_threshold = HB_INTERVAL_FAST;
                }
            }
        }

        /* V3F 日志请求响应：读取 W25Q64 传感器日志并写入共享内存 */
        {
            static uint8_t last_log_req_seq = 0;
            uint8_t cur_log_req_seq = SharedLogData.log_req_seq;
            if (cur_log_req_seq != last_log_req_seq)
            {
                last_log_req_seq = cur_log_req_seq;
                uint32_t total = Storage_GetLogCount();
                SharedLogData.log_total = (total > 65535) ? 65535 : (uint16_t)total;

                uint8_t page = SharedLogData.log_page;
                uint8_t count = SharedLogData.log_req_count;
                if (count > SHARED_LOG_PAGE_SIZE) count = SHARED_LOG_PAGE_SIZE;

                /* 页码 0 = 最新数据 */
                uint32_t total_actual = (total > 65535) ? 65535 : total;
                uint32_t start_idx = 0, fetch_count = 0;
                if (total_actual == 0)
                {
                    fetch_count = 0;
                }
                else if (total_actual <= count)
                {
                    start_idx = 0;
                    fetch_count = total_actual;
                }
                else
                {
                    uint32_t needed = (uint32_t)(page + 1) * count;
                    if (needed > total_actual)
                        needed = total_actual;
                    start_idx = total_actual - needed;
                    fetch_count = (needed > count) ? count : needed;
                }

                if (fetch_count > 0)
                    Storage_ReadLogs(start_idx, fetch_count, (SensorLog_t *)SharedLogData.log_data);
                memset((void *)&SharedLogData.log_data[fetch_count * 16], 0,
                       (SHARED_LOG_PAGE_SIZE - fetch_count) * 16);

                SharedLogData.log_resp_seq++;
                printf("NET: 日志响应 page=%u start=%lu count=%lu total=%u\r\n",
                       page, start_idx, fetch_count, SharedLogData.log_total);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));

        /* 超时保护：30秒内云推送未到则标记已同步，避免误还原用户修改 */
        if(!threshold_synced &&
           (xTaskGetTickCount() - sync_start_tick) >= pdMS_TO_TICKS(30000))
        {
            threshold_synced = 1;
            printf("NET: 云推送超时，标记同步完成\r\n");
        }
    }
}

/* ======================== 公开接口 ======================== */
void NetTask_Create(void)
{
    xTaskCreate((TaskFunction_t)net_task,
                (const char *)"net",
                (uint16_t)NET_TASK_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)NET_TASK_PRIO,
                (TaskHandle_t *)&NetTask_Handler);
}
