/**
 * @file    lvgl_task.c
 * @brief   LVGL 显示任务
 *
 *          启动流程：
 *          1. 等待传感器就绪
 *          2. 等待 V5F 网络就绪（带 10 秒超时，超时进入离线模式）
 *          3. 创建主界面 + 同步 RTC（离线模式使用本地时间）
 *          4. 隐藏开机动画
 *          5. 进入主循环：更新时间 / 天气 / 传感器数据
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "shared.h"
#include "rtc.h"
#include "watchdog.h"
#include "touch.h"

#include "lvgl.h"
#include "my_gui.h"

/* ======================== 任务配置 ======================== */
#define LVGL_TASK_PRIO      3
#define LVGL_TASK_STK_SIZE  4096

/* ======================== 超时配置 ======================== */
#define NETWORK_TIMEOUT_MS   30000   /* 网络数据等待超时 30 秒 */

/* ======================== 任务句柄 ======================== */
TaskHandle_t LvglTask_Handler;

/* ======================== 外部引用 ======================== */
extern QueueHandle_t SensorData_Queue;
extern volatile uint8_t sensor_ready;
extern volatile uint8_t lvgl_ready;

/* ======================== 传感器数据包 ======================== */
typedef struct {
    int16_t temperature_x10;
    int16_t humidity_x10;
    uint16_t light;
} sensor_data_t;

