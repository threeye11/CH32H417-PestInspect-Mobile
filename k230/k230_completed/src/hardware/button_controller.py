"""
病虫害检测系统 - 按键控制器
============================
功能: 按键控制检测的 开始/暂停/停止
集成: 配合 main_lvgl.py 使用

控制逻辑:
  按键1 (短按): 切换 开始/暂停 检测
  按键1 (长按2秒): 完全停止并退出

LED 状态指示:
  常亮: 检测运行中
  慢闪: 检测暂停
  快闪: 启动/停止过渡
  灭:   系统就绪未启动

使用方法:
  from src.hardware.button_controller import ButtonController

  ctrl = ButtonController()
  ctrl.start()

  # 在主循环中
  action = ctrl.update()
  if action == ButtonController.ACTION_TOGGLE:
      # 切换检测状态
  elif action == ButtonController.ACTION_EXIT:
      # 退出程序
"""

from src.hardware.button import Button, Led
import time


class ButtonController:
    """按键控制器 - 管理按键输入和系统状态"""

    # 动作常量
    ACTION_NONE = 0       # 无操作
    ACTION_TOGGLE = 1     # 切换 开始/暂停
    ACTION_EXIT = 2       # 完全退出

    # 长按时间阈值 (ms)
    LONG_PRESS_MS = 2000

    # 默认引脚配置 (01Studio CanMV K230)
    # 板载 S3 用户按键: GPIO21 (低电平有效)
    DEFAULT_BTN_PHYS = 21
    DEFAULT_BTN_GPIO = 21
    DEFAULT_LED_GPIO = 52  # 板载 LED2

    def __init__(self, btn_phys=None, btn_gpio=None, led_gpio=None):
        """初始化按键控制器

        参数:
            btn_phys: int, 按键物理引脚号 (默认 CON1-7)
            btn_gpio: int, 按键 GPIO 编号 (默认 GPIO7)
            led_gpio: int, LED GPIO 编号 (默认 GPIO52 = LED2)
        """
        self.btn_phys = btn_phys or self.DEFAULT_BTN_PHYS
        self.btn_gpio = btn_gpio or self.DEFAULT_BTN_GPIO
        self.led_gpio = led_gpio or self.DEFAULT_LED_GPIO

        self._button = None
        self._led = None
        self._state = "idle"  # idle / running / paused
        self._press_start = 0
        self._pressing = False
        self._last_blink = 0
        self._blink_state = False

    def start(self):
        """启动按键控制器"""
        try:
            # 初始化 LED
            self._led = Led(gpio_num=self.led_gpio)
            self._led.off()
        except Exception as e:
            print("[BTN] LED init failed: {}".format(e))
            self._led = None

        try:
            # 初始化按键 (轮询模式)
            self._button = Button(
                phys_pin=self.btn_phys,
                gpio_num=self.btn_gpio,
                active_low=True,
                pull_up=True
            )
            self._button.start()
            print("[BTN] Button ready: phys_pin={}, gpio={}".format(
                self.btn_phys, self.btn_gpio))
        except Exception as e:
            print("[BTN] Button init failed: {}".format(e))
            self._button = None

    def stop(self):
        """停止按键控制器"""
        if self._button:
            self._button.stop()
        if self._led:
            self._led.off()

    def get_state(self):
        """获取当前系统状态

        返回:
            str: "idle" / "running" / "paused"
        """
        return self._state

    def set_state(self, state):
        """外部设置状态 (用于同步)"""
        self._state = state

    def update(self):
        """主循环中调用，处理按键事件并更新 LED

        返回:
            int: ACTION_NONE / ACTION_TOGGLE / ACTION_EXIT
        """
        if self._button is None:
            return self.ACTION_NONE

        now = time.ticks_ms()
        pressed = self._button.is_pressed()

        # --- 按键状态机 ---
        if pressed and not self._pressing:
            # 按下开始
            self._pressing = True
            self._press_start = now

        elif pressed and self._pressing:
            # 持续按住 - 检测长按
            hold_time = time.ticks_diff(now, self._press_start)
            if hold_time >= self.LONG_PRESS_MS:
                # 长按确认: 等待释放后返回退出动作
                self._wait_release()
                self._pressing = False
                self._state = "idle"
                return self.ACTION_EXIT

        elif not pressed and self._pressing:
            # 释放 - 短按
            hold_time = time.ticks_diff(now, self._press_start)
            self._pressing = False
            if hold_time < self.LONG_PRESS_MS:
                # 短按: 切换
                return self._do_toggle()

        # --- LED 状态指示 ---
        self._update_led(now)

        return self.ACTION_NONE

    def _do_toggle(self):
        """执行切换操作"""
        if self._state == "idle" or self._state == "paused":
            self._state = "running"
            return self.ACTION_TOGGLE
        elif self._state == "running":
            self._state = "paused"
            return self.ACTION_TOGGLE
        return self.ACTION_NONE

    def _update_led(self, now):
        """根据状态更新 LED"""
        if self._led is None:
            return

        if self._state == "running":
            # 常亮
            self._led.on()
        elif self._state == "paused":
            # 慢闪 (500ms)
            if time.ticks_diff(now, self._last_blink) >= 500:
                self._blink_state = not self._blink_state
                if self._blink_state:
                    self._led.on()
                else:
                    self._led.off()
                self._last_blink = now
        else:
            # idle: 灭
            self._led.off()

    def _wait_release(self):
        """等待按键释放"""
        while self._button.is_pressed():
            time.sleep_ms(20)
        time.sleep_ms(self.Button.DEBOUNCE_MS if hasattr(self, 'Button') else 50)

    @staticmethod
    def print_help():
        """打印按键说明"""
        print("-" * 40)
        print("  Button Control:")
        print("    Short press: Start / Pause toggle")
        print("    Long press (2s): Exit program")
        print("  LED Status:")
        print("    ON:      Detection running")
        print("    Blink:   Detection paused")
        print("    OFF:     System ready")
        print("-" * 40)
