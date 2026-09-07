/**
 * @file    k230.c
 * @brief   K230 视觉模块串口驱动 (USART5, 中断+环形缓冲区)
 *
 *          RX 帧 (K230→H417):
 *            病虫害帧: 0xAA 0x55 0x01 0x08 Data[8] CRC 0x55 0xAA (15字节)
 *            跟踪帧:   0xAA 0x55 0x03 0x10 Data[16] CRC 0x55 0xAA (23字节)
 *            图像帧:   0xAA 0x55 0x10 [dlen] [stuffed_payload] CRC 0x55 0xAA (变长)
 *          TX 帧 (H417→K230): 0xAA 0x55 Cmd 0x55 0xAA (5字节)
 *          引脚: PE2=RX(AF4), PE3=TX(AF11)
 */
#include "k230.h"
#include "shared.h"
#include <string.h>
#include "debug.h"

/* ==================== 环形缓冲区 ==================== */
static volatile uint8_t  rx_buf[K230_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

/* ==================== 帧解析状态机 ==================== */
typedef enum {
    PARSE_WAIT_HEADER1,
    PARSE_WAIT_HEADER2,
    PARSE_RECV_DATA,
    /* IMAGE 帧接收状态 */
    PARSE_IMAGE_RECV_DLEN_HI,   /* 收集 dlen 高字节（低字节已在 PARSE_RECV_DATA 中收集） */
    PARSE_IMAGE_RECV_PAYLOAD,   /* 收集 stuffed payload (逐字节 destuff) */
    PARSE_IMAGE_RECV_CRC,       /* 收集 CRC */
    PARSE_IMAGE_RECV_FOOTER1,   /* 校验 0x55 */
    PARSE_IMAGE_RECV_FOOTER2    /* 校验 0xAA，处理 chunk */
} ParseState;

static ParseState parse_state = PARSE_WAIT_HEADER1;
static uint8_t    parse_buf[K230_TRACK_FRAME_LEN]; /* 最大23字节 */
static uint8_t    parse_idx = 0;
static uint8_t    parse_frame_len = 0;
static uint8_t    parse_data_len = 0;

/* ==================== IMAGE 帧接收状态 ==================== */
#define IMAGE_CHUNK_MAX_BYTES  600   /* 单个 chunk 最大字节数（含 header/footer，溢出则重置） */
static uint16_t   image_dlen = 0;            /* stuffed payload 长度 */
static uint16_t   image_payload_idx = 0;     /* 已接收 payload 字节数 */
static uint16_t   image_rx_count = 0;        /* 当前 chunk 已接收总字节数（用于超时保护） */
static uint8_t    image_destuff_buf[K230_IMAGE_CHUNK_MAX]; /* destuff 输出 (256B) */
static uint16_t   image_destuffed_len = 0;   /* destuff 后长度 */
static uint8_t    image_prev_byte = 0;       /* destuff 前一字节跟踪 */

/* ==================== IMAGE 帧组装 ==================== */
static uint8_t    image_frame_buf[K230_IMAGE_FRAME_SIZE];   /* 9600B 帧缓冲 */
static uint8_t    image_chunk_bitmap[K230_IMAGE_MAX_CHUNKS]; /* chunk 位图 */
static uint16_t   image_cur_frame_id = 0xFFFF;
static uint16_t   image_total_chunks = 0;
static uint16_t   image_chunks_ok = 0;

/* ==================== 最新结果 ==================== */
static k230_rx_frame_t latest_frame = {0};
static uint8_t result_seq = 0;
static volatile uint16_t rx_error_count = 0;
static volatile uint32_t rx_byte_count = 0;

/* ==================== 虫害类型名称映射 ==================== */
static void pest_type_to_names(uint8_t t, const char **plant, const char **pest)
{
    static const char *names[][2] = {
        {"玉米","非生物病害"}, {"玉米","蚜虫"}, {"玉米","弯孢霉叶斑病"},
        {"",""}, {"玉米","蠕孢菌叶斑病"}, {"玉米","健康"},
        {"玉米","锈病"}, {"玉米","草地贪夜蛾"}, {"玉米","草地贪夜蛾"},
        {"玉米","条纹病"}, {"",""}
    };
    if (t < 11) { *plant = names[t][0]; *pest = names[t][1]; }
    else { *plant = "未知"; *pest = ""; }
}

/* ==================== 中断服务程序 ==================== */
void USART5_IRQHandler(void) __attribute__((interrupt()));
void USART5_IRQHandler(void)
{
    if (USART_GetITStatus(USART5, USART_IT_RXNE) != RESET)
    {
        uint8_t data = (uint8_t)USART_ReceiveData(USART5);
        uint16_t next = (rx_head + 1) % K230_RX_BUF_SIZE;
        if (next != rx_tail) {
            rx_buf[rx_head] = data;
            rx_head = next;
        }
        rx_byte_count++;
    }
}

/* ==================== 环形缓冲区读取 ==================== */
static bool rx_read(uint8_t *data)
{
    if (rx_head == rx_tail) return false;
    *data = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % K230_RX_BUF_SIZE;
    return true;
}

/* ==================== CRC 校验 ==================== */
static uint8_t calc_crc(const uint8_t *buf, uint8_t data_len)
{
    uint8_t chk = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];
    for (uint8_t j = 0; j < data_len; j++)
        chk ^= buf[4 + j];
    return chk;
}

