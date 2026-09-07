#ifndef __BH1750_H__
#define __BH1750_H__

//  功能: 初始化I2C1，PB6=SCL, PB7=SDA
void I2C1_Init(void);


// 功能: 向BH1750发送一个字节命令
void BH1750_WriteCmd(uint8_t cmd);


// 功能: 从BH1750读取16位光照强度数据
uint16_t BH1750_ReadData(void);


// 功能: BH1750初始化：上电 → 设置为连续高分辨率模式
void BH1750_Init(void);


// 功能: 读取BH1750光照强度值（单位：lx）
// 说明: 连续高分辨率模式下，测量值/1.2即为光照强度[reference:6]
float BH1750_GetLightLux(void);

#endif
