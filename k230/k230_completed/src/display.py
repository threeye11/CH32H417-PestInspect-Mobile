"""
显示管理模块
============
封装 01Studio CanMV K230 的 Display 接口，支持 MIPI LCD / HDMI 显示。
提供 OSD 叠加层绘制功能（检测框、文字、信息面板等）。

用法:
    from src.display import DisplayManager
    disp = DisplayManager()
    disp.init()
    disp.show(img)
"""

import gc
from media.display import Display
from media.media import MediaManager


class DisplayManager:
    """显示管理器，负责屏幕初始化和图像渲染"""

    def __init__(self, config=None, display_config=None):
        """
        初始化显示管理器

        参数:
            config: dict, 完整配置（可选）
            display_config: dict, 显示配置（优先级高于 config）
        """
        if display_config is None:
            from config.settings import get_display_config
            display_config = get_display_config()

        self._config = display_config
        self._initialized = False

    def init(self):
        """
        初始化显示屏和 MediaManager

        根据配置自动选择 ST7701 (MIPI) 或 LT9611 (HDMI) 驱动
        """
        cfg = self._config
        display_mode = cfg.get("display_mode", "st7701")
        display_size = cfg.get("display_size", [800, 480])
        to_ide = cfg.get("to_ide", True)

        if display_mode == "st7701":
            # MIPI LCD (3.5寸 800x480 或 2.4寸 640x480)
            if display_size[0] == 640:
                Display.init(Display.ST7701, width=640, height=480, to_ide=to_ide)
            else:
                Display.init(Display.ST7701, to_ide=to_ide)
        elif display_mode == "hdmi":
            Display.init(Display.LT9611, to_ide=to_ide)
        else:
            # 仅 IDE 缓冲区
            Display.init(Display.VIRT, display_size[0], display_size[1], to_ide=to_ide)

        # 初始化 media 资源管理器（必须）
        MediaManager.init()

        self._initialized = True

    def show(self, img, x=None, y=None):
        """
        显示图像到屏幕

        参数:
            img: image 对象
            x, y: 显示偏移（居中显示时可省略）
        """
        if x is not None and y is not None:
            Display.show_image(img, x=x, y=y)
        else:
            Display.show_image(img)

    def show_centered(self, img, img_w, img_h):
        """居中显示图像"""
        size = self._config.get("display_size", [800, 480])
        x = (size[0] - img_w) // 2
        y = (size[1] - img_h) // 2
        Display.show_image(img, x=x, y=y)

    @property
    def width(self):
        """显示宽度"""
        return self._config.get("display_size", [800, 480])[0]

    @property
    def height(self):
        """显示高度"""
        return self._config.get("display_size", [800, 480])[1]

    @property
    def size(self):
        """显示分辨率 [width, height]"""
        return self._config.get("display_size", [800, 480])

    def deinit(self):
        """释放显示资源"""
        try:
            MediaManager.deinit()
            Display.deinit()
        except Exception:
            pass
        self._initialized = False
        gc.collect()


def draw_detection_box(img, x, y, w, h, label, color, thickness=4):
    """
    在图像上绘制检测框和标签

    参数:
        img: image 对象（OSD 图像）
        x, y, w, h: 检测框位置和尺寸
        label: 文字标签
        color: 颜色元组 (A, R, G, B) 或 (R, G, B)
        thickness: 线条粗细
    """
    img.draw_rectangle(x, y, w, h, color=color, thickness=thickness)
    if label:
        # 标签绘制在框上方
        img.draw_string_advanced(x, max(0, y - 32), 24, " " + label + " ", color=color)


def draw_info_panel(img, fps, detections, display_size, class_names_cn=None):
    """
    绘制信息面板（FPS、检测统计等）

    参数:
        img: OSD 图像
        fps: 当前帧率
        detections: 检测结果列表
        display_size: 显示分辨率 [w, h]
        class_names_cn: 中文名称映射（可选）
    """
    from config.settings import CLASS_NAMES_CN
    if class_names_cn is None:
        class_names_cn = CLASS_NAMES_CN

    # FPS 显示
    img.draw_string_advanced(10, 10, 24, " FPS: {:.1f} ".format(fps), color=(255, 0, 255, 0))

    # 检测数量
    count = len(detections) if detections else 0
    img.draw_string_advanced(10, 42, 24, " Detect: {} ".format(count), color=(255, 255, 255, 0))

    # 如果有检测结果，显示摘要
    if detections:
        # 统计各类别数量
        summary = {}
        for det in detections:
            name = det['class_name']
            summary[name] = summary.get(name, 0) + 1

        y_offset = 74
        for name, cnt in summary.items():
            cn_name = class_names_cn.get(name, name)
            text = " {}: {}".format(cn_name, cnt)
            img.draw_string_advanced(10, y_offset, 20, text, color=(255, 255, 255, 255))
            y_offset += 28
            if y_offset > display_size[1] - 40:
                break