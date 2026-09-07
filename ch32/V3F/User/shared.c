/********************************** (C) COPYRIGHT  *******************************
* File Name          : shared.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : This file provides all the firmware functions.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

#include "stdio.h"
#include "shared.h"
/* Global define */

/* Global Variable */


__attribute__((section(".shared_data"))) 
volatile uint8_t Buffer_Sharing[4] ;
__attribute__((section(".shared_data"))) 
volatile uint32_t Data_Sharing ;
__attribute__((section(".shared_data"))) 
volatile shared_sensor_data_t SharedSensorData ;
__attribute__((section(".shared_data"))) 
volatile shared_time_data_t SharedTimeData ;
__attribute__((section(".shared_data"))) 
volatile shared_weather_data_t SharedWeatherData ;
__attribute__((section(".shared_data"))) 
volatile shared_device_data_t SharedDeviceData ;
__attribute__((section(".shared_data"))) 
volatile shared_threshold_data_t SharedThresholdData ;
__attribute__((section(".shared_data"))) 
volatile shared_pest_data_t SharedPestData ;
__attribute__((section(".shared_data"))) 
volatile shared_tts_data_t SharedTtsData ;
__attribute__((section(".shared_data")))
volatile shared_car_data_t SharedCarData ;
__attribute__((section(".shared_data")))
volatile shared_image_data_t SharedImageData ;
__attribute__((section(".shared_data")))
volatile shared_wifi_data_t SharedWiFiData ;
__attribute__((section(".shared_data")))
volatile shared_log_data_t SharedLogData ;
__attribute__((section(".shared_data")))
volatile shared_weather_cmd_t SharedWeatherCmd ;
__attribute__((section(".shared_data")))
volatile shared_cal_data_t SharedCalData ;