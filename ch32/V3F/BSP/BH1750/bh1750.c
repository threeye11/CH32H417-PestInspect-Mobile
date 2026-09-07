#include "ch32h417.h"

/*==============================================================================
 * BH1750 I2C地址定义（ADDR引脚接地时使用0x46/0x47）
 * 若ADDR接VCC，请改为 0xB8 / 0xB9
 *============================================================================*/
#define BH1750_ADDR_WRITE   0x46    // 7位地址0x23 << 1 | 0（写）
#define BH1750_ADDR_READ    0x47    // 7位地址0x23 << 1 | 1（读）

/* BH1750指令 */
#define CMD_POWER_ON        0x01
#define CMD_POWER_DOWN      0x00
#define CMD_CONT_HRES       0x10    // 连续高分辨率模式
#define CMD_ONE_TIME_HRES   0x20    // 单次高分辨率模式

/*==============================================================================
 * 函数: I2C1_Init
 * 功能: 初始化I2C1，PB6=SCL, PB7=SDA
 *============================================================================*/
void I2C1_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    I2C_InitTypeDef   I2C_InitStructure;

    /* 1. 使能时钟 */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOB | RCC_HB2Periph_AFIO, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_I2C1, ENABLE);

    /* 2. 配置PB6(SCL)为复用开漏输出 */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource6, GPIO_AF4);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_OD;          // 复用开漏输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* 2. 配置PB7(SDA)为复用开漏输出 */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource7, GPIO_AF4);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_OD;          // 复用开漏输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* 3. 配置I2C1：标准模式100kHz */
    I2C_InitStructure.I2C_ClockSpeed = 100000;
    I2C_InitStructure.I2C_Mode       = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle  = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_Ack        = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &I2C_InitStructure);

    /* 4. 使能I2C1 */
    I2C_Cmd(I2C1, ENABLE);
}

/* I2C1 错误恢复：软件复位 + 完整重配 */
static void I2C1_Reinit(void)
{
    I2C_InitTypeDef I2C_InitStructure;

    I2C_SoftwareResetCmd(I2C1, ENABLE);
    I2C_SoftwareResetCmd(I2C1, DISABLE);

    I2C_InitStructure.I2C_ClockSpeed = 100000;
    I2C_InitStructure.I2C_Mode       = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle  = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_Ack        = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &I2C_InitStructure);

    I2C_Cmd(I2C1, ENABLE);
}

/*==============================================================================
 * 函数: BH1750_WriteCmd
 * 功能: 向BH1750发送一个字节命令
 * 输入: cmd - 命令字节
 *============================================================================*/
void BH1750_WriteCmd(uint8_t cmd)
{
    uint32_t timeout;

    /* 产生起始条件 */
    I2C_GenerateSTART(I2C1, ENABLE);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT)) {
        if (--timeout == 0) goto i2c_err;
    }

    /* 发送7位地址 + 写方向 */
    I2C_Send7bitAddress(I2C1, BH1750_ADDR_WRITE, I2C_Direction_Transmitter);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if (--timeout == 0) goto i2c_err;
    }

    /* 发送命令字节 */
    I2C_SendData(I2C1, cmd);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if (--timeout == 0) goto i2c_err;
    }

    /* 产生停止条件 */
    I2C_GenerateSTOP(I2C1, ENABLE);
    return;

i2c_err:
    I2C1_Reinit();
}

/*==============================================================================
 * 函数: BH1750_ReadData
 * 功能: 从BH1750读取16位光照强度数据
 * 返回: uint16_t - 原始光照数据
 * 说明: BH1750返回16位数据，先高8位后低8位[reference:4]
 *============================================================================*/
uint16_t BH1750_ReadData(void)
{
    uint8_t  data_high, data_low;
    uint16_t result;
    uint32_t timeout;

    /* 产生起始条件 */
    I2C_GenerateSTART(I2C1, ENABLE);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT)) {
        if (--timeout == 0) goto i2c_err;
    }

    /* 发送7位地址 + 读方向 */
    I2C_Send7bitAddress(I2C1, BH1750_ADDR_READ, I2C_Direction_Receiver);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) {
        if (--timeout == 0) goto i2c_err;
    }

    /* 读高8位: 使能ACK表示继续接收 */
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED)) {
        if (--timeout == 0) goto i2c_err;
    }
    data_high = I2C_ReceiveData(I2C1);

    /* 读低8位: 发送NACK表示最后一个字节[reference:5] */
    I2C_AcknowledgeConfig(I2C1, DISABLE);
    I2C_GenerateSTOP(I2C1, ENABLE);
    timeout = 0xFFFF;
    while (!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED)) {
        if (--timeout == 0) goto i2c_err;
    }
    data_low = I2C_ReceiveData(I2C1);

    /* 重新使能ACK以便下次通信 */
    I2C_AcknowledgeConfig(I2C1, ENABLE);

    /* 合成16位数据（高字节在前） */
    result = ((uint16_t)data_high << 8) | data_low;
    return result;

i2c_err:
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    I2C1_Reinit();
    return 0xFFFF;   /* 无效值，表示读取失败 */
}

/*==============================================================================
 * 函数: BH1750_Init
 * 功能: BH1750初始化：上电 → 设置为连续高分辨率模式
 *============================================================================*/
void BH1750_Init(void)
{
    I2C1_Init();
    BH1750_WriteCmd(CMD_POWER_ON);      // 发送上电命令
    BH1750_WriteCmd(CMD_CONT_HRES);     // 设置为连续高分辨率模式
}

/*==============================================================================
 * 函数: BH1750_GetLightLux
 * 功能: 读取BH1750光照强度值（单位：lx）
 * 返回: float - 光照强度
 * 说明: 连续高分辨率模式下，测量值/1.2即为光照强度[reference:6]
 *============================================================================*/
float BH1750_GetLightLux(void)
{
    uint16_t raw_data;
    float    lux;

    raw_data = BH1750_ReadData();       // 读取原始16位数据
    lux      = (float)raw_data / 1.2f;  // 转换为勒克斯(lx)
    return lux;
}

