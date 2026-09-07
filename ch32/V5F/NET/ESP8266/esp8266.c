//��Ƭ��ͷ�ļ�
#include "ch32h417.h"
#include "debug.h"

//�����豸����
#include "esp8266.h"
#include "onenet.h"

//Ӳ������
#include "usart.h"

//C��
#include <string.h>
#include <stdio.h>

/* ---- 引脚和 WiFi 信息 ---- */
//#define ESP8266_ONENET_INFO		"AT+CIPSTART=\"TCP\",\"183.230.40.39\",6002\r\n"    //�ɰ�OneNET��ַ
#define ESP8266_ONENET_INFO		"AT+CIPSTART=0,\"TCP\",\"mqtts.heclouds.com\",1883\r\n"  	//�°�OneNET��ַ(link 0)
//#define ESP8266_ONENET_INFO		"AT+CIPSTART=\"TCP\",\"192.168.11.125\",8080\r\n"   //���Ե�ַ

unsigned char esp8266_buf[1024];
unsigned short esp8266_cnt = 0, esp8266_cntPre = 0;
extern uint8_t rx_data;

volatile uint8_t esp8266_send_fail = 0;

/* ---- global: stores Date header from last HTTP GET response ---- */
char esp8266_http_date[64];


//==========================================================
//	�������ƣ�	ESP8266_Clear
//
//	�������ܣ�	��ջ�����
//
//	��ڲ�����	��
//
//	���ز�����	��
//
//	˵����		
//==========================================================
void ESP8266_Clear(void)
{

	memset(esp8266_buf, 0, sizeof(esp8266_buf));
	esp8266_cnt = 0;

}

//==========================================================
//	�������ƣ�	ESP8266_WaitRecive
//
//	�������ܣ�	�ȴ��������
//
//	��ڲ�����	��
//
//	���ز�����	REV_OK-�������		REV_WAIT-���ճ�ʱδ���
//
//	˵����		ѭ�����ü���Ƿ�������
//==========================================================
_Bool ESP8266_WaitRecive(void)
{

	if(esp8266_cnt == 0) 							//������ռ�����Ϊ0 ��˵��û�д��ڽ��������У�����ֱ���˳��ȴ�
		return REV_WAIT;
		
	if(esp8266_cnt == esp8266_cntPre)				//�����һ�ε�ֵ����һ����ͬ����˵���������
	{
		esp8266_cnt = 0;							//��0���ռ�����
			
		return REV_OK;								//���ؽ�����ɱ�־
	}
		
	esp8266_cntPre = esp8266_cnt;					//��Ϊ��ͬ
	
	return REV_WAIT;								//���ؽ���δ��ɱ�־

}

//==========================================================
//	�������ƣ�	ESP8266_SendCmd
//
//	�������ܣ�	��������
//
//	��ڲ�����	cmd������
//				res����Ҫ���ķ���ָ��
//
//	���ز�����	0-�ɹ�	1-ʧ��
//
//	˵����		
//==========================================================
_Bool ESP8266_SendCmd(char *cmd, char *res)
{
	
	unsigned char timeOut = 200;
	Usart_SendString(USART2, (unsigned char *)cmd, strlen((const char *)cmd));

	while(timeOut--)
	{
		if(ESP8266_WaitRecive() == REV_OK)							//����յ�����
		{
			if(strstr((const char *)esp8266_buf, res) != NULL)		//����������ؼ���
			{
				ESP8266_Clear();									//��ջ�����
				
				return 0;
			}
		}
		Delay_Ms(10);
	}
	
	return 1;

}

