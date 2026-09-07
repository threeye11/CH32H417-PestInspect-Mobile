# Auto Tracking Engine
# Integrates state machine + PID + servo + AI detection into main loop.
#
# Usage in main loop:
#   from src.hardware.tracking import TrackingEngine
#   engine = TrackingEngine(img_w=640, img_h=360)
#   while True:
#       img = pl.get_frame()
#       engine.tick(img, yolo_model_or_person_model, target_mode)

from src.hardware.state_machine import get_state_machine, MODE_AUTO
from src.hardware.pid import GimbalPID, create_gimbal_pids
from src.hardware.servo import servo_write, servo_home, YAW_MIN, YAW_MAX, PITCH_MIN, PITCH_MAX
import ulab.numpy as np

# ==================== Constants ====================
LOST_THRESHOLD = 60       # frames before slow homing kicks in (~2s)
HOMING_SPEED = 0.5        # deg per frame during lost protection


class TrackingEngine:
    """
    Main loop auto-tracking engine.

    Call tick() each frame. Handles:
    - Mode gating (only tracks when AUTO + detect ON)
    - Target detection parsing (person or pest)
    - PID update -> servo write
    - Lost target protection (slow homing)

    Args:
        img_w: Image width for PID target calculation
        img_h: Image height for PID target calculation
        kp, ki, kd: PID gains
        dead_zone: PID dead zone (pixels)
        max_output: PID max output per frame (degrees)
    """

    def __init__(self, img_w=640, img_h=360, kp=0.01, ki=0.0, kd=0.001,
                 dead_zone=10, max_output=8.0):
        self.img_w = img_w
        self.img_h = img_h

        # Create dual-axis PID
        self.yaw_pid, self.pitch_pid = create_gimbal_pids(
            img_w=img_w, img_h=img_h,
            kp=kp, ki=ki, kd=kd,
            dead_zone=dead_zone, max_output=max_output
        )

        self.sm = get_state_machine()

        # Current servo angles (mirrored from state machine)
        self.yaw = 90.0
        self.pitch = 90.0

        # Debug output
        self.debug_enabled = False
        self.frame_count = 0

    def tick(self, img, model, target_mode):
        """
        Process one frame. Call every frame in the main loop.

        Args:
            img: Current camera frame
            model: AI model object (PersonDetectApp or YOLO11-like)
            target_mode: "BODY" or "PEST"

        Returns:
            dict or None: {"target_x", "target_y", "found", "lost_count"}
        """
        self.frame_count += 1
        result = {"found": False, "target_x": 0, "target_y": 0,
                  "lost_count": self.sm.lost_counter}

        # Gate: only track when AUTO + detect ON
        if self.sm.work_mode != MODE_AUTO or not self.sm.detect_switch:
            return result

        # Run detection
        target_x, target_y, found = self._detect(model, target_mode, img)

        if found:
            # Target acquired: reset lost counter, run PID
            self.sm.lost_reset()
            self._pid_track(target_x, target_y)
            result["found"] = True
            result["target_x"] = target_x
            result["target_y"] = target_y
        else:
            # Target lost
            self.sm.lost_tick()
            result["lost_count"] = self.sm.lost_counter

            if self.sm.is_lost(LOST_THRESHOLD):
                # Lost protection: slow homing back to center
                self._slow_home()

        result["lost_count"] = self.sm.lost_counter

        # Debug output
        if self.debug_enabled and self.frame_count % 10 == 0:
            self._print_debug(target_x if found else -1,
                              target_y if found else -1, found)

        return result

    def _detect(self, model, target_mode, img):
        """
        Run model inference and extract target center.

        Returns:
            (target_x, target_y, found)
        """
        try:
            if target_mode == "BODY":
                return self._detect_person(model, img)
            elif target_mode == "PEST":
                return self._detect_pest(model, img)
        except Exception as e:
            print("[TRACK] Detect error:", e)
        return (0, 0, False)

    def _detect_person(self, model, img):
        """Parse person detection output: [class_id, score, x1, y1, x2, y2]"""
        dets = model.run(img)
        if not dets:
            return (0, 0, False)
        # Take largest detection
        best = None
        best_area = 0
        for d in dets:
            x1, y1, x2, y2 = int(d[2]), int(d[3]), int(d[4]), int(d[5])
            area = (x2 - x1) * (y2 - y1)
            if area > best_area:
                best_area = area
                best = (x1, y1, x2, y2)
        if best is None:
            return (0, 0, False)
        x1, y1, x2, y2 = best
        return ((x1 + x2) // 2, (y1 + y2) // 2, True)

    def _detect_pest(self, model, img):
        """Parse YOLO11 output via raw tensor -> merged detections."""
        model.run(img)
        raw = model.results[0]
        out = raw.reshape((raw.shape[0] * raw.shape[1], raw.shape[2]))
        out = out.transpose()
        boxes = out[:, 0:4]
        scores = out[:, 4:]
        confs = np.max(scores, axis=-1)
        cls_ids = np.argmax(scores, axis=-1)

        xf = float(self.img_w) / model.model_input_size[0]
        yf = float(self.img_h) / model.model_input_size[1]

        best = None
        best_area = 0
        for i in range(len(boxes)):
            if confs[i] <= model.conf_thresh:
                continue
            cid = int(cls_ids[i])
            # Skip excluded classes
            if hasattr(model, '_exclude') and cid in model._exclude:
                continue
            cx = float(boxes[i, 0]); cy = float(boxes[i, 1])
            w = float(boxes[i, 2]); h = float(boxes[i, 3])
            x1 = max(0, int((cx - 0.5 * w) * xf))
            y1 = max(0, int((cy - 0.5 * h) * yf))
            x2 = min(self.img_w, int((cx + 0.5 * w) * xf))
            y2 = min(self.img_h, int((cy + 0.5 * h) * yf))
            area = (x2 - x1) * (y2 - y1)
            if area > best_area:
                best_area = area
                best = (x1, y1, x2, y2)

        if best is None:
            return (0, 0, False)
        x1, y1, x2, y2 = best
        return ((x1 + x2) // 2, (y1 + y2) // 2, True)

    def _pid_track(self, target_x, target_y):
        """Run PID on both axes and update servos."""
        dyaw = self.yaw_pid.update(float(target_x))
        dpitch = self.pitch_pid.update(float(target_y))

        self.yaw = max(YAW_MIN, min(YAW_MAX, self.yaw + dyaw))
        self.pitch = max(PITCH_MIN, min(PITCH_MAX, self.pitch - dpitch))

        servo_write(self.pitch, self.yaw)

        # Sync to state machine
        self.sm.yaw_angle = self.yaw
        self.sm.pitch_angle = self.pitch

    def _slow_home(self):
        """Lost protection: slowly move servos back toward center (90, 90)."""
        moved = False
        if abs(self.yaw - 90.0) > 0.5:
            if self.yaw > 90.0:
                self.yaw = max(90.0, self.yaw - HOMING_SPEED)
            else:
                self.yaw = min(90.0, self.yaw + HOMING_SPEED)
            moved = True
        if abs(self.pitch - 90.0) > 0.5:
            if self.pitch > 90.0:
                self.pitch = max(90.0, self.pitch - HOMING_SPEED)
            else:
                self.pitch = min(90.0, self.pitch + HOMING_SPEED)
            moved = True
        if moved:
            servo_write(self.pitch, self.yaw)
            self.sm.yaw_angle = self.yaw
            self.sm.pitch_angle = self.pitch

    def _print_debug(self, tx, ty, found):
        """Print tracking debug info every 10 frames."""
        tag = "LOCK" if found else "LOST"
        print("[TRACK] %s x=%4d y=%4d | yaw=%.1f pitch=%.1f | lost=%d | PID: %s %s" %
              (tag, tx, ty, self.yaw, self.pitch, self.sm.lost_counter,
               self.yaw_pid.state_str, self.pitch_pid.state_str))

    def reset(self):
        """Reset engine state."""
        self.yaw_pid.reset()
        self.pitch_pid.reset()
        self.yaw = 90.0
        self.pitch = 90.0
        self.frame_count = 0