//单片机头文件
#include "ch32h417.h"
#include "FreeRTOS.h"
#include "task.h"

//网络设备
#include "esp8266.h"
#include "debug.h"

//协议文件
#include "onenet.h"
#include "mqttkit.h"
#include "cJSON.h"

//硬件驱动
#include "usart.h"
#include "car_control.h"
#include "shared.h"
#include "storage.h"

extern char g_reported_ssid[];
extern char g_weather_location[33];
volatile _Bool g_weather_announce = 0;
char g_log_control_status[16] = {0};  /* 最近一次日志操作状态 */
volatile _Bool g_cloud_interval_pending = 0;  /* 云端下发间隔命令标志，net_task 消费 */

//C库
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
//==============
/* 云平台配置（PROID / AUTH_INFO / DEVICE_NAME）统一在 onenet.h 中定义 */

extern unsigned char esp8266_buf[1024];

/*==== 连接状态标志 — 由 net_task 统一管理 ====*/
static uint8_t onet_connected = 0;	// 云平台连接状态: 0=未连接 1=已连接

/*==== 设备状态（云平台上报用） ====*/
static _Bool g_led_state    = 0;
static _Bool g_buzzer_state = 0;
static _Bool g_pump_state   = 0;

/*==== 阈值设置（云平台下发 → 共享内存） ====*/
static uint16_t g_temp_threshold  = 20;   /* 温度下限 °C，默认20 */
static uint16_t g_humi_threshold  = 50;   /* 湿度下限 %，默认50 */
static uint16_t g_light_threshold = 0;    /* 光照下限 lux，默认0（禁用） */

/*==== 病害检测（云平台上报） ====*/
static char     g_pest_plant[16] = "-";
static char     g_pest_pest[16]  = "";
static uint8_t  g_pest_confidence = 0;

//==========================================================
//	函数名称：	OneNet_DevLink
//
//	函数功能：	与onenet创建连接
//
//	入口参数：	无
//
//	返回参数：	0-成功		1-失败
//
//	说明：		与onenet平台建立连接
//==========================================================
_Bool OneNet_DevLink(void)
{

	MQTT_PACKET_STRUCTURE mqttPacket = {NULL, 0, 0, 0};	//协议包

	unsigned char *dataPtr;

	_Bool status = 1;

//---------------------------------------------步骤一：组包---------------------------------------------
	if(MQTT_PacketConnect(PROID, AUTH_INFO, DEVICE_NAME, 256, 1, MQTT_QOS_LEVEL0, NULL, NULL, 0, &mqttPacket) == 0)
	{
//---------------------------------------------步骤二：发送数据-----------------------------------------
		if(ESP8266_SendData(mqttPacket._data, mqttPacket._len) != 0)
		{
			printf("WARN: DevLink发送失败\r\n");
			MQTT_DeleteBuffer(&mqttPacket);
			return 1;
		}

//---------------------------------------------步骤三：等待平台响应--------------------------------------
		ESP8266_Clear();
		dataPtr = ESP8266_GetIPD(500);
		if(dataPtr != NULL)
		{
//---------------------------------------------步骤四：判断返回类型--------------------------------------
			if(MQTT_UnPacketRecv(dataPtr) == MQTT_PKT_CONNACK)
			{
//---------------------------------------------步骤五：解析返回结果--------------------------------------
				switch(MQTT_UnPacketConnectAck(dataPtr))
				{
					case 0:
						printf("OneNet连接成功\r\n");
						onet_connected = 1;
						status = 0;
					break;

					case 1:printf("WARN:	OneNet连接失败:协议错误\r\n");break;
					case 2:printf("WARN:	OneNet连接失败:非法的clientid\r\n");break;
					case 3:printf("WARN:	OneNet连接失败:服务器失败\r\n");break;
					case 4:printf("WARN:	OneNet连接失败:用户名或密码错误\r\n");break;
					case 5:printf("WARN:	OneNet连接失败:非法链接(比如token非法)\r\n");break;

					default:printf("ERR:	OneNet连接失败:未知错误\r\n");break;
				}
			}
		}
		else
		{
			printf("WARN: DevLink无平台响应\r\n");
		}

//---------------------------------------------步骤六：删包---------------------------------------------
		MQTT_DeleteBuffer(&mqttPacket);
	}
	else
		printf("WARN:	MQTT_PacketConnect Failed\r\n");

	return status;

}


