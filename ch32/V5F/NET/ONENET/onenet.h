/**
 * @file    onenet.h
 * @brief   OneNET 云平台接口
 */
#ifndef _ONENET_H_
#define _ONENET_H_

#include <stdint.h>

/* ==================== 云平台配置（使用前请自行填写，参见 README.md） ==================== */
#define PROID		"your_product_id"	/* OneNET 产品ID */
#define AUTH_INFO	"your_auth_token"	/* OneNET 设备鉴权token（安全鉴权信息） */
#define DEVICE_NAME	"your_device_name"	/* OneNET 设备名称 */

_Bool OneNet_DevLink(void);
_Bool OneNet_SendData(float temp, float humi, float light);
void OneNET_Publish(const char *topic, const char *msg);
void OneNET_Subscribe(void);
_Bool OneNet_HeartBeat(void);
_Bool Net_Reconnect(const char *ssid, const char *password);
void OneNet_RevPro(unsigned char *cmd);

/* 设备状态控制（云平台下发 → 设置状态 + 控制硬件） */
void Device_GPIO_Init(void);
void Device_SetLed(_Bool on);
void Device_SetBuzzer(_Bool on);
void Device_SetPump(_Bool on);

/* 阈值设置（云平台下发 → 写入共享内存供 V3F 读取） */
void Threshold_SetTemp(uint16_t val);
void Threshold_SetHumi(uint16_t val);
void Threshold_SetLight(uint16_t val);
uint16_t Threshold_GetTemp(void);
uint16_t Threshold_GetHumi(void);
uint16_t Threshold_GetLight(void);

/* 病害检测（云平台上报） */
void Pest_SetResult(const char *plant, const char *pest, uint8_t confidence);

/* 速度档位（云平台下发/上报） */
void Speed_SetGear(uint8_t gear);
uint8_t Speed_GetGear(void);

/* 连接状态管理（net_task 调用） */
void OneNet_SetConnected(_Bool val);
_Bool OneNet_IsConnected(void);

#endif
