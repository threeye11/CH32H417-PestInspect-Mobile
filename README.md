# 基于 CH32H417 的移动病虫害巡检平台

面向智慧农业的移动病虫害巡检小车：以 WCH **CH32H417 双核 RISC-V MCU** 为主控，搭配 **K230 RISC-V AI 视觉模块**（本地部署 YOLO11 模型），支持玉米、甘蔗、番茄三种作物的病虫害实时识别；本地 LVGL 触摸屏显示 + OneNET 云平台远程监控，环境参数超阈值时通过蜂鸣器 / LED / TTS 语音三重告警。

## 功能特性

- **多作物病虫害视觉检测**：K230 本地推理 YOLO11 模型（320×320 输入），覆盖玉米 11 类、甘蔗、番茄等病虫害类别，检测结果（框坐标、置信度、作物类型）经串口实时回传 CH32，帧率 10–20 FPS。
- **自主移动巡检**：TB6612FNG 驱动双层底盘，HC-SR04 超声波避障；上层 MG995 舵机云台支持自动追踪（人体检测偏移量算转向角）与手动微调。
- **本地 + 云端双端监控**：V3F 核运行 LVGL，2.8 寸触摸屏多页面显示；V5F 核经 ESP8266 接入 OneNET（MQTT），支持远程查看数据与下发控制指令，断网自动重连。
- **环境采集与阈值告警**：BH1750 光照 + SHT30 温湿度定时采集，超阈值触发蜂鸣器 + LED + TW-TTS 语音播报；内置 21 条本地病害知识库（病害名称、播报文案、推荐药物、严重等级）。
- **多模式运行**：自动巡检 / 手动遥控 / 病害检测 / 人体追踪，可通过触摸屏或云端网页切换。

## 系统架构

| 单元 | 硬件 | 职责 |
|------|------|------|
| 主控 | CH32H417QEU6（双核 RISC-V） | V5F 主核：WiFi / OneNET MQTT / 小车控制 / TTS / K230 通信；V3F 辅核：LVGL 界面 / RTC / 传感器采集 |
| 视觉 | K230（CanMV MicroPython） | YOLO11 病虫害检测、人体检测与云台跟踪 |
| 通信 | ESP8266（USART2） | WiFi 联网、MQTT 云平台接入、HTTP 天气/时间获取 |
| 传感 | SHT30 + BH1750（I2C，V3F） | 温湿度、光照采集，写入共享内存供上报 |
| 执行 | TB6612FNG + MG995 + 蜂鸣器/LED/水泵 | 底盘运动、云台、告警与浇灌 |
| 存储 | W25Q64 SPI Flash（V5F） | WiFi 凭据、触摸校准、天气地点、阈值与历史日志 |
| 语音 | TW-TTS 模块（USART3，V5F） | 检测结果播报、防治建议、时间/天气播报 |

双核通过 32KB 共享内存（`0x20178000`，`.shared_data` 段）零拷贝同步，主核以序列号（seq）感知数据更新。

### 目录结构

```
├── ch32/                          # CH32H417 双核固件（MounRiver Studio 工程）
│   ├── RTOS.wvsln                 # MRS 解决方案（FreeRTOS_V3F + FreeRTOS_V5F）
│   ├── Common/                    # 双核共用：CH32H417 外设库、共享内存定义、调试/看门狗
│   ├── V3F/                       # 辅助核固件：LVGL 界面、SHT30/BH1750、设备控制
│   └── V5F/                       # 主核固件：ESP8266/OneNET、小车、TTS、W25Q64、K230
└── k230/
    └── k230_completed/            # K230 视觉工程（CanMV MicroPython）
        ├── main.py                # 入口（MODE 切换检测/双模型等运行模式）
        ├── main_lvgl.py           # 主程序：检测 + LVGL + 串口/云台/搜索状态机
        ├── config/settings.py     # 显示/摄像头/模型/检测参数全局配置
        ├── src/                   # 摄像头、检测器、串口/以太网通信、舵机、状态机等
        ├── libs/                  # K230 官方 AI 库（YOLO/AIBase 等）
        └── *.kmodel               # YOLO11 检测模型（玉米/甘蔗/番茄/人体）
```

## 快速开始

### 1. CH32 双核固件

1. 安装 [MounRiver Studio](http://www.mounriver.com/)（含 `riscv-none-elf-gcc` 工具链），打开 `ch32/RTOS.wvsln`。
2. **先编译 FreeRTOS_V3F，再编译 FreeRTOS_V5F**——V5F 链接脚本依赖 `V3F/obj/User/shared.o`，顺序颠倒会链接失败。
3. 用 WCH-Link 分别烧录两核固件（V3F 与 V5F 需分开烧录）。

### 2. 云平台与 API 配置（必改）

| 配置项 | 文件 | 说明 |
|--------|------|------|
| OneNET 产品ID / 鉴权token / 设备名 | `ch32/V5F/NET/ONENET/onenet.h` | 修改 `PROID` / `AUTH_INFO` / `DEVICE_NAME`，在 [OneNET](https://open.iot.10086.cn/) 创建产品设备后获取 |
| 心知天气 API Key | `ch32/V5F/BSP/WEATHER/weather.h` | 修改 `WEATHER_API_KEY`，[心知天气](https://www.seniverse.com/) 免费申请 |
| WiFi | 设备触摸屏 WiFi 页面输入，或 OneNET 网页下发 `wifi_config`（格式 `ssid,password`） | 保存在 W25Q64，支持断电恢复 |
| K230 运行参数 | `k230/k230_completed/config/settings.py` | 显示模式、模型路径、置信度/NMS 阈值、以太网 IP 等 |

### 3. K230 视觉模块

1. 刷入 CanMV 固件（版本见 `k230/k230_completed/revision.txt`）。
2. 将各 `*.kmodel` 放到 SD 卡根目录（默认路径 `/sdcard/yolo11s_det_320.kmodel`，可在 `settings.py` 修改）。
3. 将 `k230_completed/` 下的 `.py` 工程文件拷入 SD 卡，用 CanMV IDE 运行 `main.py`，或配置开机自启。

K230 与 CH32 通过 USART5 通信（921600 波特率），支持启停、模式切换、作物切换、图像帧回传及人体追踪坐标（`dx/dy/th/L`）等帧协议指令。

## 主要性能指标

| 指标 | 数值 |
|------|------|
| 病虫害检测帧率 | 10–20 FPS（320×320 输入） |
| 检测置信度 | 普遍 ≥ 0.7 |
| 环境采集 | 温度 ±0.3°C，湿度 ±2%RH，光照 0–65535 lx |
| MQTT 上报延迟 | 约 200–500 ms，断网自动重连 |
| 超声波避障 | 测距 2–400 cm，响应约 100 ms |

## 注意事项

- 源码注释为中文，文件需保持 UTF-8 编码（TTS 中文播报依赖）。
- V5F 核 `vTaskDelay` 存在已知问题（SW_Handler 上下文切换），V5F 任务需使用 systick 忙等方式延时。
- K230 初始化需放在 OneNET 连接之后，避免 USART5 中断干扰 MQTT 建链。
- 仓库已去除个人云端凭证与 API Key，克隆后请按上文「云平台与 API 配置」填写自己的凭证。

