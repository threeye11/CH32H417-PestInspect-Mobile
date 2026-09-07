/**
 * @file    w25qxx.c
 * @brief   W25Q64 SPI Flash 驱动 — SPI3, CS=PA15
 */

#include "w25qxx.h"
#include "debug.h"
#include <string.h>

/* ---- 引脚定义 ---- */
#define W25QXX_CS_PORT          GPIOA
#define W25QXX_CS_PIN           GPIO_Pin_15

#define W25QXX_SPI              SPI3
#define W25QXX_SPI_CLK          RCC_HB1Periph_SPI3
#define W25QXX_SCK_PORT         GPIOC
#define W25QXX_SCK_PIN          GPIO_Pin_10
#define W25QXX_SCK_SRC          GPIO_PinSource10
#define W25QXX_MISO_PORT        GPIOC
#define W25QXX_MISO_PIN         GPIO_Pin_11
#define W25QXX_MISO_SRC         GPIO_PinSource11
#define W25QXX_MOSI_PORT        GPIOC
#define W25QXX_MOSI_PIN         GPIO_Pin_12
#define W25QXX_MOSI_SRC         GPIO_PinSource12
#define W25QXX_GPIO_AF          GPIO_AF6

/* ---- CS 控制 ---- */
#define CS_LOW()    GPIO_ResetBits(W25QXX_CS_PORT, W25QXX_CS_PIN)
#define CS_HIGH()   GPIO_SetBits(W25QXX_CS_PORT, W25QXX_CS_PIN)

