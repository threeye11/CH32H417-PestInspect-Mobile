/**
 * @file    sht30.h
 * @brief   SHT30 温湿度传感器驱动头文件
 * @note    通过 I2C 接口读取温度和湿度数据
 */

#ifndef __SHT30_H__
#define __SHT30_H__

/**
 * @brief  SHT30 传感器初始化（I2C 配置）
 */
void SHT30_Init(void);

/**
 * @brief  向 SHT30 发送命令
 * @param  cmd 16位命令字
 */
void SHT30_SendCmd(uint16_t cmd);

/**
 * @brief  从 SHT30 读取原始数据
 * @param  buf 数据缓冲区（6字节）
 * @return 读取状态
 */
uint8_t SHT30_ReadData(uint8_t *buf);

/**
 * @brief  获取温湿度值
 * @param  temp 温度输出指针（℃）
 * @param  hum  湿度输出指针（%RH）
 * @return 读取状态
 */
uint8_t SHT30_GetTempHum(float *temp, float *hum);

#endif
