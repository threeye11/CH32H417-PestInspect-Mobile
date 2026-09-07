"""
玉米病虫害检测模块 (YOLO11)
===========================
基于 01Studio libs.YOLO.YOLO11 的玉米病虫害检测器。
使用 YOLO11 模型进行推理，支持 Weeds/dirt 类别过滤。

用法:
    from src.detector import PestDetector
    detector = PestDetector()
    detector.start()
    results = detector.detect()
"""

import gc
import sys
import time

from libs.YOLO import YOLO11


class PestDetector:
    """
    玉米病虫害检测器（高层封装）
    整合 PipeLine + YOLO11，提供简洁的初始化和推理接口。
    过滤 Weeds 和 dirt 类别（不在LCD显示检测框）。

    用法:
        detector = PestDetector(display_mode="lcd3_5")
        detector.start()
        while True:
            results = detector.detect()
            detector.show()
    """

    def __init__(self, display_mode=None, config=None):
        from config.settings import (
            MODEL, DETECTION, CLASS_NAMES, EXCLUDE_CLASSES, COLORS,
            CAMERA, SYSTEM, get_display_config, get_display_size
        )

        self._model_cfg = MODEL
        self._det_cfg = DETECTION
        self._cam_cfg = CAMERA
        self._sys_cfg = SYSTEM

        if display_mode:
            from config.settings import DISPLAY
            DISPLAY["mode"] = display_mode
        self._disp_cfg = get_display_config()
        self._display_size = get_display_size()

        self._class_names = CLASS_NAMES
        self._exclude_classes = tuple(EXCLUDE_CLASSES)
        self._colors = COLORS

        self._pipeline = None
        self._yolo = None
        self._clock = None
        self._started = False

    def _build_labels(self):
        """构建 YOLO11 需要的 labels 字典 {index: name}"""
        labels = {}
        for i in range(len(self._class_names)):
            labels[i] = self._class_names[i]
        return labels

    def start(self):
        """启动检测系统"""
        from libs.PipeLine import PipeLine
        from media.sensor import Sensor

        rgb888p_size = self._model_cfg["rgb888p_size"]
        display_size = self._display_size
        display_mode = self._disp_cfg["display_mode"]

        self._pipeline = PipeLine(
            rgb888p_size=[640, 360],
            display_size=display_size,
            display_mode=display_mode
        )

        if display_size[0] <= 640:
            sensor = Sensor(width=1280, height=960)
        else:
            sensor = Sensor(width=1920, height=1080)

        self._pipeline.create(sensor=sensor)

        self._yolo = YOLO11(
            task_type="detect",
            mode="video",
            kmodel_path=self._model_cfg["kmodel_path"],
            labels=self._build_labels(),
            rgb888p_size=[640, 360],
            model_input_size=self._model_cfg["input_size"],
            display_size=display_size,
            conf_thresh=self._det_cfg["confidence_threshold"],
            nms_thresh=self._det_cfg["nms_threshold"],
            max_boxes_num=self._det_cfg["max_boxes_num"],
            debug_mode=self._sys_cfg["debug_level"],
        )
        self._yolo.config_preprocess()

        self._clock = time.clock()
        self._started = True

    def detect(self):
        """
        执行一次检测。
        返回:
            list[dict]: 检测结果列表（过滤 Weeds/dirt）
        """
        if not self._started:
            raise RuntimeError("检测器未启动，请先调用 start()")

        self._clock.tick()

        img = self._pipeline.get_frame()
        raw_dets = self._yolo.run(img)
        results = self.parse_results(raw_dets)

        self._yolo.draw_result(raw_dets, self._pipeline.osd_img)

        if self._sys_cfg["gc_interval"] <= 1:
            gc.collect()

        return results

    def parse_results(self, raw_dets):
        """
        解析 YOLO11 检测结果，过滤 Weeds/dirt 类别。
        YOLO11 格式: [[bbox, score, class_id], ...]

        返回:
            list[dict]: [{class_name, bbox, score}, ...]
        """
        results = []
        if not raw_dets:
            return results

        for det in raw_dets:
            if len(det) >= 3:
                bbox = det[0]
                score = det[1]
                cls_id = int(det[2])
            else:
                continue

            if cls_id < 0 or cls_id >= len(self._class_names):
                continue

            class_name = self._class_names[cls_id]

            # 过滤 Weeds 和 dirt（不在LCD显示检测框）
            if class_name in self._exclude_classes:
                continue

            results.append({
                "class_name": class_name,
                "bbox": bbox,
                "score": score,
            })

        return results

    def show(self):
        """将当前帧显示到屏幕"""
        self._pipeline.show_image()

    @property
    def fps(self):
        if self._clock:
            return self._clock.fps()
        return 0.0

    def stop(self):
        """停止检测系统，释放资源"""
        if self._yolo:
            self._yolo.deinit()
        if self._pipeline:
            self._pipeline.destroy()
        self._started = False
        gc.collect()
