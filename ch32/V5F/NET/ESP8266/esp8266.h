#ifndef _ESP8266_H_
#define _ESP8266_H_

#include <stdint.h>

#define REV_OK		0	//接收完成标志
#define REV_WAIT	1	//接收未完成标志


void ESP8266_Init(const char *ssid, const char *password);

_Bool ESP8266_SendCmd(char *cmd, char *res);
void ESP8266_Clear(void);

_Bool ESP8266_SendData(unsigned char *data, unsigned short len);

unsigned char *ESP8266_GetIPD(unsigned short timeOut);

_Bool ESP8266_HTTP_Get(char *host, char *path, char *response, unsigned short max_len);

extern char esp8266_http_date[64];   /* Date header from last HTTP GET */
extern volatile uint8_t esp8266_send_fail;  /* 1=最近一次发送失败，需重连 */

#endif