//==========================================================
//	函数名称：	MqttOnenet_Savedata
//
//	函数功能：	构建物模型JSON数据
//==========================================================
unsigned short MqttOnenet_Savedata(char *t_payload, float temp, float humi, float light)
{
    char json[]="{\"id\":\"123\",\"version\":\"1.0\",\"params\":{\"temp\":{\"value\":%.1f},\"humi\":{\"value\":%.1f},\"light\":{\"value\":%.1f},\"pump\":{\"value\":%s},\"led\":{\"value\":%s},\"buzzer\":{\"value\":%s},\"car_mode\":{\"value\":%d},\"car_speed\":{\"value\":%d},\"temp_threshold\":{\"value\":%u},\"humi_threshold\":{\"value\":%u},\"light_threshold\":{\"value\":%u},\"pest_info\":{\"value\":\"%s\"},\"wifi_status\":{\"value\":%d},\"bh1750_status\":{\"value\":%d},\"sht30_status\":{\"value\":%d},\"k230_cmd\":{\"value\":%d},\"tts_mode\":{\"value\":%d},\"tts_volume\":{\"value\":%d},\"tts_tone\":{\"value\":%d},\"tts_speed\":{\"value\":%d},\"wifi_config\":{\"value\":\"%s\"},\"weather_location\":{\"value\":\"%s\"},\"log_control\":{\"value\":\"%s\"}}}";

    char t_json[1200];
    char pest_buf[96];
    unsigned short json_len;
    snprintf(pest_buf, sizeof(pest_buf), "%s,%s,%d",
             g_pest_plant, g_pest_pest, g_pest_confidence);
	sprintf(t_json, json, temp, humi, light,
            g_pump_state   ? "true" : "false",
            g_led_state    ? "true" : "false",
            g_buzzer_state ? "true" : "false",
            (int)CarControl_GetMode(),
            (int)CarControl_GetSpeedGear(),
            SharedThresholdData.temp_threshold,
            SharedThresholdData.humi_threshold,
            SharedThresholdData.light_threshold,
            pest_buf,
            SharedTimeData.wifi_ok,
            SharedSensorData.bh1750_ok,
            SharedSensorData.sht30_ok,
            (int)SharedPestData.k230_cmd,
            (int)SharedThresholdData.mute,
            (int)SharedThresholdData.tts_volume,
            (int)SharedThresholdData.tts_tone,
            (int)SharedThresholdData.tts_speed,
            g_reported_ssid,
            g_weather_location,
            g_log_control_status);

    json_len = strlen(t_json)/sizeof(char);
	memcpy(t_payload, t_json, json_len);

    return json_len;
}


//==========================================================
//	函数名称：	OneNet_SendData
//
//	函数功能：	上传数据到平台
//
//	返回参数：	0-成功	1-失败
//==========================================================
_Bool OneNet_SendData(float temp, float humi, float light)
{

	MQTT_PACKET_STRUCTURE mqttPacket = {NULL, 0, 0, 0};		//协议包

	_Bool status = 0;
	short body_len = 0, i = 0;
	char buf[1200];

	// 未连接云平台时不发送数据
	if(!onet_connected)
	{
		printf("WARN: 未连接云平台，跳过数据发送\r\n");
		return 1;
	}

//---------------------------------------------步骤一：构建JSON数据---------------------------------------------
	memset(buf, 0, sizeof(buf));
	body_len = MqttOnenet_Savedata(buf, temp, humi, light);

	if(body_len)
	{
//---------------------------------------------步骤二：填写协议头-------------------------------------------------
		if(MQTT_PacketSaveData(DEVICE_NAME, body_len, NULL, 5, &mqttPacket) == 0)
		{
//---------------------------------------------步骤三：追加payload到MQTT包---------------------------------------
			for(; i < body_len; i++)
				mqttPacket._data[mqttPacket._len++] = buf[i];

//---------------------------------------------步骤四：发送数据-------------------------------------------------
			if(ESP8266_SendData(mqttPacket._data, mqttPacket._len) == 0)
			{
				printf("云平台: T=%.1f H=%.1f L=%.1f (%d Bytes)\r\n", temp, humi, light, mqttPacket._len);
			}
			else
			{
				printf("WARN: 云平台数据发送失败\r\n");
				status = 1;
			}

//---------------------------------------------步骤五：删包------------------------------------------------------
			MQTT_DeleteBuffer(&mqttPacket);
		}
		else
			printf("WARN: MQTT_PacketSaveData Failed\r\n");
	}
	else
		status = 1;

	return status;

}


