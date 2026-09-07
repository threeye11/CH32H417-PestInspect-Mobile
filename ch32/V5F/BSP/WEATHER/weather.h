#ifndef _WEATHER_H_
#define _WEATHER_H_

/* 天气信息结构体，供 LVGL 等上层模块直接读取 */
typedef struct
{
	char weather[16];	/* 天气现象文字，如 "多云"、"晴" */
	char temp[8];		/* 温度字符串（含 °C），如 "25°C" */
	char city[16];		/* 城市名称 */
	char update[32];	/* 数据更新时间 */
	_Bool valid;		/* 数据是否有效（成功获取为 1） */
} weather_info_t;

/* 全局天气实例，LVGL 直接读取此变量即可 */
extern weather_info_t g_weather;

/* 心知天气 API（使用前请自行填写，免费申请: https://www.seniverse.com） */
#define WEATHER_API_HOST	"api.seniverse.com"
#define WEATHER_API_KEY		"your_seniverse_api_key"

void Weather_Fetch(const char *location);

#endif