/* ==================== IMAGE chunk 处理 ==================== */
static void process_image_chunk(uint8_t *data, uint16_t len)
{
    if (len < 10) return;

    uint16_t frame_id     = data[0] | ((uint16_t)data[1] << 8);
    uint16_t chunk_idx    = data[2] | ((uint16_t)data[3] << 8);
    uint16_t total_chunks = data[4] | ((uint16_t)data[5] << 8);
    uint32_t chunk_size   = data[6] | ((uint32_t)data[7] << 8) |
                            ((uint32_t)data[8] << 16) | ((uint32_t)data[9] << 24);

    if (total_chunks == 0 || total_chunks > K230_IMAGE_MAX_CHUNKS) return;
    if (chunk_idx >= total_chunks) return;
    if (chunk_size > 200 || len < 10 + chunk_size) return;

    /* 新帧 ID? 重置状态 */
    if (frame_id != image_cur_frame_id) {
        image_cur_frame_id = frame_id;
        image_total_chunks = total_chunks;
        image_chunks_ok = 0;
        memset(image_chunk_bitmap, 0, sizeof(image_chunk_bitmap));
    }

    /* 存储 chunk（如果未收到过） */
    if (!image_chunk_bitmap[chunk_idx]) {
        uint32_t offset = ((uint32_t)chunk_idx * K230_IMAGE_FRAME_SIZE) / (uint32_t)total_chunks;
        if (offset + chunk_size <= K230_IMAGE_FRAME_SIZE) {
            memcpy(image_frame_buf + offset, data + 10, chunk_size);
            image_chunk_bitmap[chunk_idx] = 1;
            image_chunks_ok++;
        }
    }

    /* 帧完整 → 写入共享内存 */
    if (image_chunks_ok >= image_total_chunks && image_total_chunks > 0) {
        memcpy((void *)SharedImageData.img_data, image_frame_buf, K230_IMAGE_FRAME_SIZE);
        SharedImageData.img_valid = 1;
        SharedImageData.img_seq++;
        image_chunks_ok = 0;
    }
}

/* ==================== 初始化 ==================== */
void K230_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOE | RCC_HB2Periph_AFIO, ENABLE);

    GPIO_PinAFConfig(GPIOE, GPIO_PinSource2, GPIO_AF4);
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    GPIO_PinAFConfig(GPIOE, GPIO_PinSource3, GPIO_AF11);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_USART5, ENABLE);

    USART_InitStructure.USART_BaudRate            = baudrate;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART5, &USART_InitStructure);

    USART_ITConfig(USART5, USART_IT_RXNE, ENABLE);
    NVIC_EnableIRQ(USART5_IRQn);
    USART_Cmd(USART5, ENABLE);
    printf("K230: USART5 初始化完成 (%lu-8N1)\r\n", (unsigned long)baudrate);
}

void K230_DeInit(void)
{
    USART_Cmd(USART5, DISABLE);
    USART_ITConfig(USART5, USART_IT_RXNE, DISABLE);
}

/* ==================== 发送命令帧 ==================== */
void K230_SendCmd(uint8_t cmd)
{
    uint8_t frame[K230_TX_FRAME_LEN] = {K230_HEADER1, K230_HEADER2, cmd, K230_FOOTER1, K230_FOOTER2};
    for (uint8_t i = 0; i < K230_TX_FRAME_LEN; i++) {
        uint32_t timeout = 100000;
        while (USART_GetFlagStatus(USART5, USART_FLAG_TXE) == RESET && --timeout);
        if (timeout == 0) return;
        USART_SendData(USART5, frame[i]);
    }
}