//==========================================================
//	函数名称：	OneNET_Publish
//
//	函数功能：	发布消息
//==========================================================
void OneNET_Publish(const char *topic, const char *msg)
{

	MQTT_PACKET_STRUCTURE mqtt_packet = {NULL, 0, 0, 0};						//协议包

	if(!onet_connected)
	{
		printf("WARN: 未连接云平台，跳过Publish\r\n");
		return;
	}

	printf("Publish Topic: %s, Msg: %s\r\n", topic, msg);

	if(MQTT_PacketPublish(MQTT_PUBLISH_ID, topic, msg, strlen(msg), MQTT_QOS_LEVEL0, 0, 1, &mqtt_packet) == 0)
	{
		if(ESP8266_SendData(mqtt_packet._data, mqtt_packet._len) != 0)
			printf("WARN: Publish发送失败\r\n");

		MQTT_DeleteBuffer(&mqtt_packet);
	}

}


//==========================================================
//	函数名称：	OneNET_Subscribe
//
//	函数功能：	订阅
//==========================================================
void OneNET_Subscribe(void)
{

	MQTT_PACKET_STRUCTURE mqtt_packet = {NULL, 0, 0, 0};						//协议包

	char topic_buf[128];
	const char *topic = topic_buf;

	snprintf(topic_buf, sizeof(topic_buf), "$sys/%s/%s/thing/property/set", PROID, DEVICE_NAME);

	printf("Subscribe Topic: %s\r\n", topic_buf);

	if(MQTT_PacketSubscribe(MQTT_SUBSCRIBE_ID, MQTT_QOS_LEVEL0, &topic, 1, &mqtt_packet) == 0)
	{
		if(ESP8266_SendData(mqtt_packet._data, mqtt_packet._len) != 0)
			printf("WARN: Subscribe发送失败\r\n");

		MQTT_DeleteBuffer(&mqtt_packet);
	}

}


//==========================================================
//	函数名称：	OneNet_HeartBeat
//
//	函数功能：	心跳检测（PINGREQ）
//
//	返回参数：	0-成功	1-失败
//==========================================================
_Bool OneNet_HeartBeat(void)
{

	MQTT_PACKET_STRUCTURE mqtt_packet = {NULL, 0, 0, 0};	//协议包
	unsigned char *dataPtr;

	if(MQTT_PacketPing(&mqtt_packet) != 0)
		return 1;

	if(ESP8266_SendData(mqtt_packet._data, mqtt_packet._len) != 0)
	{
		MQTT_DeleteBuffer(&mqtt_packet);
		return 1;
	}
	MQTT_DeleteBuffer(&mqtt_packet);

	ESP8266_Clear();
	{
		int retry;
		for(retry = 0; retry < 15; retry++)
		{
			Delay_Ms(100);
			dataPtr = ESP8266_GetIPD(0);
			if(dataPtr != NULL)
			{
				if(MQTT_UnPacketRecv(dataPtr) == MQTT_PKT_PINGRESP)
					break;
			}
		}
		if(retry < 15)
			return 0;
	}
	return 1;

}