/* ---- SPI 收发 ---- */
static uint8_t SPI_WriteReadByte(uint8_t data)
{
    while(SPI_I2S_GetFlagStatus(W25QXX_SPI, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(W25QXX_SPI, data);
    while(SPI_I2S_GetFlagStatus(W25QXX_SPI, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(W25QXX_SPI);
}

/* ---- 内部辅助 ---- */
static void W25QXX_WriteEnable(void)
{
    CS_LOW();
    SPI_WriteReadByte(W25QXX_CMD_WRITE_ENABLE);
    CS_HIGH();
}

static void W25QXX_WaitBusy(void)
{
    uint32_t timeout = 0;
    CS_LOW();
    SPI_WriteReadByte(W25QXX_CMD_READ_STATUS_REG1);
    while((SPI_WriteReadByte(0xFF) & 0x01) && timeout < 0xFFFF)
        timeout++;
    CS_HIGH();
}

/* ---- 公开 API ---- */

void W25QXX_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 使能时钟 */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA | RCC_HB2Periph_GPIOC | RCC_HB2Periph_AFIO, ENABLE);
    RCC_HB1PeriphClockCmd(W25QXX_SPI_CLK, ENABLE);

    /* 禁用 JTAG（保留 SWD），释放 PA15 给 GPIO 使用
     * AFIO_PCFR1 SWJ_CFG[2:0]: 010 = JTAG关、SWD开 */
    AFIO->PCFR1 = (AFIO->PCFR1 & ~AFIO_PCFR1_SWJ_CFG) | (0x02 << 24);

    /* PA15: CS — 推挽输出 */
    gpio.GPIO_Pin   = W25QXX_CS_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(W25QXX_CS_PORT, &gpio);
    CS_HIGH();

    /* PC10(SCK) / PC11(MISO) / PC12(MOSI): AF6 复用推挽 */
    gpio.GPIO_Pin   = W25QXX_SCK_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(W25QXX_SCK_PORT, &gpio);

    gpio.GPIO_Pin  = W25QXX_MISO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(W25QXX_MISO_PORT, &gpio);

    gpio.GPIO_Pin   = W25QXX_MOSI_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(W25QXX_MOSI_PORT, &gpio);

    GPIO_PinAFConfig(W25QXX_SCK_PORT,  W25QXX_SCK_SRC,  W25QXX_GPIO_AF);
    GPIO_PinAFConfig(W25QXX_MISO_PORT, W25QXX_MISO_SRC, W25QXX_GPIO_AF);
    GPIO_PinAFConfig(W25QXX_MOSI_PORT, W25QXX_MOSI_SRC, W25QXX_GPIO_AF);

    /* SPI3 配置: Mode 0, 8bit, MSB, Master, 软件CS */
    {
        SPI_InitTypeDef spi;
        spi.SPI_Direction          = SPI_Direction_2Lines_FullDuplex;
        spi.SPI_Mode               = SPI_Mode_Master;
        spi.SPI_DataSize           = SPI_DataSize_8b;
        spi.SPI_CPOL               = SPI_CPOL_Low;
        spi.SPI_CPHA               = SPI_CPHA_1Edge;
        spi.SPI_NSS                = SPI_NSS_Soft;
        spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_Mode6;  /* fPCLK/128 */
        spi.SPI_FirstBit           = SPI_FirstBit_MSB;
        spi.SPI_CRCPolynomial      = 7;
        SPI_Init(W25QXX_SPI, &spi);
    }
    SPI_Cmd(W25QXX_SPI, ENABLE);

    /* 释放掉电模式 */
    CS_LOW();
    SPI_WriteReadByte(W25QXX_CMD_RELEASE_PD);
    CS_HIGH();
    Delay_Ms(1);

    printf("[W25Q] 初始化完成\r\n");
}

uint32_t W25QXX_ReadID(void)
{
    uint32_t id;
    CS_LOW();
    SPI_WriteReadByte(W25QXX_CMD_JEDEC_ID);
    id  = (uint32_t)SPI_WriteReadByte(0xFF) << 16;
    id |= (uint32_t)SPI_WriteReadByte(0xFF) << 8;
    id |= (uint32_t)SPI_WriteReadByte(0xFF);
    CS_HIGH();
    return id;
}

uint8_t W25QXX_ReadByte(uint32_t addr)
{
    uint8_t val;
    CS_LOW();
    SPI_WriteReadByte(W25QXX_CMD_READ_DATA);
    SPI_WriteReadByte((addr >> 16) & 0xFF);
    SPI_WriteReadByte((addr >> 8)  & 0xFF);
    SPI_WriteReadByte( addr        & 0xFF);
    val = SPI_WriteReadByte(0xFF);
    CS_HIGH();
    return val;
}

void W25QXX_ReadBuffer(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    CS_LOW();
    SPI_WriteReadByte(W25QXX_CMD_READ_DATA);
    SPI_WriteReadByte((addr >> 16) & 0xFF);
    SPI_WriteReadByte((addr >> 8)  & 0xFF);
    SPI_WriteReadByte( addr        & 0xFF);
    for(i = 0; i < len; i++)
        buf[i] = SPI_WriteReadByte(0xFF);
    CS_HIGH();
}

void W25QXX_WritePage(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    if(len > W25QXX_PAGE_SIZE) len = W25QXX_PAGE_SIZE;

    W25QXX_WriteEnable();
    CS_LOW();
    SPI_WriteReadByte(W25QXX_CMD_PAGE_PROGRAM);
    SPI_WriteReadByte((addr >> 16) & 0xFF);
    SPI_WriteReadByte((addr >> 8)  & 0xFF);
    SPI_WriteReadByte( addr        & 0xFF);
    for(i = 0; i < len; i++)
        SPI_WriteReadByte(buf[i]);
    CS_HIGH();
    W25QXX_WaitBusy();
}

void W25QXX_EraseSector(uint32_t addr)
{
    addr &= ~(W25QXX_SECTOR_SIZE - 1);  /* 对齐到4KB边界 */
    W25QXX_WriteEnable();
    CS_LOW();
    SPI_WriteReadByte(W25QXX_CMD_SECTOR_ERASE);
    SPI_WriteReadByte((addr >> 16) & 0xFF);
    SPI_WriteReadByte((addr >> 8)  & 0xFF);
    SPI_WriteReadByte( addr        & 0xFF);
    CS_HIGH();
    W25QXX_WaitBusy();
}

void W25QXX_EraseChip(void)
{
    W25QXX_WriteEnable();
    CS_LOW();
    SPI_WriteReadByte(W25QXX_CMD_CHIP_ERASE);
    CS_HIGH();
    W25QXX_WaitBusy();
}
