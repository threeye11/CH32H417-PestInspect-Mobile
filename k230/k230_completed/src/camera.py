"""
摄像头管理模块
==============
封装 01Studio CanMV K230 的 Sensor 接口，提供统一的摄像头初始化和控制。

用法:
    from src.camera import CameraManager
    cam = CameraManager()
    cam.init()
    img = cam.snapshot()
"""

import gc
import sys

# K230 MicroPython API
from media.sensor import Sensor
from media.display import Display
from media.media import MediaManager


class CameraManager:
    """摄像头管理器，负责传感器初始化、参数配置和图像采集"""

    def __init__(self, config=None):
        """
        初始化摄像头管理器

        参数:
            config: dict, 摄像头配置（默认从 settings 读取）
        """
        if config is None:
            from config.settings import CAMERA
            config = CAMERA

        self._config = config
        self._sensor = None
        self._initialized = False

    def init(self):
        """
        初始化摄像头传感器

        流程: 构建对象 → 复位 → 设置帧大小 → 设置像素格式 → 启动

        返回:
            self, 支持链式调用
        """
        cfg = self._config

        # 构建传感器对象
        self._sensor = Sensor(
            width=cfg.get("sensor_width", 1280),
            height=cfg.get("sensor_height", 960)
        )

        # 复位和初始化
        self._sensor.reset()

        # 设置帧大小（显示用分辨率）
        self._sensor.set_framesize(
            width=cfg.get("frame_width", 800),
            height=cfg.get("frame_height", 480)
        )

        # 设置像素格式
        self._sensor.set_pixformat(Sensor.RGB565)

        self._initialized = True
        return self

    def run(self):
        """启动传感器采集（必须在 Display 和 MediaManager 初始化之后调用）"""
        if self._sensor is None:
            raise RuntimeError("摄像头未初始化，请先调用 init()")
        self._sensor.run()

    def snapshot(self):
        """
        拍摄一帧图像

        返回:
            image 对象
        """
        if self._sensor is None:
            raise RuntimeError("摄像头未初始化")
        return self._sensor.snapshot()

    def skip_frames(self, n=None):
        """跳过指定帧数（用于传感器稳定）"""
        if n is None:
            n = self._config.get("skip_frames", 30)
        if self._sensor:
            self._sensor.skip_frames(n)

    @property
    def sensor(self):
        """获取底层 Sensor 对象（高级用法）"""
        return self._sensor

    @property
    def width(self):
        """获取帧宽度"""
        return self._config.get("frame_width", 800)

    @property
    def height(self):
        """获取帧高度"""
        return self._config.get("frame_height", 480)

    def deinit(self):
        """释放摄像头资源"""
        if self._sensor:
            try:
                self._sensor.stop()
            except Exception:
                pass
            self._sensor = None
        self._initialized = False
        gc.collect()