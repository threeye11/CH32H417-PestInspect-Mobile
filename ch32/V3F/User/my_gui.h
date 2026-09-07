#ifndef     __MY_GUI
#define     __MY_GUI

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

void my_gui(void);

void show_boot_animation(void);
void hide_boot_animation(void);

void update_sensor_data(float temp, float humi, uint16_t light);
void update_spray_status(bool active);
void update_alarm_status(bool sound_active, bool light_active);
void update_detect_result(const char *plant, const char *disease,
                          uint8_t confidence);
void update_preview_image(const uint8_t *data, uint16_t w, uint16_t h,
                          lv_img_cf_t cf, uint32_t data_size);
void set_preview_zoom(uint16_t zoom);
void update_weather_info(const char *city, const char *weather, int8_t temp);
void update_time_info(const char *datetime);
void update_home_info(const char *datetime, const char *city,
                      const char *weather, int8_t temp);
void update_threshold_from_shared(uint16_t temp_th, uint16_t humi_th, uint16_t light_th);
void update_car_status(uint8_t mode, uint8_t speed);
void update_module_status(uint8_t wifi, uint8_t bh1750, uint8_t sht30);
void update_wifi_status(uint8_t connected);
void update_mute_switch(uint8_t mute);
void update_tts_settings(uint8_t vol, uint8_t tone, uint8_t spd);
void update_weather_location_status(void);
void update_save_log_status(void);
void update_log_interval(void);
void sync_detect_button(uint8_t detect_cmd);
void update_history_data(void);

#endif // __MY_GUI