//==========================================================
//	�������ƣ�	ESP8266_SendData
//
//	�������ܣ�	��������
//
//	��ڲ�����	data������
//				len������
//
//	���ز�����	��
//
//	˵����		
//==========================================================
_Bool ESP8266_SendData(unsigned char *data, unsigned short len)
{

	char cmdBuf[50];

	sprintf(cmdBuf, "AT+CIPSEND=0,%d\r\n", len);	//link 0 (OneNet)
	if(!ESP8266_SendCmd(cmdBuf, ">"))				//收到">"时可以发送数据
	{
        Usart_SendString(USART2, data, len);
		esp8266_send_fail = 0;
		return 0;
	}
	else
	{
		printf("WARN: CIPSEND Failed, no '>'\r\n");
		esp8266_send_fail = 1;
		return 1;
	}

}

//==========================================================
//	�������ƣ�	ESP8266_GetIPD
//
//	�������ܣ�	��ȡƽ̨���ص�����
//
//	��ڲ�����	�ȴ�ʱ��(��λ10ms)
//
//	���ز�����	ƽ̨���ص�ԭʼ����
//
//	˵����		��ͬ�����豸���صĸ�ʽ��ͬ����Ҫȥ��ͷ
//				ESP8266�ķ��ظ�ʽΪ	"+IPD,x:yyy"	x�����ݳ��ȣ�yyy����������
//==========================================================
unsigned char *ESP8266_GetIPD(unsigned short timeOut)
{

	char *ptrIPD = NULL;
	
	do
	{
		if(ESP8266_WaitRecive() == REV_OK)								//����������
		{
			ptrIPD = strstr((char *)esp8266_buf, "IPD,");				//����"IPD"ͷ
			if(ptrIPD != NULL)										
			{
				ptrIPD = strchr(ptrIPD, ':');							//�ҵ�':'
				if(ptrIPD != NULL)
				{
					ptrIPD++;
					return (unsigned char *)(ptrIPD);
				}
				else
					return NULL;
				
			}
		}
		Delay_Ms(5);
	} while(timeOut--);
	
	return NULL;														//��ʱδ�ҵ����ؿ�ָ��

}

//==========================================================
//	�������ƣ�	ESP8266_HTTP_Get
//
//	�������ܣ�	ͨ�� ESP8266 ���� HTTP GET ����link 1��
//
//	��ڲ�����	host	- �������������� "api.seniverse.com"
//				path	- HTTP ����·������ "/v3/weather/now.json?..."
//				response- �����Ӧ���ĵĻ�����
//				max_len	- response ��󳤶�
//
//	���ز�����	0-�ɹ���1-ʧ��
//
//	˵����		���ӳɹ����� HTTP/1.1 GET ����
//				����Ӧ����ȡ HTTP ���ĵ� response
//==========================================================
_Bool ESP8266_HTTP_Get(char *host, char *path, char *response, unsigned short max_len)
{
	ESP8266_Clear();

	ESP8266_SendCmd("AT+CIPCLOSE=1\r\n", "OK");
	Delay_Ms(200);
	ESP8266_Clear();

	{
		char cmd[128];
		unsigned short timeOut;
		_Bool connected = 0;
		sprintf(cmd, "AT+CIPSTART=1,\"TCP\",\"%s\",80\r\n", host);
		Usart_SendString(USART2, (unsigned char *)cmd, strlen(cmd));
		timeOut = 500;
		while(timeOut--)
		{
			if(ESP8266_WaitRecive() == REV_OK)
			{
				if(strstr((const char *)esp8266_buf, "CONNECT") != NULL ||
				   strstr((const char *)esp8266_buf, "ALREADY") != NULL)
				{
					connected = 1;
					break;
				}
			}
			Delay_Ms(10);
		}
		ESP8266_Clear();
		if(!connected)
		{
			UsartPrintf(USART2, "HTTP: connect %s failed\r\n", host);
			return 1;
		}
		UsartPrintf(USART2, "HTTP: connected to %s\r\n", host);
	}

	{
		char request[600];
		int req_len = sprintf(request,
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			path, host);

		char cmd[32];
		ESP8266_Clear();
		sprintf(cmd, "AT+CIPSEND=1,%d\r\n", req_len);
		if(ESP8266_SendCmd(cmd, ">"))
		{
			UsartPrintf(USART2, "HTTP: CIPSEND failed\r\n");
			return 1;
		}

		Usart_SendString(USART2, (unsigned char *)request, req_len);
	}

	{
		unsigned short timeOut = 500;
		do
		{
			if(ESP8266_WaitRecive() == REV_OK)
			{
				char *ptrIPD = strstr((char *)esp8266_buf, "+IPD,1,");
				if(ptrIPD != NULL)
				{
					ptrIPD = strchr(ptrIPD, ':');
					if(ptrIPD != NULL)
					{
						ptrIPD++;

						/* ---- extract HTTP Date header for RTC time sync ---- */
						{
							char *date_hdr = strstr(ptrIPD, "Date: ");
							if(date_hdr) {
								date_hdr += 6;   /* skip "Date: " */
								char *date_end = strstr(date_hdr, "\r\n");
								if(date_end) {
									int len = date_end - date_hdr;
									if(len > 63) len = 63;
									memcpy(esp8266_http_date, date_hdr, len);
									esp8266_http_date[len] = '\0';
								}
							}
						}

						char *body = strstr(ptrIPD, "\r\n\r\n");
						if(body != NULL)
						{
							body += 4;   /* skip HTTP header separator */
						}
						else
						{
							/* No HTTP headers �� treat data after ':' as raw body.
							   Look for JSON start '{' to strip any stray prefix. */
							body = strchr(ptrIPD, '{');
						}
						if(body != NULL)
						{
							unsigned short copy_len = strlen(body);
							if(copy_len >= max_len) copy_len = max_len - 1;
							memcpy(response, body, copy_len);
							response[copy_len] = '\0';
							ESP8266_SendCmd("AT+CIPCLOSE=1\r\n", "OK");
							UsartPrintf(USART2, "HTTP: got %d bytes\r\n", copy_len);
							return 0;
						}
					}
				}
			}
			Delay_Ms(10);
		} while(timeOut--);
	}

	UsartPrintf(USART2, "HTTP: response timeout, buf=%s\r\n", esp8266_buf);
	ESP8266_SendCmd("AT+CIPCLOSE=1\r\n", "OK");
	return 1;
}

