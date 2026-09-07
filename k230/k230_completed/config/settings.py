"""
玉米病虫害识别系统 - 全局配置
==================================
适配平台: 01Studio CanMV K230 + 3.5寸/2.4寸 MIPI LCD
模型框架: YOLO11 (nncase)

使用说明:
  - 修改此文件即可调整系统行为，无需改动业务代码
  - 所有模块统一从本文件读取配置
  - 支持通过 load_override() 加载外部覆盖配置
"""

# ============================================================
# 显示配置
# ============================================================
DISPLAY = {
    # 显示模式: "lcd3_5" (3.5寸800x480) | "lcd2_4" (2.4寸640x480) | "hdmi" (1920x1080)
    "mode": "lcd3_5",

    # 各模式参数（自动根据 mode 选择，无需手动修改）
    "profiles": {
        "lcd3_5": {"driver": "st7701", "width": 800, "height": 480},
        "lcd2_4": {"driver": "st7701", "width": 640, "height": 480},
        "hdmi":   {"driver": "hdmi",   "width": 1920, "height": 1080},
    },

    # 是否同时输出到 IDE 缓冲区（CanMV IDE 实时预览）
    "to_ide": True,
}


# ============================================================
# 摄像头配置
# ============================================================
CAMERA = {
    # 传感器分辨率（物理采集分辨率）
    "sensor_width": 1280,
    "sensor_height": 960,

    # 帧大小（显示用）
    "frame_width": 800,
    "frame_height": 480,

    # 像素格式
    "pixformat": "RGB565",

    # 自动增益 / 自动白平衡（检测场景建议关闭以保持一致性）
    "auto_gain": False,
    "auto_whitebal": False,

    # 初始化后跳帧数
    "skip_frames": 30,
}


# ============================================================
# 模型配置 (玉米病虫害 YOLO11)
# ============================================================
MODEL = {
    # kmodel 文件路径（SD卡上的路径）
    "kmodel_path": "/sdcard/yolo11s_det_320.kmodel",

    # 模型输入尺寸
    "input_size": [320, 320],

    # AI 输入图像尺寸（sensor 输出给 AI 的分辨率）
    # 与 input_size 一致，PipeLine 会自动 resize
    "rgb888p_size": [640, 360],

    # 检测类别数
    "num_classes": 11,
}


# ============================================================
# 检测参数
# ============================================================
DETECTION = {
    # 置信度阈值
    "confidence_threshold": 0.6,

    # NMS 阈值（越大越宽松）
    "nms_threshold": 0.45,

    # 最大检测框数量
    "max_boxes_num": 50,
}


# ============================================================
# 类别定义 (玉米病虫害 11类)
# ============================================================
CLASS_NAMES = [
    "Abiotics_diseases_d",         # 0  - 非生物疾病
    "Aphids_p",                    # 1  - 蚜虫
    "curvulariosis_d",             # 2  - 弯孢菌叶斑病
    "dirt",                        # 3  - 泥土（不在LCD显示）
    "helminthosporiosis_d",        # 4  - 大斑病
    "Helthy_leaf",                 # 5  - 健康叶片
    "Rust_d",                      # 6  - 锈病
    "Spodoptera_frugiperda_a",     # 7  - 草地贪夜蛾(成虫)
    "Spodoptera_frugiperda_P",     # 8  - 草地贪夜蛾(幼虫)
    "stripe_d",                    # 9  - 纹枯病
    "Weeds",                       # 10 - 杂草（不在LCD显示）
]

# 不在LCD检测框中显示的类别
EXCLUDE_CLASSES = ["Weeds", "dirt"]

# 中文名称映射（用于显示）
CLASS_NAMES_CN = {
    "Abiotics_diseases_d":         "非生物疾病",
    "Aphids_p":                    "蚜虫",
    "curvulariosis_d":             "弯孢菌叶斑病",
    "dirt":                        "泥土",
    "helminthosporiosis_d":        "大斑病",
    "Helthy_leaf":                 "健康叶片",
    "Rust_d":                      "锈病",
    "Spodoptera_frugiperda_a":     "草地贪夜蛾(成虫)",
    "Spodoptera_frugiperda_P":     "草地贪夜蛾(幼虫)",
    "stripe_d":                    "纹枯病",
    "Weeds":                       "杂草",
}


# ============================================================
# 颜色配置 (ARGB8888)
# 格式: (Alpha, Red, Green, Blue)
# ============================================================
COLORS = [
    (255, 255, 0, 0),       # 0  红色   - 非生物疾病
    (255, 0, 200, 0),       # 1  亮绿   - 蚜虫
    (255, 255, 165, 0),     # 2  橙色   - 弯孢菌叶斑病
    (255, 139, 69, 19),     # 3  棕色   - 泥土
    (255, 0, 100, 0),       # 4  深绿   - 大斑病
    (255, 0, 255, 0),       # 5  绿色   - 健康叶片
    (255, 200, 0, 0),       # 6  深红   - 锈病
    (255, 0, 0, 255),       # 7  蓝色   - 草地贪夜蛾(成虫)
    (255, 139, 0, 139),     # 8  紫色   - 草地贪夜蛾(幼虫)
    (255, 255, 105, 180),   # 9  粉色   - 纹枯病
    (255, 128, 128, 128),   # 10 灰色   - 杂草
]


# ============================================================
# 系统配置
# ============================================================
SYSTEM = {
    # 是否打印 FPS
    "show_fps": True,

    # 是否打印检测结果到串口
    "print_detections": True,

    # GC 回收间隔（每 N 帧执行一次 gc.collect）
    "gc_interval": 1,

    # 调试级别: 0=关闭, 1=基本, 2=详细
    "debug_level": 0,
}


# ============================================================
# 工具函数
# ============================================================
def get_display_config():
    """获取当前显示模式的配置参数"""
    mode = DISPLAY["mode"]
    profile = DISPLAY["profiles"].get(mode)
    if profile is None:
        raise ValueError("不支持的显示模式: {}".format(mode))
    return {
        "display_mode": profile["driver"],
        "display_size": [profile["width"], profile["height"]],
        "to_ide": DISPLAY["to_ide"],
    }


def get_display_size():
    """获取当前显示分辨率 [width, height]"""
    cfg = get_display_config()
    return cfg["display_size"]


def load_override(override_dict):
    """
    加载外部覆盖配置（用于运行时动态调整）

    参数:
        override_dict: dict, 键为模块名（大写），值为要覆盖的字段字典
    示例:
        config.load_override({
            "DETECTION": {"confidence_threshold": 0.3},
            "DISPLAY": {"mode": "hdmi"},
        })
    """
    import sys
    module = sys.modules[__name__]
    for key, values in override_dict.items():
        target = getattr(module, key, None)
        if isinstance(target, dict) and isinstance(values, dict):
            target.update(values)
        else:
            setattr(module, key, values)

# ============================================================
# 以太网点对点通信配置
# ============================================================
ETH = {
    # K230 作为 TCP Server
    "server_ip": "192.168.1.100",
    "server_port": 5000,
    "subnet_mask": "255.255.255.0",
    "gateway": "192.168.1.1",

    # CH32 客户端 IP (用于调试日志)
    "client_ip": "192.168.1.10",

    # 是否自动启动以太网 (True=main_eth.py 自动初始化)
    "auto_start": True,

    # 心跳间隔 (秒, 0=禁用)
    "heartbeat_interval": 5,
}