/* ==================== 帧解析（状态机） ==================== */
bool K230_GetResult(k230_rx_frame_t *frame)
{
    uint8_t byte;
    while (rx_read(&byte))
    {
        switch (parse_state)
        {
            case PARSE_WAIT_HEADER1:
                if (byte == K230_HEADER1) {
                    parse_buf[0] = byte;
                    parse_state = PARSE_WAIT_HEADER2;
                }
                break;

            case PARSE_WAIT_HEADER2:
                if (byte == K230_HEADER2) {
                    parse_buf[1] = byte;
                    parse_idx = 2;
                    parse_frame_len = 0xFF;
                    parse_state = PARSE_RECV_DATA;
                } else {
                    parse_state = PARSE_WAIT_HEADER1;
                }
                break;

            case PARSE_RECV_DATA:
                parse_buf[parse_idx++] = byte;

                /* 收到第4字节时确定帧长度 */
                if (parse_idx == 4) {
                    uint8_t type = parse_buf[2];
                    if (type == K230_TYPE_PEST || type == K230_TYPE_HEARTBEAT) {
                        parse_frame_len = K230_PEST_FRAME_LEN;
                        parse_data_len = K230_PEST_DATA_LEN;
                    } else if (type == K230_TYPE_TRACK) {
                        parse_frame_len = K230_TRACK_FRAME_LEN;
                        parse_data_len = K230_TRACK_DATA_LEN;
                    } else if (type == K230_TYPE_IMAGE) {
                        /* IMAGE 帧: parse_buf[3] 已有 dlen_lo，还需收集 dlen_hi */
                        image_dlen = parse_buf[3];  /* dlen_lo 已在 parse_buf[3] */
                        image_rx_count = 4;         /* AA 55 10 dlen_lo 已收到 */
                        parse_state = PARSE_IMAGE_RECV_DLEN_HI;
                        break;
                    } else {
                        parse_state = PARSE_WAIT_HEADER1;
                        break;
                    }
                }

                /* 收集完毕，校验帧 */
                if (parse_idx >= parse_frame_len) {
                    uint8_t type = parse_buf[2];

                    /* 检查帧尾 */
                    if (parse_buf[parse_frame_len - 2] == K230_FOOTER1 &&
                        parse_buf[parse_frame_len - 1] == K230_FOOTER2) {
                        /* CRC 校验 */
                        uint8_t chk = calc_crc(parse_buf, parse_data_len);
                        if (chk == parse_buf[4 + parse_data_len]) {
                            /* CRC 正确，解析数据 */
                            latest_frame.frame_type = type;
                            latest_frame.data_len = parse_data_len;

                            if (type == K230_TYPE_PEST || type == K230_TYPE_HEARTBEAT) {
                                const uint8_t *d = &parse_buf[4];
                                latest_frame.data.pest.pest_type = d[0];
                                latest_frame.data.pest.confidence = d[1];
                                latest_frame.data.pest.recognition_count = (uint16_t)d[2] | ((uint16_t)d[3] << 8);
                                latest_frame.data.pest.crop_type = d[4];
                            } else if (type == K230_TYPE_TRACK) {
                                const uint8_t *d = &parse_buf[4];
                                latest_frame.data.track.x = (uint16_t)d[0] | ((uint16_t)d[1] << 8);
                                latest_frame.data.track.y = (uint16_t)d[2] | ((uint16_t)d[3] << 8);
                                latest_frame.data.track.w = (uint16_t)d[4] | ((uint16_t)d[5] << 8);
                                latest_frame.data.track.h = (uint16_t)d[6] | ((uint16_t)d[7] << 8);
                                latest_frame.data.track.dx = (int16_t)((uint16_t)d[8] | ((uint16_t)d[9] << 8));
                                latest_frame.data.track.dy = (int16_t)((uint16_t)d[10] | ((uint16_t)d[11] << 8));
                                latest_frame.data.track.confidence = d[12];
                                latest_frame.data.track.track_id = d[13];
                                latest_frame.data.track.status = d[14];
                                latest_frame.data.track.reserved = d[15];
                            }

                            result_seq++;
                            parse_state = PARSE_WAIT_HEADER1;
                            if (frame) {
                                memcpy(frame, &latest_frame, sizeof(k230_rx_frame_t));
                                return true;
                            }
                        } else {
                            rx_error_count++;
                            printf("K230: [ERR] CRC exp=0x%02X got=0x%02X\r\n", chk, parse_buf[4 + parse_data_len]);
                            printf("K230: BUF "); for (uint8_t i = 0; i < parse_idx && i < 23; i++) printf("%02X ", parse_buf[i]); printf("\r\n");
                        }
                    } else {
                        rx_error_count++;
                        printf("K230: [ERR] Footer mismatch idx=%u type=0x%02X\r\n", parse_idx, parse_buf[2]);
                        printf("K230: BUF "); for (uint8_t i = 0; i < parse_idx && i < 23; i++) printf("%02X ", parse_buf[i]); printf("\r\n");
                    }
                    parse_state = PARSE_WAIT_HEADER1;
                }
                break;

            /* ========== IMAGE 帧接收状态 ========== */

            case PARSE_IMAGE_RECV_DLEN_HI:
                /* dlen 高字节 */
                image_rx_count++;
                if (image_rx_count > IMAGE_CHUNK_MAX_BYTES) {
                    parse_state = PARSE_WAIT_HEADER1;
                    break;
                }
                image_dlen |= ((uint16_t)byte << 8);
                if (image_dlen < 10 || image_dlen > 500) {
                    parse_state = PARSE_WAIT_HEADER1;
                } else {
                    image_payload_idx = 0;
                    image_destuffed_len = 0;
                    image_prev_byte = 0;
                    parse_state = PARSE_IMAGE_RECV_PAYLOAD;
                }
                break;

            case PARSE_IMAGE_RECV_PAYLOAD:
                image_rx_count++;
                if (image_rx_count > IMAGE_CHUNK_MAX_BYTES) {
                    parse_state = PARSE_WAIT_HEADER1;
                    break;
                }
                /* 逐字节 destuff: 0xAA 0x00 → 0xAA */
                if (image_prev_byte == K230_HEADER1 && byte == 0x00) {
                    image_prev_byte = 0;
                    image_payload_idx++;
                    break;
                }
                if (image_destuffed_len < K230_IMAGE_CHUNK_MAX) {
                    image_destuff_buf[image_destuffed_len++] = byte;
                }
                image_prev_byte = byte;
                image_payload_idx++;
                if (image_payload_idx >= image_dlen) {
                    parse_state = PARSE_IMAGE_RECV_CRC;
                }
                break;

            case PARSE_IMAGE_RECV_CRC:
                image_rx_count++;
                if (image_rx_count > IMAGE_CHUNK_MAX_BYTES) {
                    parse_state = PARSE_WAIT_HEADER1;
                    break;
                }
                parse_state = PARSE_IMAGE_RECV_FOOTER1;
                break;

            case PARSE_IMAGE_RECV_FOOTER1:
                image_rx_count++;
                if (image_rx_count > IMAGE_CHUNK_MAX_BYTES) {
                    parse_state = PARSE_WAIT_HEADER1;
                    break;
                }
                if (byte != K230_FOOTER1) {
                    parse_state = PARSE_WAIT_HEADER1;
                } else {
                    parse_state = PARSE_IMAGE_RECV_FOOTER2;
                }
                break;

            case PARSE_IMAGE_RECV_FOOTER2:
                if (byte == K230_FOOTER2) {
                    process_image_chunk(image_destuff_buf, image_destuffed_len);
                }
                parse_state = PARSE_WAIT_HEADER1;
                break;
        }
    }
    return false;
}