//==========================================================
//	�������ƣ�	ESP8266_Init
//
//	�������ܣ�	��ʼ��ESP8266
//
//	��ڲ�����	��
//
//	���ز�����	��
//
//	˵����		
//==========================================================
void ESP8266_Init(const char *ssid, const char *password)
{
	GPIO_InitTypeDef GPIO_Initure = {0};
	int retry;
	char wifi_cmd[128];

	RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA | RCC_HB2Periph_GPIOE, ENABLE);

	/* PE6: 使能引脚 */
	GPIO_Initure.GPIO_Pin = GPIO_Pin_6;
	GPIO_Initure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Initure.GPIO_Speed = GPIO_Speed_Very_High;
	GPIO_Init(GPIOE, &GPIO_Initure);
	GPIO_SetBits(GPIOE, GPIO_Pin_6);  /* 使能 ESP8266 */

	/* PE5: 复位引脚 */
	GPIO_Initure.GPIO_Pin = GPIO_Pin_5;
	GPIO_Initure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Initure.GPIO_Speed = GPIO_Speed_Very_High;
	GPIO_Init(GPIOE, &GPIO_Initure);

	/* 硬件复位：拉低复位引脚 500ms，等待模块完全断电 */
	GPIO_ResetBits(GPIOE, GPIO_Pin_5);
	Delay_Ms(500);
	GPIO_SetBits(GPIOE, GPIO_Pin_5);
	/* 等待模块启动完成（ESP8266 启动需要 2-3 秒） */
	Delay_Ms(3000);

	ESP8266_Clear();

	/* 预热：发送空 AT 等待模块就绪，最多等 5 秒 */
	for (retry = 0; retry < 10; retry++)
	{
		ESP8266_Clear();
		Usart_SendString(USART2, (unsigned char *)"AT\r\n", 4);
		Delay_Ms(500);
		if(ESP8266_WaitRecive() == REV_OK &&
		   strstr((const char *)esp8266_buf, "OK") != NULL)
		{
			ESP8266_Clear();
			printf("[ESP] 模块就绪 (第%d次探测)\r\n", retry + 1);
			break;
		}
	}
	if(retry >= 10)
	{
		printf("[ESP] 模块未响应，尝试重新复位...\r\n");
		/* 重新复位 */
		GPIO_ResetBits(GPIOE, GPIO_Pin_5);
		Delay_Ms(500);
		GPIO_SetBits(GPIOE, GPIO_Pin_5);
		Delay_Ms(3000);
		ESP8266_Clear();
		/* 再次探测 */
		for (retry = 0; retry < 5; retry++)
		{
			ESP8266_Clear();
			Usart_SendString(USART2, (unsigned char *)"AT\r\n", 4);
			Delay_Ms(500);
			if(ESP8266_WaitRecive() == REV_OK &&
			   strstr((const char *)esp8266_buf, "OK") != NULL)
			{
				ESP8266_Clear();
				printf("[ESP] 重新复位后模块就绪\r\n");
				break;
			}
		}
		if(retry >= 5)
			printf("[ESP] 模块仍然未响应\r\n");
	}

	for (retry = 0; retry < 5; retry++)
	{
		ESP8266_Clear();
		if(ESP8266_SendCmd("AT\r\n", "OK") == 0)
			break;
		Delay_Ms(500);
	}
	printf("[ESP] AT: %s\r\n", retry < 5 ? "OK" : "FAIL");

	for (retry = 0; retry < 5; retry++)
	{
		ESP8266_Clear();
		if(ESP8266_SendCmd("AT+CWMODE=1\r\n", "OK") == 0)
			break;
		Delay_Ms(500);
	}
	printf("[ESP] CWMODE: %s\r\n", retry < 5 ? "OK" : "FAIL");

	for (retry = 0; retry < 5; retry++)
	{
		ESP8266_Clear();
		if(ESP8266_SendCmd("AT+CIPMUX=1\r\n", "OK") == 0)
			break;
		Delay_Ms(500);
	}
	printf("[ESP] CIPMUX: %s\r\n", retry < 5 ? "OK" : "FAIL");

	/* WiFi 连接 */
	sprintf(wifi_cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);
	printf("[ESP] WiFi SSID=%s\r\n", ssid);	/* 不打印密码 */
	for (retry = 0; retry < 5; retry++)
	{
		ESP8266_Clear();
		printf("[ESP] WiFi 连接中... (第%d次)\r\n", retry + 1);
		if(ESP8266_SendCmd(wifi_cmd, "GOT IP") == 0)
			break;
		Delay_Ms(1000);
	}
	printf("[ESP] WiFi: %s\r\n", retry < 5 ? "OK" : "FAIL");

	/* TCP 连接 */
	for (retry = 0; retry < 3; retry++)
	{
		ESP8266_Clear();
		printf("[ESP] TCP 连接中... (第%d次)\r\n", retry + 1);
		if(ESP8266_SendCmd(ESP8266_ONENET_INFO, "CONNECT") == 0)
			break;
		Delay_Ms(1000);
	}
	printf("[ESP] TCP: %s\r\n", retry < 10 ? "OK" : "FAIL");
}

//==========================================================
//	�������ƣ�	USART2_IRQHandler
//
//	�������ܣ�	����4�����ж�
//
//	��ڲ�����	��
//
//	���ز�����	��
//
//	˵����		
//==========================================================
void USART2_IRQHandler(void) __attribute__((interrupt()));

void USART2_IRQHandler(void)
{
	if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)	//�����ж�
	{
		uint8_t data = USART_ReceiveData(USART2);
		rx_data = data;
		if(esp8266_cnt >= sizeof(esp8266_buf))	esp8266_cnt = 0;	//��ֹ�������ˢ
		esp8266_buf[esp8266_cnt++] = data;
	}
}
