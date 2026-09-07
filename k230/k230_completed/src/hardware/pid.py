# Improved PID Controller for 2-Axis Gimbal
# Based on 01Studio face tracking PID, with enhancements:
#
# 1. Integral separation:
#    When abs(error) > img_width/3, freeze integral to prevent
#    overshoot during large deviations.
#
# 2. Output clamping:
#    Single update output limited to +/-MAX_OUTPUT_DEG (8 deg)
#    to prevent servo jerking on target jumps.
#
# 3. Dead zone:
#    Ignore errors smaller than dead_zone to reduce jitter.


class GimbalPID:
    """
    Single-axis PID controller for gimbal servo tracking.

    Args:
        kp: Proportional gain
        ki: Integral gain (set 0 to disable I term)
        kd: Derivative gain
        img_size: Image dimension for this axis (width for yaw, height for pitch)
        dead_zone: Ignore errors below this threshold (pixels)
        max_output: Maximum output increment per update (degrees)
        integral_threshold: Error threshold to freeze integral (pixels)
    """

    def __init__(self, kp=0.01, ki=0.0, kd=0.001,
                 img_size=320, dead_zone=10,
                 max_output=8.0, integral_threshold=None):
        self.kp = kp
        self.ki = ki
        self.kd = kd
        self.img_size = img_size
        self.dead_zone = dead_zone
        self.max_output = max_output

        # Integral separation threshold: default = img_size / 3
        if integral_threshold is None:
            self.integral_threshold = img_size / 3.0
        else:
            self.integral_threshold = integral_threshold

        self.target = 0.0
        self.error = 0.0
        self.last_error = 0.0
        self.integral = 0.0
        self.output = 0.0

    def set_target(self, target):
        """Set target position (image center coordinate)."""
        self.target = float(target)
        self.integral = 0.0
        self.last_error = 0.0

    def update(self, current_value):
        """
        Compute PID output for one axis.

        Args:
            current_value: Current position (detection center coordinate)

        Returns:
            float: Angle increment in degrees (clamped to +/-max_output)
        """
        self.error = self.target - current_value

        # Dead zone: ignore small errors to reduce jitter
        if abs(self.error) < self.dead_zone:
            return 0.0

        # ---- Integral with separation ----
        # Skip entirely when ki=0 (no integral term)
        # Freeze when error exceeds threshold
        if self.ki != 0 and abs(self.error) <= self.integral_threshold:
            self.integral += self.error

        # ---- Derivative ----
        derivative = self.error - self.last_error
        self.last_error = self.error

        # ---- PID output ----
        raw_output = (self.kp * self.error +
                      self.ki * self.integral +
                      self.kd * derivative)

        # ---- Output clamping ----
        if raw_output > self.max_output:
            raw_output = self.max_output
        elif raw_output < -self.max_output:
            raw_output = -self.max_output

        self.output = raw_output
        return self.output

    def reset(self):
        """Reset all PID state."""
        self.error = 0.0
        self.last_error = 0.0
        self.integral = 0.0
        self.output = 0.0

    @property
    def state_str(self):
        """Compact state string for debug print."""
        return "err=%.1f I=%.1f out=%.2f" % (self.error, self.integral, self.output)


# ==================== Dual-Axis PID Factory ====================

def create_gimbal_pids(img_w=320, img_h=240, kp=0.01, ki=0.0, kd=0.001,
                       dead_zone=10, max_output=8.0):
    """
    Create a matched pair of PID controllers for yaw (X) and pitch (Y).

    Returns:
        (yaw_pid, pitch_pid)
    """
    yaw_pid = GimbalPID(
        kp=kp, ki=ki, kd=kd,
        img_size=img_w, dead_zone=dead_zone,
        max_output=max_output,
        integral_threshold=img_w / 3.0
    )
    yaw_pid.set_target(img_w / 2.0)

    pitch_pid = GimbalPID(
        kp=kp, ki=ki, kd=kd,
        img_size=img_h, dead_zone=dead_zone,
        max_output=max_output,
        integral_threshold=img_h / 3.0
    )
    pitch_pid.set_target(img_h / 2.0)

    return (yaw_pid, pitch_pid)