"""
玉米病虫害识别系统 - 源码包
模块:
    camera   - 摄像头管理
    display  - 显示管理
    detector - YOLO11 玉米病虫害检测器
"""

from src.camera import CameraManager
from src.display import DisplayManager
from src.detector import PestDetector

__all__ = [
    "CameraManager",
    "DisplayManager",
    "PestDetector",
]
