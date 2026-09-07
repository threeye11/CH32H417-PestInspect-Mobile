/**
 * @file    sht30.c
 * @brief   SHT30 温湿度传感器驱动
 * @note    通过 I2C2（PC0=SCL, PC1=SDA）与传感器通信，支持 CRC8 校验
 */

#include "ch32h417.h"

/** SHT30 I2C 写地址（0x44 << 1） */
#define SHT30_ADDR_WRITE   0x88
/** SHT30 I2C 读地址（0x44 << 1 | 1） */
#define SHT30_ADDR_READ    0x89

/** 常用测量命令（时钟延展禁止，高/中/低重复性） */
#define SHT30_MEAS_HIGH    0x2400   /**< 高重复性测量 */
#define SHT30_MEAS_MED     0x240B   /**< 中重复性测量 */
#define SHT30_MEAS_LOW     0x2416   /**< 低重复性测量 */

/**
 * @brief  SHT30 初始化
 * @note   配置 PC0/PC1 为 I2C2 复用开漏输出，100kHz 速率
 */
void SHT30_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    I2C_InitTypeDef   I2C_InitStructure;

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOB, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_I2C2, ENABLE);

    /* 配置PC0(SCL)为复用开漏输出 */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource0, GPIO_AF9);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* 配置PC1(SDA)为复用开漏输出 */
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource1, GPIO_AF9);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* 配置 I2C2：100kHz、标准模式、7位地址 */
    I2C_InitStructure.I2C_ClockSpeed = 100000;
    I2C_InitStructure.I2C_Mode       = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle  = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_Ack        = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C2, &I2C_InitStructure);

    I2C_Cmd(I2C2, ENABLE);
}

/**
 * @brief  I2C2 错误恢复：软件复位后重新配置
 * @note   当 I2C 通信超时时调用，恢复总线到正常状态
 */
static void I2C2_Reinit(void)
{
    I2C_InitTypeDef I2C_InitStructure;

    I2C_SoftwareResetCmd(I2C2, ENABLE);
    I2C_SoftwareResetCmd(I2C2, DISABLE);

    I2C_InitStructure.I2C_ClockSpeed = 100000;
    I2C_InitStructure.I2C_Mode       = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle  = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_Ack        = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C2, &I2C_InitStructure);

    I2C_Cmd(I2C2, ENABLE);
}

/**
 * @brief  SHT30 CRC8 校验（多项式 0x31，初始值 0xFF）
 * @param  data 数据缓冲区
 * @param  len  数据长度
 * @return CRC8 校验值
 */
static uint8_t SHT30_CRC8(uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/**
 * @brief  向 SHT30 发送 16 位命令
 * @param  cmd 命令字（高字节先发）
 * @note   超时时自动复位 I2C 总线
 */
void SHT30_SendCmd(uint16_t cmd)
{
    uint32_t timeout;
    uint8_t hi = cmd >> 8;
    uint8_t lo = cmd & 0xFF;

    I2C_GenerateSTART(I2C2, ENABLE);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT)) {
        if (--timeout == 0) goto i2c_err;
    }

    I2C_Send7bitAddress(I2C2, SHT30_ADDR_WRITE, I2C_Direction_Transmitter);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if (--timeout == 0) goto i2c_err;
    }

    I2C_SendData(I2C2, hi);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if (--timeout == 0) goto i2c_err;
    }

    I2C_SendData(I2C2, lo);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if (--timeout == 0) goto i2c_err;
    }

    I2C_GenerateSTOP(I2C2, ENABLE);
    return;

i2c_err:
    I2C2_Reinit();
}

/**
 * @brief  从 SHT30 读取 6 字节原始数据
 * @param  buf 输出缓冲区（至少 6 字节）
 * @return 0=成功, 1=I2C 错误
 * @note   前 5 字节回复 ACK，第 6 字节回复 NACK + STOP
 */
uint8_t SHT30_ReadData(uint8_t *buf)
{
    uint32_t timeout;

    I2C_GenerateSTART(I2C2, ENABLE);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT)) {
        if (--timeout == 0) goto i2c_err;
    }

    I2C_Send7bitAddress(I2C2, SHT30_ADDR_READ, I2C_Direction_Receiver);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) {
        if (--timeout == 0) goto i2c_err;
    }

    /* 前 5 个字节回复 ACK，第 6 个字节回复 NACK + STOP */
    for (int i = 0; i < 6; i++)
    {
        if (i == 5)          /* 最后一个字节 */
        {
            I2C_AcknowledgeConfig(I2C2, DISABLE);
            I2C_GenerateSTOP(I2C2, ENABLE);
        }
        timeout = 0xFFFF;
        while (!I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_BYTE_RECEIVED)) {
            if (--timeout == 0) goto i2c_err;
        }
        buf[i] = I2C_ReceiveData(I2C2);
    }

    I2C_AcknowledgeConfig(I2C2, ENABLE);  /* 恢复 ACK */
    return 0;

i2c_err:
    I2C_AcknowledgeConfig(I2C2, ENABLE);
    I2C2_Reinit();
    return 1;
}

/**
 * @brief  获取温湿度值（高重复性模式，含 CRC8 校验）
 * @param  temp 温度输出（℃）
 * @param  hum  湿度输出（%RH）
 * @return 0=成功, 1=CRC校验失败, 2=I2C读取失败
 */
uint8_t SHT30_GetTempHum(float *temp, float *hum)
{
    uint8_t buf[6];

    /* 1. 发送高重复性测量命令（时钟延展禁止） */
    SHT30_SendCmd(SHT30_MEAS_HIGH);

    /* 2. 等待测量完成（高重复性最长 15ms，等待 20ms 留有余量） */
    Delay_Ms(20);

    /* 3. 读取 6 字节数据 */
    if (SHT30_ReadData(buf) != 0)
    {
        return 2;   /* I2C 读取失败 */
    }

    /* 4. CRC 校验：温度 2 字节 + CRC，湿度 2 字节 + CRC */
    if (SHT30_CRC8(buf, 2) != buf[2] ||
        SHT30_CRC8(buf + 3, 2) != buf[5])
    {
        return 1;   /* 校验失败 */
    }

    /* 5. 计算温湿度（SHT30 公式） */
    uint16_t st  = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t srh = ((uint16_t)buf[3] << 8) | buf[4];

    *temp = -45.0f + 175.0f * (float)st  / 65535.0f;
    *hum  = 100.0f  * (float)srh / 65535.0f;

    return 0;
}