/* ======================== 任务主函数 ======================== */
static void lvgl_task(void *pvParameters)
{
    printf("V3F LVGL: 任务启动\r\n");

    /* 等待传感器就绪 */
    while (!sensor_ready)
    {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    printf("V3F LVGL: 传感器就绪\r\n");

    /* 等待 V5F 网络就绪（带超时） */
    printf("V3F LVGL: 等待V5F网络就绪（超时 %d 秒）...\r\n", NETWORK_TIMEOUT_MS / 1000);
    uint32_t wait_start = xTaskGetTickCount();
    uint8_t network_ready = 0;
    while (!network_ready)
    {
        if (SharedTimeData.time_valid && SharedWeatherData.weather_valid)
        {
            network_ready = 1;
            printf("V3F LVGL: 网络就绪\r\n");
        }
        else if ((xTaskGetTickCount() - wait_start) * portTICK_PERIOD_MS >= NETWORK_TIMEOUT_MS)
        {
            printf("V3F LVGL: 网络等待超时，进入离线模式\r\n");
            /* 通知 V5F 播报离线模式提示 */
            SharedTtsData.tts_seq++;
            SharedTtsData.tts_pending = 1;
            strncpy((char *)SharedTtsData.text, "网络连接失败，进入离线模式", sizeof(SharedTtsData.text) - 1);
            SharedTtsData.text[sizeof(SharedTtsData.text) - 1] = '\0';
            break;
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* 同步网络时间到 RTC（仅在网络就绪时） */
    if (network_ready)
    {
        RTC_Set(SharedTimeData.year, SharedTimeData.month, SharedTimeData.day,
                SharedTimeData.hour, SharedTimeData.minute, SharedTimeData.second);
        RTC_Get();
        printf("V3F RTC: 网络对时 %04d-%02d-%02d %02d:%02d:%02d\r\n",
               SharedTimeData.year, SharedTimeData.month, SharedTimeData.day,
               SharedTimeData.hour, SharedTimeData.minute, SharedTimeData.second);
    }
    else
    {
        RTC_Get();
        printf("V3F RTC: 使用本地时间（网络未同步）\r\n");
    }

    /* 创建主界面 */
    my_gui();
    printf("V3F LVGL: 界面创建完成\r\n");

    /* 初始化共享内存阈值：V5F 从 Flash 加载，V3F 直接读取 */

    /* 清空队列中的旧数据 */
    sensor_data_t dummy;
    while (xQueueReceive(SensorData_Queue, &dummy, 0) == pdTRUE) {}

    /* 更新时间信息 */
    char init_time[48];
    RTC_FormatString(init_time, sizeof(init_time));
    update_time_info(init_time);

    /* 更新天气信息（网络就绪时显示真实数据，否则显示占位符） */
    if (network_ready && SharedWeatherData.weather_valid)
    {
        update_weather_info((const char *)SharedWeatherData.city,
                            (const char *)SharedWeatherData.weather,
                            SharedWeatherData.temp_outdoor);
    }
    else
    {
        update_weather_info("--", "--", 0);
    }

    /* 读取首次传感器数据 */
    {
        sensor_data_t init_pkt;
        if (xQueueReceive(SensorData_Queue, &init_pkt, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            update_sensor_data(init_pkt.temperature_x10 / 10.0f, init_pkt.humidity_x10 / 10.0f, init_pkt.light);
        }
    }

    /* 标记整个屏幕需要重绘 */
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);

    /* 隐藏开机动画 */
    hide_boot_animation();
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(50));

    /* 标记 LVGL 界面就绪，允许 device_task 开始告警检查 */
    lvgl_ready = 1;
    printf("V3F LVGL: 界面就绪，设备控制任务可开始告警\r\n");

    uint8_t last_time_seq = SharedTimeData.time_seq;
    uint8_t last_weather_seq = SharedWeatherData.weather_seq;
    uint8_t last_threshold_seq = SharedThresholdData.threshold_seq;

    /* 启动时无条件从 SharedThresholdData 读取阈值并更新 LVGL */
    update_threshold_from_shared(
        SharedThresholdData.temp_threshold,
        SharedThresholdData.humi_threshold,
        SharedThresholdData.light_threshold);
    update_mute_switch(SharedThresholdData.mute);
    uint8_t last_mute = SharedThresholdData.mute;
    update_tts_settings(
        SharedThresholdData.tts_volume,
        SharedThresholdData.tts_tone,
        SharedThresholdData.tts_speed);

    uint8_t last_pest_seq = SharedPestData.pest_seq;
    uint8_t last_pest_detect_cmd = SharedPestData.pest_detect_cmd;
    uint8_t last_device_seq = SharedDeviceData.device_seq;
    uint8_t last_car_seq = SharedCarData.car_seq;
    uint8_t last_img_seq = SharedImageData.img_seq;
    uint8_t was_offline = !network_ready;  /* 记录是否处于离线模式 */
    uint8_t last_bh1750_ok = 1;
    uint8_t last_sht30_ok = 1;
    sensor_data_t data;

    /* 看门狗在任务初始化完成后启动 */
    IWDG_FeedInit(IWDG_Prescaler_32, 8000);

    while (1)
    {
        IWDG_Feed();
        /* 检查设备控制状态变化（警报灯/警报声/喷药） */
        uint8_t current_device_seq = SharedDeviceData.device_seq;
        if (current_device_seq != last_device_seq)
        {
            last_device_seq = current_device_seq;
            update_alarm_status(SharedDeviceData.buzzer, SharedDeviceData.led);
            update_spray_status(SharedDeviceData.pump);
        }

        /* 检查 V5F 网络对时数据 */
        uint8_t current_time_seq = SharedTimeData.time_seq;
        if (current_time_seq != last_time_seq && SharedTimeData.time_valid)
        {
            last_time_seq = current_time_seq;
            RTC_Set(SharedTimeData.year, SharedTimeData.month, SharedTimeData.day,
                    SharedTimeData.hour, SharedTimeData.minute, SharedTimeData.second);
            RTC_Get();
            printf("V3F RTC: 网络对时 %04d-%02d-%02d %02d:%02d:%02d\r\n",
                   SharedTimeData.year, SharedTimeData.month, SharedTimeData.day,
                   SharedTimeData.hour, SharedTimeData.minute, SharedTimeData.second);

            /* 网络恢复标记（TTS 播报由 V5F net_task 负责） */
            if (was_offline)
            {
                was_offline = 0;
                printf("V3F: 网络恢复\r\n");
            }
        }

        /* 检查 V5F 天气数据 */
        uint8_t current_weather_seq = SharedWeatherData.weather_seq;
        if (current_weather_seq != last_weather_seq && SharedWeatherData.weather_valid)
        {
            last_weather_seq = current_weather_seq;
            update_weather_info((const char *)SharedWeatherData.city,
                                (const char *)SharedWeatherData.weather,
                                SharedWeatherData.temp_outdoor);
        }

        /* 检查 V5F 阈值设置 */
        uint8_t current_threshold_seq = SharedThresholdData.threshold_seq;
        if (current_threshold_seq != last_threshold_seq)
        {
            last_threshold_seq = current_threshold_seq;
            update_threshold_from_shared(
                SharedThresholdData.temp_threshold,
                SharedThresholdData.humi_threshold,
                SharedThresholdData.light_threshold);
            update_tts_settings(
                SharedThresholdData.tts_volume,
                SharedThresholdData.tts_tone,
                SharedThresholdData.tts_speed);
            update_log_interval();
        }

        /* 同步静音状态（图标 + 开关） */
        if (SharedThresholdData.mute != last_mute)
        {
            last_mute = SharedThresholdData.mute;
            update_mute_switch(last_mute);
        }

        /* 检查天气地点设置状态 */
        update_weather_location_status();

        /* 检查手动保存日志状态 */
        update_save_log_status();

        /* 检查清空历史数据完成信号 */
        if (SharedLogData.save_sensor_log == 4)
        {
            SharedLogData.save_sensor_log = 0;
            printf("V3F: 历史数据已清空\r\n");
        }

        /* 同步网页端检测开关状态 */
        uint8_t cur_pest_detect_cmd = SharedPestData.pest_detect_cmd;
        if (cur_pest_detect_cmd != last_pest_detect_cmd)
        {
            last_pest_detect_cmd = cur_pest_detect_cmd;
            sync_detect_button(cur_pest_detect_cmd);
        }

        /* 检查 V5F 病害检测数据 */
        uint8_t current_pest_seq = SharedPestData.pest_seq;
        if (current_pest_seq != last_pest_seq)
        {
            last_pest_seq = current_pest_seq;
            const char *disease = (SharedPestData.pest[0] != '\0') ? (const char *)SharedPestData.pest : NULL;
            update_detect_result(
                (const char *)SharedPestData.plant,
                disease,
                SharedPestData.confidence);
        }

        /* 检查 V5F 小车状态数据 */
        uint8_t current_car_seq = SharedCarData.car_seq;
        if (current_car_seq != last_car_seq)
        {
            last_car_seq = current_car_seq;
            update_car_status(SharedCarData.car_mode, SharedCarData.car_speed);
        }

        RTC_Get();
        char time_str[48];
        RTC_FormatString(time_str, sizeof(time_str));
        update_time_info(time_str);

        /* 将 RTC 时间回写 SharedTimeData，供 V5F 日志保存使用 */
        SharedTimeData.year   = calendar.w_year;
        SharedTimeData.month  = calendar.w_month;
        SharedTimeData.day    = calendar.w_date;
        SharedTimeData.hour   = calendar.hour;
        SharedTimeData.minute = calendar.min;
        SharedTimeData.second = calendar.sec;

        while (xQueueReceive(SensorData_Queue, &data, 0) == pdTRUE)
        {
            update_sensor_data(data.temperature_x10 / 10.0f, data.humidity_x10 / 10.0f, data.light);
        }

        /* 更新模块状态（WiFi/BH1750/SHT30） */
        uint8_t cur_bh = SharedSensorData.bh1750_ok;
        uint8_t cur_sh = SharedSensorData.sht30_ok;
        update_module_status(SharedTimeData.wifi_ok, cur_bh, cur_sh);
        update_wifi_status(SharedWiFiData.wifi_connected);

        /* BH1750 异常/恢复 TTS 播报 */
        if (cur_bh != last_bh1750_ok)
        {
            last_bh1750_ok = cur_bh;
            SharedTtsData.tts_seq++;
            SharedTtsData.tts_pending = 1;
            if (cur_bh)
                strncpy((char *)SharedTtsData.text, "光照传感器已恢复", sizeof(SharedTtsData.text) - 1);
            else
                strncpy((char *)SharedTtsData.text, "光照传感器异常", sizeof(SharedTtsData.text) - 1);
            SharedTtsData.text[sizeof(SharedTtsData.text) - 1] = '\0';
        }

        /* SHT30 异常/恢复 TTS 播报 */
        if (cur_sh != last_sht30_ok)
        {
            last_sht30_ok = cur_sh;
            SharedTtsData.tts_seq++;
            SharedTtsData.tts_pending = 1;
            if (cur_sh)
                strncpy((char *)SharedTtsData.text, "温湿度传感器已恢复", sizeof(SharedTtsData.text) - 1);
            else
                strncpy((char *)SharedTtsData.text, "温湿度传感器异常", sizeof(SharedTtsData.text) - 1);
            SharedTtsData.text[sizeof(SharedTtsData.text) - 1] = '\0';
        }

        /* 检查 V5F K230 图像帧 */
        uint8_t cur_img_seq = SharedImageData.img_seq;
        if (cur_img_seq != last_img_seq && SharedImageData.img_valid)
        {
            last_img_seq = cur_img_seq;
            update_preview_image(SharedImageData.img_data,
                                 80, 60,
                                 LV_IMG_CF_TRUE_COLOR,
                                 IMAGE_BUF_SIZE);
            set_preview_zoom(768);  /* 3倍放大 80x60→240x180 */
        }

        /* 历史数据轮询：首次请求 + 响应更新 */
        {
            static uint8_t log_req_sent = 0;
            if (!log_req_sent)
            {
                /* 首次请求最新一页日志 */
                SharedLogData.log_page = 0;
                SharedLogData.log_req_count = SHARED_LOG_PAGE_SIZE;
                SharedLogData.log_req_seq++;
                log_req_sent = 1;
                printf("V3F: 请求历史数据 page=0\r\n");
            }
            update_history_data();
        }

        /* PC7 校准按键检测（低电平有效，50ms 消抖） */
        {
            static uint8_t cal_btn_cnt = 0;
            if (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_7) == 0) {
                cal_btn_cnt++;
                if (cal_btn_cnt >= 10 && !tp_calibrating) {
                    cal_btn_cnt = 0;
                    printf("V3F: PC7 校准按键触发\r\n");
                    /* 通知 V5F 播报提示 */
                    SharedTtsData.tts_seq++;
                    SharedTtsData.tts_pending = 1;
                    strncpy((char *)SharedTtsData.text,
                            "进入屏幕校准模式",
                            sizeof(SharedTtsData.text) - 1);
                    SharedTtsData.text[sizeof(SharedTtsData.text) - 1] = '\0';
                    /* 等待 TTS 播报完成后进入校准 */
                    vTaskDelay(pdMS_TO_TICKS(1500));
                    TP_Adjust();
                    /* 校准完成后重新加载 */
                    if (SharedCalData.cal_status != 0) {
                        tp_dev.xfac = SharedCalData.xfac;
                        tp_dev.yfac = SharedCalData.yfac;
                        tp_dev.xc   = SharedCalData.xc;
                        tp_dev.yc   = SharedCalData.yc;
                        printf("V3F: 校准完成 (xfac=%.4f yfac=%.4f)\r\n",
                               tp_dev.xfac, tp_dev.yfac);
                        /* 通知 V5F 播报校准完成 */
                        SharedTtsData.tts_seq++;
                        SharedTtsData.tts_pending = 1;
                        strncpy((char *)SharedTtsData.text,
                                "屏幕校准完成",
                                sizeof(SharedTtsData.text) - 1);
                        SharedTtsData.text[sizeof(SharedTtsData.text) - 1] = '\0';
                    } else {
                        /* 校准失败，优先沿用 SharedCalData 中的旧值 */
                        if (SharedCalData.cal_status != 0) {
                            tp_dev.xfac = SharedCalData.xfac;
                            tp_dev.yfac = SharedCalData.yfac;
                            tp_dev.xc   = SharedCalData.xc;
                            tp_dev.yc   = SharedCalData.yc;
                            printf("V3F: 校准失败，沿用旧值 (xfac=%.4f)\r\n",
                                   tp_dev.xfac);
                        }
                        SharedTtsData.tts_seq++;
                        SharedTtsData.tts_pending = 1;
                        strncpy((char *)SharedTtsData.text,
                                "校准失败，沿用原有设置",
                                sizeof(SharedTtsData.text) - 1);
                        SharedTtsData.text[sizeof(SharedTtsData.text) - 1] = '\0';
                    }
                    /* 强制刷新整个屏幕（TP_Adjust 清空了 LCD） */
                    lv_obj_invalidate(lv_scr_act());
                    lv_refr_now(NULL);
                }
            } else {
                cal_btn_cnt = 0;
            }
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ======================== 公开接口 ======================== */
void LvglTask_Create(void)
{
    xTaskCreate((TaskFunction_t)lvgl_task,
                (const char *)"lvgl",
                (uint16_t)LVGL_TASK_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)LVGL_TASK_PRIO,
                (TaskHandle_t *)&LvglTask_Handler);
}