//==========================================================
//	函数名称：	OneNet_RevPro
//
//	函数功能：	平台返回数据检测
//
//	入口参数：	dataPtr：平台返回的数据
//
//	返回参数：	无
//
//	说明：		
//==========================================================
void OneNet_RevPro(unsigned char *cmd)
{
    char *req_payload = NULL;
    char *cmdid_topic = NULL;
    unsigned char qos = 0;
    static unsigned short pkt_id = 0;
    unsigned short topic_len = 0;
    unsigned short req_len = 0;
    unsigned char type = 0;
    short result = 0;

    cJSON *raw_json, *params_json, *item_json;
    char reply_topic[64];
    char reply_msg[128];

    type = MQTT_UnPacketRecv(cmd);

    switch (type)
    {
        case MQTT_PKT_PUBLISH:
            result = MQTT_UnPacketPublish(cmd, &cmdid_topic, &topic_len, &req_payload, &req_len, &qos, &pkt_id);
            if (result == 0 && req_payload != NULL)
            {
                raw_json = cJSON_Parse(req_payload);
                if (raw_json != NULL)
                {
                    params_json = cJSON_GetObjectItem(raw_json, "params");
                    if (params_json == NULL)
                        params_json = raw_json;

                    /* ---- 解析 car_control ---- */
                    item_json = cJSON_GetObjectItem(params_json, "car_control");
                    if (item_json != NULL && item_json->valuestring != NULL)
                    {
                        const char *val = item_json->valuestring;
                        printf("NET: 收到 car_control=%s\r\n", val);

                        if (strcmp(val, "forward") == 0)
                            CarControl_SetCmd(CAR_CMD_FORWARD);
                        else if (strcmp(val, "backward") == 0)
                            CarControl_SetCmd(CAR_CMD_BACKWARD);
                        else if (strcmp(val, "left") == 0)
                            CarControl_SetCmd(CAR_CMD_LEFT);
                        else if (strcmp(val, "right") == 0)
                            CarControl_SetCmd(CAR_CMD_RIGHT);
                        else if (strcmp(val, "stop") == 0)
                            CarControl_SetCmd(CAR_CMD_STOP);
                        else if (strcmp(val, "avoid") == 0)
                            CarControl_SetCmd(CAR_CMD_AVOID);
                        else if (strcmp(val, "manual") == 0)
                            CarControl_SetCmd(CAR_CMD_MANUAL);
                        else if (strcmp(val, "follow") == 0)
                            CarControl_SetCmd(CAR_CMD_FOLLOW);
                    }

                    /* ---- 解析 car_speed（int 类型） ---- */
                    item_json = cJSON_GetObjectItem(params_json, "car_speed");
                    if (item_json != NULL && item_json->type == cJSON_Number)
                    {
                        uint8_t gear = (uint8_t)item_json->valueint;
                        printf("NET: 收到 car_speed=%d\r\n", gear);
                        CarControl_SetSpeedGear(gear);
                    }

                    /* ---- 解析 led / buzzer / pump（bool 类型） ---- */
                    item_json = cJSON_GetObjectItem(params_json, "led");
                    if (item_json != NULL)
                    {
                        _Bool val = (item_json->type == cJSON_True) ||
                                    (item_json->type == cJSON_Number && item_json->valueint == 1);
                        Device_SetLed(val);
                        g_led_state = val;
                        printf("NET: led=%d\r\n", val);
                    }

                    item_json = cJSON_GetObjectItem(params_json, "buzzer");
                    if (item_json != NULL)
                    {
                        _Bool val = (item_json->type == cJSON_True) ||
                                    (item_json->type == cJSON_Number && item_json->valueint == 1);
                        Device_SetBuzzer(val);
                        g_buzzer_state = val;
                        printf("NET: buzzer=%d\r\n", val);
                    }

                    item_json = cJSON_GetObjectItem(params_json, "pump");
                    if (item_json != NULL)
                    {
                        _Bool val = (item_json->type == cJSON_True) ||
                                    (item_json->type == cJSON_Number && item_json->valueint == 1);
                        Device_SetPump(val);
                        g_pump_state = val;
                        printf("NET: pump=%d\r\n", val);
                    }

                    /* ---- 阈值设置（int 类型） ---- */
                    item_json = cJSON_GetObjectItem(params_json, "temp_threshold");
                    if (item_json != NULL && item_json->type == cJSON_Number)
                    {
                        Threshold_SetTemp((uint16_t)item_json->valueint);
                    }

                    item_json = cJSON_GetObjectItem(params_json, "humi_threshold");
                    if (item_json != NULL && item_json->type == cJSON_Number)
                    {
                        Threshold_SetHumi((uint16_t)item_json->valueint);
                    }

                    item_json = cJSON_GetObjectItem(params_json, "light_threshold");
                    if (item_json != NULL && item_json->type == cJSON_Number)
                    {
                        Threshold_SetLight((uint16_t)item_json->valueint);
                    }

                    /* ---- 解析 K230 命令（int 类型，1-12 对应 0x01-0x0C） ---- */
                    item_json = cJSON_GetObjectItem(params_json, "k230_cmd");
                    if (item_json != NULL && item_json->type == cJSON_Number)
                    {
                        int cmd = item_json->valueint;
                        if (cmd >= 1 && cmd <= 12) {
                            SharedPestData.k230_cmd = (uint8_t)cmd;
                            SharedPestData.k230_cmd_seq++;
                            printf("NET: 收到 k230_cmd=%d (0x%02X)\r\n", cmd, cmd);
                        }
                    }

                    /* ---- 解析 wifi_config（string 类型，格式 "ssid,password"） ---- */
                    item_json = cJSON_GetObjectItem(params_json, "wifi_config");
                    if (item_json != NULL && item_json->valuestring != NULL)
                    {
                        const char *cfg = item_json->valuestring;
                        printf("NET: 收到WiFi配网指令\r\n");
                        const char *sep = strchr(cfg, ',');
                        if (sep != NULL && sep > cfg)
                        {
                            size_t ssid_len = (size_t)(sep - cfg);
                            const char *pass = sep + 1;
                            if (ssid_len < sizeof(SharedWiFiData.ssid) && strlen(pass) < sizeof(SharedWiFiData.password))
                            {
                                memset((char *)SharedWiFiData.ssid, 0, sizeof(SharedWiFiData.ssid));
                                memcpy((char *)SharedWiFiData.ssid, cfg, ssid_len);
                                SharedWiFiData.ssid[ssid_len] = '\0';
                                memset((char *)SharedWiFiData.password, 0, sizeof(SharedWiFiData.password));
                                strncpy((char *)SharedWiFiData.password, pass, sizeof(SharedWiFiData.password) - 1);
                                SharedWiFiData.wifi_cmd_seq++;
                                printf("NET: WiFi配网 SSID=%s\r\n", SharedWiFiData.ssid);	/* 不打印密码 */
                            }
                            else
                            {
                                printf("WARN: wifi_config 参数过长\r\n");
                            }
                        }
                        else
                        {
                            printf("WARN: wifi_config 格式错误，需要 ssid,password\r\n");
                        }
                    }

                    /* ---- 解析 tts_mode（int 类型，0=静音 1=正常） ---- */
                    item_json = cJSON_GetObjectItem(params_json, "tts_mode");
                    if (item_json != NULL && item_json->type == cJSON_Number)
                    {
                        uint8_t mode = (uint8_t)item_json->valueint;
                        SharedThresholdData.mute = mode;
                        SharedThresholdData.threshold_seq++;
                        printf("NET: tts_mode=%d\r\n", mode);
                    }

                    /* ---- 解析 tts_volume（int 类型，0-9） ---- */
                    item_json = cJSON_GetObjectItem(params_json, "tts_volume");
                    if (item_json != NULL && item_json->type == cJSON_Number)
                    {
                        uint8_t vol = (uint8_t)item_json->valueint;
                        SharedThresholdData.tts_volume = vol;
                        SharedThresholdData.threshold_seq++;
                        printf("NET: tts_volume=%d\r\n", vol);
                    }

                    /* ---- 解析 tts_tone（int 类型，0-9） ---- */
                    item_json = cJSON_GetObjectItem(params_json, "tts_tone");
                    if (item_json != NULL && item_json->type == cJSON_Number)
                    {
                        uint8_t tone = (uint8_t)item_json->valueint;
                        SharedThresholdData.tts_tone = tone;
                        SharedThresholdData.threshold_seq++;
                        printf("NET: tts_tone=%d\r\n", tone);
                    }

                    /* ---- 解析 tts_speed（int 类型，0-9） ---- */
                    item_json = cJSON_GetObjectItem(params_json, "tts_speed");
                    if (item_json != NULL && item_json->type == cJSON_Number)
                    {
                        uint8_t spd = (uint8_t)item_json->valueint;
                        SharedThresholdData.tts_speed = spd;
                        SharedThresholdData.threshold_seq++;
                        printf("NET: tts_speed=%d\r\n", spd);
                    }

                    /* ---- 解析 weather_location（string 类型） ---- */
                    item_json = cJSON_GetObjectItem(params_json, "weather_location");
                    if (item_json != NULL && item_json->valuestring != NULL)
                    {
                        const char *loc = item_json->valuestring;
                        if (loc[0] != '\0')
                        {
                            strncpy(g_weather_location, loc, sizeof(g_weather_location) - 1);
                            g_weather_location[sizeof(g_weather_location) - 1] = '\0';
                            printf("NET: weather_location=%s\r\n", g_weather_location);
                            /* 保存到 Flash */
                            WeatherSettings_t ws;
                            memset(&ws, 0, sizeof(ws));
                            strncpy(ws.location, g_weather_location, sizeof(ws.location) - 1);
                            Storage_SaveWeather(&ws);
                            /* 通知 net_task 获取天气 + 播报 + 同步共享内存 */
                            g_weather_announce = 1;
                        }
                    }

                    /* ---- 解析 log_control（string 类型） ---- */
                    item_json = cJSON_GetObjectItem(params_json, "log_control");
                    if (item_json != NULL && item_json->valuestring != NULL)
                    {
                        const char *cmd = item_json->valuestring;
                        if (strcmp(cmd, "save") == 0)
                        {
                            SharedLogData.save_sensor_log = 1;
                            printf("NET: 收到日志命令=save\r\n");
                        }
                        else if (strcmp(cmd, "clear") == 0)
                        {
                            SharedLogData.save_sensor_log = 3;
                            printf("NET: 收到日志命令=clear\r\n");
                        }
                        else if (strncmp(cmd, "interval:", 9) == 0)
                        {
                            int min = atoi(cmd + 9);
                            if (min < 1) min = 1;
                            if (min > 60) min = 60;
                            int sec = min * 60;
                            SharedThresholdData.log_interval_s = (uint16_t)sec;
                            SharedThresholdData.threshold_seq++;
                            snprintf(g_log_control_status, sizeof(g_log_control_status),
                                     "interval=%dmin", min);
                            printf("NET: 日志间隔设为 %d 分钟\r\n", min);
                            g_cloud_interval_pending = 1;  /* 通知 net_task 播报 TTS */
                        }
                    }

                    /* ---- 回复平台（id 可能是字符串或数字） ---- */
                    cJSON *id_item = cJSON_GetObjectItem(raw_json, "id");
                    char id_str[16] = {0};
                    if (id_item != NULL)
                    {
                        if (id_item->valuestring != NULL)
                            snprintf(id_str, sizeof(id_str), "%s", id_item->valuestring);
                        else
                            snprintf(id_str, sizeof(id_str), "%d", id_item->valueint);
                    }
                    if (id_str[0] != '\0')
                    {
                        snprintf(reply_topic, sizeof(reply_topic),
                                 "$sys/%s/%s/thing/property/set_reply", PROID, DEVICE_NAME);
                        snprintf(reply_msg, sizeof(reply_msg),
                                 "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}", id_str);
                        OneNET_Publish(reply_topic, reply_msg);
                        printf("NET: set_reply sent id=%s\r\n", id_str);
                    }

                    cJSON_Delete(raw_json);
                }
            }
            break;

        case MQTT_PKT_PUBACK:
            printf("PUBACK\r\n");
            break;

        case MQTT_PKT_SUBACK:
            printf("SUBACK\r\n");
            break;

        default:
            break;
    }

    ESP8266_Clear();

    if (type == MQTT_PKT_CMD || type == MQTT_PKT_PUBLISH)
    {
        if (cmdid_topic != NULL) MQTT_FreeBuffer(cmdid_topic);
        if (req_payload != NULL) MQTT_FreeBuffer(req_payload);
    }
}

