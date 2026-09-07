# Servo PWM Driver for 2-Axis Gimbal (K230 Direct PWM)
# Hardware: K230 PWM -> Servo X (IO60), Servo Y (IO61)
# Independent 5V servo power, common GND with K230
#
# CanMV K230 PWM API:
#   FPIOA.set_function(pin, FPIOA.PWMx)  -- remap pin to PWM channel
#   PWM(channel, freq=50)                 -- create PWM object
#   pwm.duty_ns(pulse_width_ns)          -- set pulse width

from machine import PWM, FPIOA
import time

# ==================== Hardware Angle Limits ====================
# Prevent servo mechanical damage from over-rotation.
YAW_MIN = 10
YAW_MAX = 170
PITCH_MIN = 15
PITCH_MAX = 165

# ==================== Home Position (single source of truth) ====================
YAW_HOME = 90
PITCH_HOME = 80

# ==================== Pin & PWM Channel Mapping ====================
# K230 FPIOA: any IO can map to PWM0~PWM3
# IO60 -> PWM0 (YAW / X-axis horizontal)
# IO61 -> PWM1 (PITCH / Y-axis vertical)
_YAW_PIN = 60
_YAW_PWM_CHANNEL = 0
_PITCH_PIN = 61
_PITCH_PWM_CHANNEL = 1

# ==================== Servo PWM Parameters ====================
_SERVO_FREQ = 50              # 50Hz standard servo
_SERVO_MIN_US = 500           # 0 deg  -> 0.5ms pulse
_SERVO_MAX_US = 2500          # 180 deg -> 2.5ms pulse
_SERVO_MIN_NS = _SERVO_MIN_US * 1000   # 500000 ns
_SERVO_MAX_NS = _SERVO_MAX_US * 1000   # 2500000 ns
_SERVO_RANGE_NS = _SERVO_MAX_NS - _SERVO_MIN_NS  # 2000000 ns

# ==================== Init State ====================
_initialized = False
_pwm_yaw = None
_pwm_pitch = None
_yaw_angle = 90.0
_pitch_angle = 90.0


def _init_hardware():
    """Lazily initialize FPIOA + PWM channels (called on first servo_write)."""
    global _initialized, _pwm_yaw, _pwm_pitch
    if _initialized:
        return
    fpioa = FPIOA()
    fpioa.set_function(_YAW_PIN, getattr(FPIOA, "PWM%d" % _YAW_PWM_CHANNEL))
    fpioa.set_function(_PITCH_PIN, getattr(FPIOA, "PWM%d" % _PITCH_PWM_CHANNEL))
    _pwm_yaw = PWM(_YAW_PWM_CHANNEL, freq=_SERVO_FREQ, duty_ns=0)
    _pwm_pitch = PWM(_PITCH_PWM_CHANNEL, freq=_SERVO_FREQ, duty_ns=0)
    _initialized = True


def _angle_to_ns(angle):
    """Convert 0~180 degree angle to PWM pulse width in nanoseconds."""
    if angle <= 0:
        return _SERVO_MIN_NS
    if angle >= 180:
        return _SERVO_MAX_NS
    return int(_SERVO_MIN_NS + _SERVO_RANGE_NS * angle / 180.0)


def _clamp(value, lo, hi):
    if value < lo:
        return lo
    if value > hi:
        return hi
    return value


def _set_servo(pwm_obj, angle):
    """Set a single servo to the given angle (clamped 0~180)."""
    angle = _clamp(float(angle), 0, 180)
    pwm_obj.duty_ns(_angle_to_ns(angle))
    return angle


def servo_write(pitch, yaw):
    """
    Set both servo angles with enforced hardware limits.

    Args:
        pitch: Vertical angle (clamped to PITCH_MIN..PITCH_MAX)
        yaw:   Horizontal angle (clamped to YAW_MIN..YAW_MAX)

    Returns:
        (clamped_pitch, clamped_yaw)
    """
    global _yaw_angle, _pitch_angle
    _init_hardware()
    _pitch_angle = _clamp(float(pitch), PITCH_MIN, PITCH_MAX)
    _yaw_angle = _clamp(float(yaw), YAW_MIN, YAW_MAX)
    _set_servo(_pwm_pitch, _pitch_angle)
    _set_servo(_pwm_yaw, _yaw_angle)
    return (_pitch_angle, _yaw_angle)


def servo_home():
    """Move both servos to home position."""
    global _yaw_angle, _pitch_angle
    _init_hardware()
    _pitch_angle = float(PITCH_HOME)
    _yaw_angle = float(YAW_HOME)
    _set_servo(_pwm_pitch, PITCH_HOME)
    _set_servo(_pwm_yaw, YAW_HOME)


def servo_deinit():
    """Release PWM resources."""
    global _initialized, _pwm_yaw, _pwm_pitch
    if _pwm_yaw:
        try:
            _pwm_yaw.deinit()
        except Exception:
            pass
        _pwm_yaw = None
    if _pwm_pitch:
        try:
            _pwm_pitch.deinit()
        except Exception:
            pass
        _pwm_pitch = None
    _initialized = False