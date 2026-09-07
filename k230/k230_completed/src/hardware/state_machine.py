# Global State Machine & Command Parser
# K230 2-Axis Gimbal Control System
#
# Commands (ASCII from PC serial / binary mapped from CH32):
#   S - Start detection    T - Stop detection
#   H - Servo home         A/D - Servo left/right 15 deg
#   W/Z - Servo up/down 15 deg
#   O - Toggle MANUAL/AUTO mode
#   M - Toggle UART data send
#   P - Switch to PEST mode  R - Switch to BODY mode
#   C - Cycle crop type
#
# Binary -> ASCII mapping (CH32 sends these):
#   0x01->S  0x02->T  0x03->M  0x04->C  0x05->P  0x06->R
#   0x07->H  0x08->A  0x09->D  0x0A->Z  0x0B->W  0x0C->O

from src.hardware.servo import servo_write, servo_home, YAW_MIN, YAW_MAX, PITCH_MIN, PITCH_MAX, YAW_HOME, PITCH_HOME
import time

# ==================== Mode Constants ====================
MODE_MANUAL = "MANUAL"
MODE_AUTO = "AUTO"

TARGET_FACE = "FACE"
TARGET_BODY = "BODY"
TARGET_PEST = "PEST"

# ==================== System State Constants ====================
STATE_TRACKING = 0
STATE_SEARCHING = 1
STATE_IDLE = 2

# ==================== Binary -> ASCII Mapping ====================
_BIN_TO_ASCII = {
    0x01: "S", 0x02: "T", 0x03: "M", 0x04: "C",
    0x05: "P", 0x06: "R", 0x07: "H", 0x08: "A",
    0x09: "D", 0x0A: "Z", 0x0B: "W", 0x0C: "O",
    0x0D: "1", 0x0E: "2", 0x0F: "3",
}