//==========================================================
//	函数名称：	Net_Reconnect
//
//	函数功能：	网络断线重连
//
//	入口参数：	无
//
//	返回参数：	0-成功	1-失败
//
//	说明：		最多重试3次，每次间隔500ms
//==========================================================
_Bool Net_Reconnect(const char *ssid, const char *password)
{
	int try_count;

	/* 先尝试软重连（仅 TCP 断开重连，不复位 WiFi） */
	for (try_count = 0; try_count < 2; try_count++)
	{
		printf("NET: TCP 重连 (第%d次)\r\n", try_count + 1);
		ESP8266_Clear();
		/* 关闭旧连接，避免 "ALREADY CONNECTED" 假成功 */
		ESP8266_SendCmd("AT+CIPCLOSE=0\r\n", "OK");
		vTaskDelay(pdMS_TO_TICKS(500));
		ESP8266_Clear();
		if (ESP8266_SendCmd("AT+CIPSTART=0,\"TCP\",\"mqtts.heclouds.com\",1883\r\n", "CONNECT") == 0)
		{
			if (OneNet_DevLink() == 0)
			{
				OneNET_Subscribe();
				onet_connected = 1;
				printf("NET: TCP 重连成功\r\n");
				return 0;
			}
		}
		/* 第2次失败时硬件复位 ESP8266 */
		if (try_count == 1)
		{
			printf("NET: ESP8266 硬件复位\r\n");
			RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOE, ENABLE);
			GPIO_ResetBits(GPIOE, GPIO_Pin_5);
			vTaskDelay(pdMS_TO_TICKS(200));
			GPIO_SetBits(GPIOE, GPIO_Pin_5);
			vTaskDelay(pdMS_TO_TICKS(2000));
			ESP8266_Clear();
		}
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	/* TCP 重连失败，尝试完整重连（含 WiFi） */
	for (try_count = 0; try_count < 2; try_count++)
	{
		printf("NET: 完整重连 (第%d次)\r\n", try_count + 1);
		vTaskDelay(pdMS_TO_TICKS(1000));

		ESP8266_Init(ssid, password);

		if (OneNet_DevLink() == 0)
		{
			OneNET_Subscribe();
			onet_connected = 1;
			printf("NET: 完整重连成功\r\n");
			return 0;
		}
	}

	printf("WARN: 网络重连全部失败\r\n");
	return 1;
}