/* ==================== 公开接口 ==================== */
uint8_t K230_GetSeq(void) { return result_seq; }
uint16_t K230_GetErrorCount(void) { return rx_error_count; }
uint32_t K230_GetByteCount(void) { return rx_byte_count; }
uint16_t K230_GetBufUsage(void) {
    if (rx_head >= rx_tail) return rx_head - rx_tail;
    return K230_RX_BUF_SIZE - rx_tail + rx_head;
}

const char* K230_GetFrameTypeName(uint8_t type)
{
    switch (type) {
        case K230_TYPE_PEST:      return "PEST";
        case K230_TYPE_HEARTBEAT: return "HEARTBEAT";
        case K230_TYPE_TRACK:     return "TRACK";
        case K230_TYPE_IMAGE:     return "IMAGE";
        default:                  return "UNKNOWN";
    }
}

const char* K230_GetCmdName(uint8_t cmd)
{
    switch (cmd) {
        case K230_CMD_START:       return "START";
        case K230_CMD_STOP:        return "STOP";
        case K230_CMD_MUTE_TX:     return "MUTE";
        case K230_CMD_SWITCH_CROP: return "CROP";
        case K230_CMD_MODE_PEST:   return "MODE_PEST";
        case K230_CMD_MODE_TRACK:  return "MODE_TRACK";
        case K230_CMD_SERVO_HOME:  return "HOME";
        case K230_CMD_SERVO_LEFT:  return "LEFT";
        case K230_CMD_SERVO_RIGHT: return "RIGHT";
        case K230_CMD_SERVO_DOWN:  return "DOWN";
        case K230_CMD_SERVO_UP:    return "UP";
        case K230_CMD_SERVO_PATROL:return "PATROL";
        case K230_CMD_CROP_CORN:   return "CORN";
        case K230_CMD_CROP_POTATO: return "POTATO";
        case K230_CMD_CROP_TOMATO: return "TOMATO";
        case K230_CMD_SEARCH_START:return "SEARCH";
        case K230_CMD_SEARCH_ABORT:return "ABORT";
        default:                   return "?";
    }
}
