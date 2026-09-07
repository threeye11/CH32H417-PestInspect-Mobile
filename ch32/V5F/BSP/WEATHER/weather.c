#include "weather.h"
#include <stddef.h>
#include "cJSON.h"
#include "../../NET/ESP8266/esp8266.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Thread-safe queue function (defined in main.c) */
extern void Queue_WeatherUpdate(const char *city, const char *weather, int8_t temp);

weather_info_t g_weather;

/* ---------------------------------------------------------------
 * Weather_Fetch(location) — 获取天气，更新 g_weather 并同步到 LVGL
 *
 * 1. 通过 ESP8266_HTTP_Get 获取心知天气 JSON
 * 2. 剥离可能残留的 +IPD 前缀，定位 '{' 起点
 * 3. 解析 now.text（天气）和 now.temperature（温度）
 * 4. 调用 Queue_WeatherUpdate() 刷新 LVGL 界面
 * --------------------------------------------------------------- */
void Weather_Fetch(const char *location)
{
	char resp[512];
	char api_path[128];
	resp[0] = '\0';

	if(location == NULL || location[0] == '\0')
	{
		printf("Weather: 无地点信息，跳过\r\n");
		g_weather.valid = 0;
		return;
	}

	snprintf(api_path, sizeof(api_path),
			 "/v3/weather/now.json?key=%s&location=%s&language=zh-Hans&unit=c",
			 WEATHER_API_KEY, location);

	printf("Weather: fetching [%s]...\r\n", location);
	if(ESP8266_HTTP_Get(WEATHER_API_HOST, api_path, resp, sizeof(resp)))
	{
		printf("Weather: HTTP failed\r\n");
		g_weather.valid = 0;
		return;
	}

	/* Safety: strip any residual +IPD prefix by locating '{' */
	char *json_start = strchr(resp, '{');
	if(json_start == NULL)
	{
		printf("Weather: no JSON\r\n");
		g_weather.valid = 0;
		return;
	}

	cJSON *root = cJSON_Parse(json_start);
	if(root == NULL)
	{
		printf("Weather: JSON parse failed\r\n");
		g_weather.valid = 0;
		return;
	}

	/* Navigate: root -> results[0] -> now */
	cJSON *results = cJSON_GetObjectItem(root, "results");
	cJSON *item0   = results ? cJSON_GetArrayItem(results, 0) : NULL;
	cJSON *now     = item0   ? cJSON_GetObjectItem(item0, "now") : NULL;

	if(now == NULL)
	{
		cJSON_Delete(root);
		g_weather.valid = 0;
		return;
	}

	cJSON *text = cJSON_GetObjectItem(now, "text");
	cJSON *temp = cJSON_GetObjectItem(now, "temperature");

	const char *weather_text = (text && text->valuestring) ? text->valuestring : "--";
	const char *temp_str     = (temp && temp->valuestring) ? temp->valuestring : "0";
	int8_t temp_val          = (int8_t)atoi(temp_str);

	/* 从 API 响应中解析中文城市名（results[0].location.name） */
	const char *city_name = location;  /* 默认用输入值兜底 */
	cJSON *loc_obj = item0 ? cJSON_GetObjectItem(item0, "location") : NULL;
	if (loc_obj)
	{
		cJSON *name_obj = cJSON_GetObjectItem(loc_obj, "name");
		if (name_obj && name_obj->valuestring && name_obj->valuestring[0] != '\0')
			city_name = name_obj->valuestring;
	}

	/* Update global struct */
	strncpy(g_weather.weather, weather_text, sizeof(g_weather.weather) - 1);
	g_weather.weather[sizeof(g_weather.weather) - 1] = '\0';
	snprintf(g_weather.temp, sizeof(g_weather.temp), "%d", temp_val);
	strncpy(g_weather.city, city_name, sizeof(g_weather.city) - 1);
	g_weather.city[sizeof(g_weather.city) - 1] = '\0';
	g_weather.valid = 1;

	printf("[WEATHER] text=");
	for (int i = 0; weather_text[i] && i < 16; i++)
		printf(" %02X", (unsigned char)weather_text[i]);
	printf("\r\n[WEATHER] city=");
	for (int i = 0; g_weather.city[i] && i < 16; i++)
		printf(" %02X", (unsigned char)g_weather.city[i]);
	printf("\r\n");

	/* Push to LVGL UI via queue (thread-safe) */
	Queue_WeatherUpdate(location, weather_text, temp_val);

	cJSON_Delete(root);
}
