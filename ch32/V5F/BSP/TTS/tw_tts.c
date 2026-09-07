/**
 * @file    tw_tts.c
 * @brief   TW-TTS 文字转语音模块驱动
 *
 *          帧格式：FD + 数据长度(2B) + 命令字(1B) + 编码参数(1B) + 文本(NB)
 *          数据长度 = 命令字 + 编码参数 + 文本 = text_len + 2
 *          默认波特率 9600，8N1
 *
 *          硬件连接：
 *          UART3 PB10(AF7,TX) -> TTS 模块 RXD
 *          UART3 PB11(AF7,RX) -> TTS 模块 TXD
 */

#include "tw_tts.h"
#include <string.h>

/* ======================== 硬件配置 ======================== */
#define TTS_USART           USART3
#define TTS_USART_RCC       RCC_HB1Periph_USART3
#define TTS_TX_GPIO         GPIOB
#define TTS_TX_PIN          GPIO_Pin_10
#define TTS_TX_PIN_SRC      GPIO_PinSource10
#define TTS_RX_GPIO         GPIOB
#define TTS_RX_PIN          GPIO_Pin_11
#define TTS_RX_PIN_SRC      GPIO_PinSource11
#define TTS_GPIO_RCC        RCC_HB2Periph_GPIOB
#define TTS_GPIO_AF         GPIO_AF7

/* ======================== 串口发送（阻塞式） ======================== */

static void TTS_SendData(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        while (USART_GetFlagStatus(TTS_USART, USART_FLAG_TC) == RESET);
        USART_SendData(TTS_USART, data[i]);
    }
}

/* ======================== 初始化 ======================== */

void TW_TTS_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStruct = {0};
    USART_InitTypeDef USART_InitStruct = {0};

    /* 使能时钟 */
    RCC_HB1PeriphClockCmd(TTS_USART_RCC, ENABLE);
    RCC_HB2PeriphClockCmd(TTS_GPIO_RCC | RCC_HB2Periph_AFIO, ENABLE);

    /* TX 引脚 — 复用推挽输出 */
    GPIO_PinAFConfig(TTS_TX_GPIO, TTS_TX_PIN_SRC, TTS_GPIO_AF);
    GPIO_InitStruct.GPIO_Pin   = TTS_TX_PIN;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(TTS_TX_GPIO, &GPIO_InitStruct);

    /* RX 引脚 — 浮空输入 */
    GPIO_PinAFConfig(TTS_RX_GPIO, TTS_RX_PIN_SRC, TTS_GPIO_AF);
    GPIO_InitStruct.GPIO_Pin  = TTS_RX_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(TTS_RX_GPIO, &GPIO_InitStruct);

    /* 串口参数：9600 8N1 */
    USART_InitStruct.USART_BaudRate            = 9600;
    USART_InitStruct.USART_WordLength          = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits            = USART_StopBits_1;
    USART_InitStruct.USART_Parity              = USART_Parity_No;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStruct.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(TTS_USART, &USART_InitStruct);
    USART_Cmd(TTS_USART, ENABLE);
}

/* ======================== 播放文本 ======================== */

void TW_TTS_Play(const char *str)
{
    uint8_t header[5];
    uint16_t str_len;

    if (str == NULL)
        return;

    str_len = strlen(str);
    if (str_len == 0 || str_len > 4094)
        return;

    header[0] = TTS_FRAME_HEADER;               /* 帧头 0xFD */
    header[1] = (uint8_t)((str_len + 2) >> 8);   /* 数据长度高位 */
    header[2] = (uint8_t)((str_len + 2) & 0xFF);  /* 数据长度低位 */
    header[3] = TTS_CMD_SPEAK;                   /* 命令字：开始合成 */
    header[4] = TTS_ENCODE_UTF8;                /* 编码参数：UTF-8 */

    /* 发送帧头(5字节) + 文本数据 */
    TTS_SendData(header, 5);
    TTS_SendData((const uint8_t *)str, str_len);
}

/* ======================== 停止 ======================== */

void TW_TTS_Stop(void)
{
    uint8_t frame[4] = {0xFD, 0x00, 0x01, TTS_CMD_STOP};
    TTS_SendData(frame, 4);
}

/* ======================== 暂停 ======================== */

void TW_TTS_Pause(void)
{
    uint8_t frame[4] = {0xFD, 0x00, 0x01, TTS_CMD_PAUSE};
    TTS_SendData(frame, 4);
}

/* ======================== 恢复 ======================== */

void TW_TTS_Resume(void)
{
    uint8_t frame[4] = {0xFD, 0x00, 0x01, TTS_CMD_RESUME};
    TTS_SendData(frame, 4);
}

/* ======================== 语速设置 ======================== */

void TW_TTS_SetSpeed(uint8_t speed)
{
    if (speed > 9) speed = 9;
    uint8_t frame[9] = {0xFD, 0x00, 0x06, 0x01, 0x01,
                        0x5B, 0x73, (uint8_t)(0x30 + speed), 0x5D};
    TTS_SendData(frame, 9);
}

/* ======================== 音量设置 ======================== */

void TW_TTS_SetVolume(uint8_t vol)
{
    if (vol > 9) vol = 9;
    uint8_t frame[9] = {0xFD, 0x00, 0x06, 0x01, 0x01,
                        0x5B, 0x76, (uint8_t)(0x30 + vol), 0x5D};
    TTS_SendData(frame, 9);
}

/* ======================== 语调设置 ======================== */

void TW_TTS_SetTone(uint8_t tone)
{
    if (tone > 9) tone = 9;
    uint8_t frame[9] = {0xFD, 0x00, 0x06, 0x01, 0x01,
                        0x5B, 0x74, (uint8_t)(0x30 + tone), 0x5D};
    TTS_SendData(frame, 9);
}