/*==== 设备控制（通过共享内存让 V3F 执行） ====*/

void Device_GPIO_Init(void)
{
    /* V5F 无需初始化，硬件由 V3F 控制 */
}

void Device_SetLed(_Bool on)
{
    g_led_state = on;
    SharedDeviceData.led = on ? 1 : 0;
    SharedDeviceData.device_seq++;
    printf("DEV: LED %s\r\n", on ? "ON" : "OFF");
}

void Device_SetBuzzer(_Bool on)
{
    g_buzzer_state = on;
    SharedDeviceData.buzzer = on ? 1 : 0;
    SharedDeviceData.device_seq++;
    printf("DEV: 蜂鸣器 %s\r\n", on ? "ON" : "OFF");
}

void Device_SetPump(_Bool on)
{
    g_pump_state = on;
    SharedDeviceData.pump = on ? 1 : 0;
    SharedDeviceData.device_seq++;
    printf("DEV: 水泵 %s\r\n", on ? "ON" : "OFF");
}

/*==== 阈值设置（云平台下发 → 写入共享内存供 V3F 读取） ====*/

void Threshold_SetTemp(uint16_t val)
{
    g_temp_threshold = val;
    SharedThresholdData.temp_threshold = val;
    SharedThresholdData.threshold_seq++;
    printf("THR: 温度下限 = %u°C\r\n", val);
}