class StateMachine:
    """
    Global state machine for 2-axis gimbal control.

    State variables:
      work_mode   : MODE_MANUAL | MODE_AUTO
      detect_switch: True (ON) | False (OFF)
      target_mode : TARGET_FACE | TARGET_BODY | TARGET_PEST
      lost_counter: int, target lost protection counter
      uart_tx_on  : bool, toggle debug data send
      crop_mode   : int, crop type index
      yaw_angle   : float, current yaw angle
      pitch_angle : float, current pitch angle
    """

    def __init__(self):
        self.work_mode = MODE_MANUAL
        self.detect_switch = False
        self.target_mode = TARGET_BODY
        self.lost_counter = 0
        self.uart_tx_on = True
        self.crop_mode = 0
        self.system_state = STATE_TRACKING
        # Manual mode angles
        self.yaw_angle = float(YAW_HOME)
        self.pitch_angle = float(PITCH_HOME)
        # Auto mode angles (independent)
        self.auto_yaw = float(YAW_HOME)
        self.auto_pitch = float(PITCH_HOME)
        self._step = 15.0  # manual step degrees
        # Auto tracking PID state
        self._integral_pitch = 0.0
        self._prev_pitch = float(PITCH_HOME)

    # ==================== Status Echo ====================
    def _echo(self, action):
        """Print current system state after any command."""
        det = "ON" if self.detect_switch else "OFF"
        if self.work_mode == MODE_AUTO:
            print("[SYS] %s | mode=%s detect=%s target=%s lost=%d "
                  "yaw=%.0f pitch=%.0f crop=%d tx=%s" %
                  (action, self.work_mode, det, self.target_mode,
                   self.lost_counter, self.auto_yaw, self.auto_pitch,
                   self.crop_mode, "ON" if self.uart_tx_on else "OFF"))
        else:
            print("[SYS] %s | mode=%s detect=%s target=%s lost=%d "
                  "yaw=%.0f pitch=%.0f crop=%d tx=%s" %
                  (action, self.work_mode, det, self.target_mode,
                   self.lost_counter, self.yaw_angle, self.pitch_angle,
                   self.crop_mode, "ON" if self.uart_tx_on else "OFF"))

    # ==================== Command Dispatch ====================
    def handle_command(self, cmd):
        """
        Process one command. cmd can be:
          - ASCII char: 'S','T','H','A','D','W','Z','O','M','P','R','C'
          - Binary int: 0x01~0x0C (auto-mapped to ASCII)
          - Binary int: 0x10=Search, 0x11=Abort
        Returns True if command was recognized.
        """
        # New binary commands for search (0x10-0x11)
        if cmd == 0x10:
            return True  # recognized; orchestration handled by main_lvgl
        if cmd == 0x11:
            self._cmd_abort()
            return True

        # Map binary to ASCII
        if isinstance(cmd, int) and cmd in _BIN_TO_ASCII:
            cmd = _BIN_TO_ASCII[cmd]

        if not isinstance(cmd, str) or len(cmd) != 1:
            return False

        c = cmd.upper()
        if c == "S":
            self._cmd_start()
        elif c == "T":
            self._cmd_stop()
        elif c == "H":
            self._cmd_home()
        elif c == "A":
            self._cmd_left()
        elif c == "D":
            self._cmd_right()
        elif c == "W":
            self._cmd_up()
        elif c == "Z":
            self._cmd_down()
        elif c == "O":
            self._cmd_toggle_mode()
        elif c == "M":
            self._cmd_toggle_tx()
        elif c == "P":
            self._cmd_pest_mode()
        elif c == "R":
            self._cmd_body_mode()
        elif c == "C":
            self._cmd_cycle_crop()
        else:
            return False
        return True

    # ==================== S: Start Detection ====================
    def _cmd_start(self):
        self.detect_switch = True
        self.lost_counter = 0
        self._echo("Detection START")

    # ==================== T: Stop Detection ====================
    def _cmd_stop(self):
        self.detect_switch = False
        self.lost_counter = 0
        self._echo("Detection STOP (PID frozen, servos hold)")

    # ==================== H: Servo Home ====================
    def _cmd_home(self):
        if self.work_mode == MODE_AUTO:
            print("[SYS] Manual control LOCKED in AUTO mode")
            return
        servo_home()
        self.yaw_angle = float(YAW_HOME)
        self.pitch_angle = float(PITCH_HOME)
        self._echo("Servo HOME")

    # ==================== A/D/W/Z: Manual Direction ====================
    def _cmd_left(self):
        if self.work_mode == MODE_AUTO:
            print("[SYS] Manual control LOCKED in AUTO mode")
            return
        self.yaw_angle = max(YAW_MIN, self.yaw_angle + self._step)
        servo_write(self.pitch_angle, self.yaw_angle)
        self._echo("Servo LEFT (-%.0f)" % self._step)

    def _cmd_right(self):
        if self.work_mode == MODE_AUTO:
            print("[SYS] Manual control LOCKED in AUTO mode")
            return
        self.yaw_angle = min(YAW_MAX, self.yaw_angle - self._step)
        servo_write(self.pitch_angle, self.yaw_angle)
        self._echo("Servo RIGHT (+%.0f)" % self._step)

    def _cmd_up(self):
        if self.work_mode == MODE_AUTO:
            print("[SYS] Manual control LOCKED in AUTO mode")
            return
        self.pitch_angle = min(PITCH_MAX, self.pitch_angle + self._step)
        servo_write(self.pitch_angle, self.yaw_angle)
        self._echo("Servo UP (+%.0f)" % self._step)

    def _cmd_down(self):
        if self.work_mode == MODE_AUTO:
            print("[SYS] Manual control LOCKED in AUTO mode")
            return
        self.pitch_angle = max(PITCH_MIN, self.pitch_angle - self._step)
        servo_write(self.pitch_angle, self.yaw_angle)
        self._echo("Servo DOWN (-%.0f)" % self._step)

    # ==================== O: Toggle MANUAL/AUTO ====================
    def _cmd_toggle_mode(self):
        if self.work_mode == MODE_MANUAL:
            self.work_mode = MODE_AUTO
            self.auto_yaw = float(YAW_HOME)
            self.auto_pitch = float(PITCH_HOME)
            servo_write(self.auto_pitch, self.auto_yaw)

    # ==================== O: Toggle MANUAL/AUTO ====================
    def _cmd_toggle_mode(self):
        if self.work_mode == MODE_MANUAL:
            self.work_mode = MODE_AUTO
            self.auto_yaw = float(YAW_HOME)
            self.auto_pitch = float(PITCH_HOME)
            self._integral_pitch = 0.0  # reset integral on mode switch
            servo_write(self.auto_pitch, self.auto_yaw)
            if not self.detect_switch:
                print("[SYS] WARNING: detect OFF, tracking ineffective. Send S first.")
            else:
                print("[SYS] AI auto tracking READY")
        else:
            self.work_mode = MODE_MANUAL
            servo_write(self.pitch_angle, self.yaw_angle)
        self._echo("Mode TOGGLE")

    # ==================== M: Toggle UART TX ====================
    def _cmd_toggle_tx(self):
        self.uart_tx_on = not self.uart_tx_on
        self._echo("UART TX %s" % ("ON" if self.uart_tx_on else "OFF"))

    # ==================== P: Pest Mode ====================
    def _cmd_pest_mode(self):
        self.target_mode = TARGET_PEST
        self.lost_counter = 0
        servo_home()
        self.yaw_angle = float(YAW_HOME)
        self.pitch_angle = float(PITCH_HOME)
        self._echo("Target PEST (servo reset)")

    # ==================== R: Body Tracking Mode ====================
    def _cmd_body_mode(self):
        self.target_mode = TARGET_BODY
        self.lost_counter = 0
        servo_home()
        self.yaw_angle = float(YAW_HOME)
        self.pitch_angle = float(PITCH_HOME)
        self._echo("Target BODY (servo reset)")

    # ==================== C: Cycle Crop ====================
    def _cmd_cycle_crop(self):
        self.crop_mode = (self.crop_mode + 1) % 2  # 0=corn, 1=sugarcane
        names = {0: "corn", 1: "sugarcane"}
        self._echo("Crop -> %s" % names.get(self.crop_mode, "?"))

    # ==================== Auto Tracking Update ====================
    def auto_track_update(self, target_x, target_y, img_w, img_h):
        if self.work_mode != MODE_AUTO or not self.detect_switch:
            return

        center_x = img_w / 2.0
        center_y = img_h / 2.0
        err_x = target_x - center_x
        err_y = target_y - center_y

        # Dead zone: 20px
        if abs(err_x) < 20 and abs(err_y) < 20:
            self._integral_pitch *= 0.9  # decay integral in deadzone
            return

        kp = 0.03
        ki = 0.005
        max_step = 2.0  # max pitch change per frame (degrees)
        integral_limit = 30.0

        # Yaw: P control
        dx = -err_x * kp

        # Pitch: PI control with anti-windup
        self._integral_pitch += err_y
        if self._integral_pitch > integral_limit:
            self._integral_pitch = integral_limit
        if self._integral_pitch < -integral_limit:
            self._integral_pitch = -integral_limit
        dy = err_y * kp + self._integral_pitch * ki

        old_yaw = self.auto_yaw
        old_pitch = self.auto_pitch
        raw_yaw = self.auto_yaw + dx
        raw_pitch = self.auto_pitch - dy
        new_yaw = max(YAW_MIN, min(YAW_MAX, raw_yaw))
        new_pitch = max(PITCH_MIN, min(PITCH_MAX, raw_pitch))

        # Pitch rate limiter: max 2 deg per frame
        dp = new_pitch - old_pitch
        if dp > max_step:
            new_pitch = old_pitch + max_step
        elif dp < -max_step:
            new_pitch = old_pitch - max_step
        new_pitch = max(PITCH_MIN, min(PITCH_MAX, new_pitch))

        ly = "HIT" if new_yaw != raw_yaw else "ok"
        lp = "HIT" if abs(new_pitch - old_pitch) >= max_step - 0.01 else "ok"
        print("[PID] tx=%.0f ty=%.0f | err=(%d,%d) | kp_d=(%.2f,%.2f) I=%.1f | yaw %.0f->%.0f [%s] pitch %.0f->%.0f [%s]" % (target_x, target_y, int(err_x), int(err_y), dx, dy, self._integral_pitch, old_yaw, new_yaw, ly, old_pitch, new_pitch, lp))
        self.auto_yaw = new_yaw
        self.auto_pitch = new_pitch
        servo_write(self.auto_pitch, self.auto_yaw)

    # ==================== O: Toggle MANUAL/AUTO ====================
    def _cmd_toggle_mode(self):
        if self.work_mode == MODE_MANUAL:
            self.work_mode = MODE_AUTO
            self.auto_yaw = float(YAW_HOME)
            self.auto_pitch = float(PITCH_HOME)
            self._integral_pitch = 0.0  # reset integral on mode switch
            servo_write(self.auto_pitch, self.auto_yaw)
            # Exit IDLE if currently idle (e.g., after failed search)
            if self.system_state == STATE_IDLE:
                self.system_state = STATE_TRACKING
            if not self.detect_switch:
                print("[SYS] WARNING: detect OFF, tracking ineffective. Send S first.")
            else:
                print("[SYS] AI auto tracking READY")
        else:
            self.work_mode = MODE_MANUAL
            servo_write(self.pitch_angle, self.yaw_angle)
        self._echo("Mode TOGGLE")

    # ==================== M: Toggle UART TX ====================
    def _cmd_toggle_tx(self):
        self.uart_tx_on = not self.uart_tx_on
        self._echo("UART TX %s" % ("ON" if self.uart_tx_on else "OFF"))

    # ==================== P: Pest Mode ====================
    def _cmd_pest_mode(self):
        self.target_mode = TARGET_PEST
        self.lost_counter = 0
        servo_home()
        self.yaw_angle = float(YAW_HOME)
        self.pitch_angle = float(PITCH_HOME)
        self._echo("Target PEST (servo reset)")

    # ==================== R: Body Tracking Mode ====================
    def _cmd_body_mode(self):
        self.target_mode = TARGET_BODY
        self.lost_counter = 0
        servo_home()
        self.yaw_angle = float(YAW_HOME)
        self.pitch_angle = float(PITCH_HOME)
        self._echo("Target BODY (servo reset)")

    # ==================== C: Cycle Crop ====================
    def _cmd_cycle_crop(self):
        self.crop_mode = (self.crop_mode + 1) % 2  # 0=corn, 1=sugarcane
        names = {0: "corn", 1: "sugarcane"}
        self._echo("Crop -> %s" % names.get(self.crop_mode, "?"))

    # ==================== Auto Tracking Update ====================
    def auto_track_update(self, target_x, target_y, img_w, img_h):
        if self.work_mode != MODE_AUTO or not self.detect_switch or self.system_state != STATE_TRACKING:
            return

        center_x = img_w / 2.0
        center_y = img_h / 2.0
        err_x = target_x - center_x
        err_y = target_y - center_y

        if abs(err_x) < 5 and abs(err_y) < 5:
            print("[PID] DEADZONE ex=%d ey=%d yaw=%.0f pitch=%.0f" % (err_x, err_y, self.auto_yaw, self.auto_pitch))
            return

        kp = 0.08
        dx = -err_x * kp
        dy = err_y * kp

        old_yaw = self.auto_yaw
        old_pitch = self.auto_pitch
        raw_yaw = self.auto_yaw + dx
        raw_pitch = self.auto_pitch - dy
        new_yaw = max(YAW_MIN, min(YAW_MAX, raw_yaw))
        new_pitch = max(PITCH_MIN, min(PITCH_MAX, raw_pitch))
        ly = "HIT" if new_yaw != raw_yaw else "ok"
        lp = "HIT" if new_pitch != raw_pitch else "ok"
        print("[PID] tx=%.1f ty=%.1f | cx=%.0f cy=%.0f | ex=%d ey=%d | dx=%.2f dy=%.2f | yaw %.1f->%.1f [%s] pitch %.1f->%.1f [%s]" % (target_x, target_y, center_x, center_y, err_x, err_y, dx, dy, old_yaw, new_yaw, ly, old_pitch, new_pitch, lp))
        self.auto_yaw = new_yaw
        self.auto_pitch = new_pitch
        servo_write(self.auto_pitch, self.auto_yaw)

    # ==================== Fast Home (no echo, immediate) ====================
    def fast_home(self):
        """Immediate servo home without echo or manual-mode guard."""
        from src.hardware.servo import servo_write, YAW_HOME, PITCH_HOME
        servo_write(float(PITCH_HOME), float(YAW_HOME))
        self.auto_yaw = float(YAW_HOME)
        self.auto_pitch = float(PITCH_HOME)
        self.yaw_angle = float(YAW_HOME)
        self.pitch_angle = float(PITCH_HOME)
        self._integral_pitch = 0.0

    # ==================== 0x11: Abort / Force IDLE ====================
    def _cmd_abort(self):
        self.fast_home()
        self.system_state = STATE_IDLE
        self._echo("ABORT -> IDLE")

    # ==================== Lost Target Protection ====================
    def lost_tick(self):
        """Call each frame when no target detected. Increments lost_counter."""
        self.lost_counter += 1
        self._integral_pitch *= 0.8

    def lost_reset(self):
        """Call when target is re-acquired."""
        self.lost_counter = 0

    def is_lost(self, threshold=30):
        """Returns True if target lost for more than threshold frames."""
        return self.lost_counter > threshold


# ==================== Global Singleton ====================
_instance = None


def get_state_machine():
    global _instance
    if _instance is None:
        _instance = StateMachine()
    return _instance