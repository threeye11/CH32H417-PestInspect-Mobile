"""
K230 GPIO 按键驱动模块
=====================
适配: 01Studio CanMV K230
功能: GPIO 按键输入 + 消抖 + 中断回调 + LED 指示
引脚: 支持任意 GPIO 引脚，通过 FPIOA 映射

硬件说明 (基于原理图):
  - 板载 S2 (RSTN): 复位按键，不可用于应用
  - 板载 S3 (BOOT): GPIO1/BOOT1，启动模式选择，不建议复用
  - LED2: GPIO52，板载绿色 LED
  - S3 按键: GPIO21 (板载用户按键, 低电平有效)

外接按键接线:
  S3 按键: Pin1 接 GPIO21, Pin2/3/4 接 GND
  R61(10k)外部上拉或使用内部上拉, 按下为低电平
"""

from machine import Pin
from machine import FPIOA
import time


# ============================================================
# FPIOA 引脚映射管理
# ============================================================
_fpioa = FPIOA()
_pin_mapped = {}  # 已映射的引脚缓存


def map_gpio(phys_pin, gpio_num):
    """将物理引脚映射到 GPIO 功能

    参数:
        phys_pin: int, 物理引脚号 (40Pin 接口上的编号)
        gpio_num: int, GPIO 编号 (FPIOA.GPIO0 ~ GPIO63)
    """
    key = (phys_pin, gpio_num)
    if key not in _pin_mapped:
        func = getattr(FPIOA, "GPIO{}".format(gpio_num), None)
        if func is None:
            raise ValueError("不支持的 GPIO 编号: {}".format(gpio_num))
        _fpioa.set_function(phys_pin, func)
        _pin_mapped[key] = True


# ============================================================
# 按键类
# ============================================================
class Button:
    """GPIO 按键驱动 (带消抖)

    使用方法:
        # 板载 S3 按键: GPIO21, 按下低电平
        btn = Button(phys_pin=21, gpio_num=21, active_low=True)
        btn.start()

        # 轮询模式
        if btn.was_pressed():
            print("按键按下")

        # 中断模式
        btn2 = Button(phys_pin=9, gpio_num=9)
        btn2.start(callback=my_callback)

        btn.stop()
    """

    # 消抖时间 (ms)
    DEBOUNCE_MS = 50

    def __init__(self, phys_pin, gpio_num, active_low=True, pull_up=True):
        """初始化按键

        参数:
            phys_pin: int, 物理引脚号
            gpio_num: int, GPIO 编号
            active_low: bool, True=按下为低电平 (接地)
            pull_up: bool, True=启用内部上拉
        """
        self.phys_pin = phys_pin
        self.gpio_num = gpio_num
        self.active_low = active_low
        self.pull_up = pull_up

        self._pin = None
        self._callback = None
        self._pressed_flag = False
        self._last_time = 0
        self._last_state = not active_low  # 默认释放状态
        self._started = False

    def start(self, callback=None):
        """启动按键检测

        参数:
            callback: function(pin), 按键按下时的回调函数 (中断模式)
        """
        # 映射引脚
        map_gpio(self.phys_pin, self.gpio_num)

        # 配置上下拉
        if self.pull_up:
            pull = Pin.PULL_UP
        else:
            pull = Pin.PULL_DOWN

        # 初始化为输入
        self._pin = Pin(self.gpio_num, Pin.IN, pull=pull, drive=7)

        # 读取初始状态
        self._last_state = self._pin.value()
        self._pressed_flag = False
        self._callback = callback

        # 如果提供了回调，启用中断
        if callback is not None:
            trigger = Pin.IRQ_FALLING if self.active_low else Pin.IRQ_RISING
            self._pin.irq(trigger=trigger, handler=self._irq_handler)

        self._started = True

    def stop(self):
        """停止按键检测，释放资源"""
        if self._pin is not None:
            # 禁用中断
            self._pin.irq(handler=None)
            self._pin = None
        self._started = False
        self._callback = None

    def _irq_handler(self, pin):
        """中断处理 (内部使用)"""
        now = time.ticks_ms()
        if time.ticks_diff(now, self._last_time) < self.DEBOUNCE_MS:
            return
        self._last_time = now
        self._pressed_flag = True
        if self._callback:
            self._callback(pin)

    def is_pressed(self):
        """实时读取按键状态 (带消抖)

        返回:
            bool, True=当前按下
        """
        if self._pin is None:
            return False
        val = self._pin.value()
        if self.active_low:
            return val == 0
        else:
            return val == 1

    def was_pressed(self):
        """检查是否有按键事件 (中断标志模式)

        返回:
            bool, True=自上次检查后有按下事件
        """
        if self._pressed_flag:
            self._pressed_flag = False
            return True
        return False

    def wait_press(self, timeout_ms=-1):
        """阻塞等待按键按下

        参数:
            timeout_ms: int, 超时时间(毫秒), -1=无限等待
        返回:
            bool, True=按下, False=超时
        """
        start = time.ticks_ms()
        while True:
            if self.is_pressed():
                # 等待消抖确认
                time.sleep_ms(self.DEBOUNCE_MS)
                if self.is_pressed():
                    # 等待释放
                    while self.is_pressed():
                        time.sleep_ms(10)
                    return True
            if timeout_ms >= 0:
                if time.ticks_diff(time.ticks_ms(), start) >= timeout_ms:
                    return False
            time.sleep_ms(10)


# ============================================================
# LED 指示灯
# ============================================================
class Led:
    """板载 LED 驱动

    使用方法:
        led = Led(gpio_num=52)  # LED2 = GPIO52
        led.on()
        led.off()
        led.toggle()
        led.blink(times=3, interval_ms=200)
    """

    def __init__(self, gpio_num=52, phys_pin=None):
        """初始化 LED

        参数:
            gpio_num: int, GPIO 编号 (默认 52 = LED2)
            phys_pin: int, 物理引脚号 (None=自动根据 gpio_num 推断)
        """
        self.gpio_num = gpio_num
        if phys_pin is None:
            phys_pin = gpio_num  # 默认物理引脚号=GPIO号
        map_gpio(phys_pin, gpio_num)
        self._pin = Pin(gpio_num, Pin.OUT, pull=Pin.PULL_NONE, drive=7)
        self._pin.value(0)

    def on(self):
        self._pin.value(1)

    def off(self):
        self._pin.value(0)

    def toggle(self):
        self._pin.value(1 - self._pin.value())

    def blink(self, times=3, interval_ms=200):
        """闪烁 LED"""
        for _ in range(times):
            self.on()
            time.sleep_ms(interval_ms)
            self.off()
            time.sleep_ms(interval_ms)

    @property
    def state(self):
        return self._pin.value() == 1