void Threshold_SetHumi(uint16_t val)
{
    g_humi_threshold = val;
    SharedThresholdData.humi_threshold = val;
    SharedThresholdData.threshold_seq++;
    printf("THR: 湿度下限 = %u%%\r\n", val);
}

void Threshold_SetLight(uint16_t val)
{
    g_light_threshold = val;
    SharedThresholdData.light_threshold = val;
    SharedThresholdData.threshold_seq++;
    printf("THR: 光照下限 = %u lux\r\n", val);
}

uint16_t Threshold_GetTemp(void)  { return g_temp_threshold; }
uint16_t Threshold_GetHumi(void)  { return g_humi_threshold; }
uint16_t Threshold_GetLight(void) { return g_light_threshold; }

/*==== 病害检测（云平台上报） ====*/

void Pest_SetResult(const char *plant, const char *pest, uint8_t confidence)
{
    taskENTER_CRITICAL();
    if (plant) {
        strncpy(g_pest_plant, plant, sizeof(g_pest_plant) - 1);
        g_pest_plant[sizeof(g_pest_plant) - 1] = '\0';
    }
    if (pest) {
        strncpy(g_pest_pest, pest, sizeof(g_pest_pest) - 1);
        g_pest_pest[sizeof(g_pest_pest) - 1] = '\0';
    }
    g_pest_confidence = confidence;
    taskEXIT_CRITICAL();
}

/*==== 速度档位 ====*/

void Speed_SetGear(uint8_t gear)
{
    CarControl_SetSpeedGear(gear);
}

uint8_t Speed_GetGear(void)
{
    return CarControl_GetSpeedGear();
}

/*==== 连接状态管理（net_task 调用） ====*/
void OneNet_SetConnected(_Bool val)
{
    onet_connected = val ? 1 : 0;
}

_Bool OneNet_IsConnected(void)
{
    return onet_connected;
}



