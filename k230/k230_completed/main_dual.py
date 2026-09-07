"""
Corn Disease Detection - LVGL v3
Detection: PipeLine + CornYOLO11 (based on demo-camera.py)
"""

from libs.PipeLine import PipeLine
from libs.YOLO import YOLO11
from libs.Utils import *
from media.display import *
from media.media import *
from media.sensor import *
import time, os, sys, gc
import lvgl as lv
import _thread
import image
import uctypes
import ulab.numpy as np

# Config
DISPLAY_WIDTH = ALIGN_UP(800, 16)
DISPLAY_HEIGHT = 480
DISPLAY_SIZE = [800, 480]
RES_PATH = "/sdcard/data/"
KMODEL_PATH = "/sdcard/yolo11s_det_320.kmodel"
MODEL_INPUT_SIZE = [320, 320]
RGB888P_SIZE = [640, 360]
LABELS = {0: "玉米非生物胁迫病", 1: "玉米蚜虫害", 2: "玉米弯孢霉叶斑病",
          3: "泥土", 4: "玉米大斑病", 5: "玉米健康叶",
          6: "玉米锈病", 7: "草地贪夜蛾幼虫", 8: "草地贪夜蛾叶片",
          9: "玉米条纹病", 10: "杂草"}
LABELS_LIST = ["玉米非生物胁迫病", "玉米蚜虫害", "玉米弯孢霉叶斑病",
               "泥土", "玉米大斑病", "玉米健康叶",
               "玉米锈病", "草地贪夜蛾幼虫", "草地贪夜蛾叶片",
               "玉米条纹病", "杂草"]
LABELS_EXCLUDE = ("Weeds", "dirt")

# ==================== Crop Config ====================
CROP_CORN = 0
CROP_POTATO = 1
CROP_CONFIGS = {
    CROP_CORN: {
        "labels": LABELS, "labels_list": LABELS_LIST, "labels_exclude": LABELS_EXCLUDE,
        "kmodel_path": KMODEL_PATH,
    },
    CROP_POTATO: {
        "labels": {0: "马铃薯青枯病", 1: "马铃薯早疫病", 2: "马铃薯健康", 3: "马铃薯根结线虫病", 4: "马铃薯虫害", 5: "马铃薯晚疫病"},
        "labels_list": ["马铃薯青枯病", "马铃薯早疫病", "马铃薯健康", "马铃薯根结线虫病", "马铃薯虫害", "马铃薯晚疫病"],
        "labels_exclude": (),
        "kmodel_path": "/sdcard/potato_det_320.kmodel",
    },
}
CROP_NAMES = {CROP_CORN: 'corn', CROP_POTATO: 'potato'}
DET_HOLD_MS = 5000

# ==================== Person Tracking ====================
MODE_PEST = 0
MODE_TRACK = 1
current_mode = MODE_PEST
track_result = None
person_det = None
person_tracker = None
CMD_MODE_PEST = 0x05
CMD_MODE_TRACK = 0x06
CMD_MUTE_TX = 0x03
CMD_SWITCH_CROP = 0x04

class KalmanFilter2D:
    def __init__(self, pn=0.03, mn=0.5):
        self.x=[0.0,0.0,0.0,0.0]; self.P=[[1.0,0,0,0],[0,1.0,0,0],[0,0,1.0,0],[0,0,0,1.0]]
        self.q=pn; self.r=mn; self.initialized=False; self.last_time=0
    def predict(self, dt):
        if dt<=0: dt=0.033
        self.x=[self.x[0]+self.x[2]*dt, self.x[1]+self.x[3]*dt, self.x[2], self.x[3]]
        q=self.q*dt*dt
        for i in range(4): self.P[i][i]+=q if i<2 else self.q
    def update(self, zx, zy):
        y0=zx-self.x[0]; y1=zy-self.x[1]
        s00=self.P[0][0]+self.r; s11=self.P[1][1]+self.r; det=s00*s11-self.P[0][1]*self.P[1][0]
        if abs(det)<1e-6: det=1e-6
        k00=(self.P[0][0]*s11+self.P[0][1]*(-self.P[1][0]))/det
        k01=(self.P[0][0]*(-self.P[0][1])+self.P[0][1]*s00)/det
        k10=(self.P[1][0]*s11+self.P[1][1]*(-self.P[1][0]))/det
        k11=(self.P[1][0]*(-self.P[0][1])+self.P[1][1]*s00)/det
        self.x[0]+=k00*y0+k01*y1; self.x[1]+=k10*y0+k11*y1
        p00=self.P[0][0]; p01=self.P[0][1]; p10=self.P[1][0]; p11=self.P[1][1]
        self.P[0][0]=p00-k00*p00; self.P[0][1]=p01-k00*p01; self.P[1][0]=p10-k10*p00; self.P[1][1]=p11-k10*p01
    def get_smoothed_position(self): return (self.x[0], self.x[1])
    def reset(self):
        self.x=[0.0,0.0,0.0,0.0]; self.P=[[1.0,0,0,0],[0,1.0,0,0],[0,0,1.0,0],[0,0,0,1.0]]; self.initialized=False
    def initialize(self, x, y):
        self.x=[float(x),float(y),0.0,0.0]; self.initialized=True
        self.last_time=time.ticks_ms() if hasattr(time,'ticks_ms') else int(time.time()*1000)

class PersonTracker:
    def __init__(self):
        self._kf_pos=KalmanFilter2D(0.05,1.0); self._kf_size=KalmanFilter2D(0.02,2.0)
        self._lost=0; self._max_lost=15; self._tid=0; self._was_lost=True
        self._cx=320; self._cy=180
    def select(self, dets):
        if not dets: return None
        best=None; ba=0
        for d in dets:
            if len(d)<3 or int(d[2])!=0: continue
            a=d[0][2]*d[0][3]
            if a>ba: ba=a; best=(int(d[0][0]),int(d[0][1]),int(d[0][2]),int(d[0][3]),float(d[1]))
        return best
    def update(self, dets):
        t=self.select(dets); ts=int(time.ticks_ms()) if hasattr(time,'ticks_ms') else int(time.time()*1000)
        if t:
            x,y,w,h,c=t; cx=x+w//2; cy=y+h//2
            if self._was_lost: self._tid=(self._tid+1)&0xFF or 1; self._was_lost=False
            if not self._kf_pos.initialized: self._kf_pos.initialize(cx,cy); self._kf_size.initialize(w,h)
            else:
                ct=time.ticks_ms() if hasattr(time,'ticks_ms') else int(time.time()*1000)
                dt=(ct-self._kf_pos.last_time)/1000.0; dt=max(dt,0.033)
                self._kf_pos.predict(dt); self._kf_size.predict(dt)
                self._kf_pos.update(cx,cy); self._kf_size.update(w,h)
                self._kf_pos.last_time=ct; self._kf_size.last_time=ct
            sx,sy=self._kf_pos.get_smoothed_position(); sw,sh=self._kf_size.get_smoothed_position()
            sx=int(sx-sw/2); sy=int(sy-sh/2); sw=max(10,int(sw)); sh=max(10,int(sh))
            sx=max(0,min(sx,640-sw)); sy=max(0,min(sy,360-sh))
            dx=int(sx+sw/2-self._cx); dy=int(sy+sh/2-self._cy)
            self._lost=0
            return {'found':True,'x':sx,'y':sy,'w':sw,'h':sh,'dx':dx,'dy':dy,'confidence':c,'track_id':self._tid,'status':1,'timestamp':ts&0xFFFF}
        self._lost+=1
        if self._lost>self._max_lost:
            self._kf_pos.reset(); self._kf_size.reset(); self._was_lost=True
            return {'found':False,'x':0,'y':0,'w':0,'h':0,'dx':0,'dy':0,'confidence':0,'track_id':self._tid,'status':0,'timestamp':ts&0xFFFF}
        ct=time.ticks_ms() if hasattr(time,'ticks_ms') else int(time.time()*1000)
        dt=(ct-self._kf_pos.last_time)/1000.0; dt=max(dt,0.033)
        self._kf_pos.predict(dt); self._kf_size.predict(dt)
        self._kf_pos.last_time=ct; self._kf_size.last_time=ct
        sx,sy=self._kf_pos.get_smoothed_position(); sw,sh=self._kf_size.get_smoothed_position()
        sx=int(sx-sw/2); sy=int(sy-sh/2); sw=max(10,int(sw)); sh=max(10,int(sh))
        dx=int(sx+sw/2-self._cx); dy=int(sy+sh/2-self._cy)
        return {'found':True,'x':sx,'y':sy,'w':sw,'h':sh,'dx':dx,'dy':dy,'confidence':0,'track_id':self._tid,'status':1,'timestamp':ts&0xFFFF}

PERSON_KMODEL_PATH = "/sdcard/person_detect_yolo11n.kmodel"
PERSON_MODEL_INPUT_SIZE = [320, 320]


def _ensure_person_detector():
    global person_det, person_tracker
    if person_det is not None: return True
    try:
        from libs.YOLO import YOLO11
        gc.collect(); time.sleep_ms(100)
        print("[TRACK] Loading person model...")
        pd = YOLO11(task_type="detect", mode="video", kmodel_path=PERSON_KMODEL_PATH, labels={0: "person"}, rgb888p_size=RGB888P_SIZE, model_input_size=PERSON_MODEL_INPUT_SIZE, display_size=DISPLAY_SIZE, conf_thresh=0.6, nms_thresh=0.45, max_boxes_num=50, debug_mode=0)
        pd.config_preprocess()
        person_det = pd; person_tracker = PersonTracker()
        print("[TRACK] Person detector ready")
        return True
    except Exception as e:
        print("[TRACK] Person init error:", e)
        person_det = None; person_tracker = None
        return False



# Hardware
from machine import Pin
from machine import FPIOA
BTN_PHYS_PIN = 21
BTN_GPIO_NUM = 21
LED_GPIO_NUM = 52
BTN_LONG_PRESS_MS = 2000

# Global state
pl = None
cur_state = 0
ai_running = False
yolo_ref = None
gui_fps = 0.0
gui_dets = None
gui_dirty = False
btn_action = 0
disp_img1 = None
disp_img2 = None
fps_label = None
count_label = None
result_label = None
status_label = None
btn_pin = None
led_pin = None
camera_paused = False
stream_frame = None; shared_frame_565 = None
shared_frame_raw = None
det_frame_565 = None; det_frame_time = 0
prev_det_classes = set(); det_result_changed = False
crop_mode = CROP_CORN
crop_switch_to = -1
uart_tx_enabled = True

# UART
from machine import UART
from machine import FPIOA as _FPIOA
HEADER1 = 0xAA; HEADER2 = 0x55; FOOTER1 = 0x55; FOOTER2 = 0xAA
DATA_LEN = 8; TX_FRAME_LEN = 15; TYPE_PEST = 0x01
CMD_START = 0x01; CMD_STOP = 0x02

TYPE_TRACK = 0x03; TRACK_DATA_LEN = 16; TRACK_TX_FRAME_LEN = 23
uart_tx_enabled = True
def set_status(text, color_hex):
    try:
        if status_label:
            status_label.set_text(text)
            status_label.set_style_text_color(lv.color_hex(color_hex), 0)
    except: pass
TYPE_TRACK = 0x03

def set_status(text, color_hex):
    try:
        if status_label:
            status_label.set_text(text)
            status_label.set_style_text_color(lv.color_hex(color_hex), 0)
    except: pass

class UartComm:
    def __init__(self):
        self._uart = None; self._ok = False; self._rbuf = bytearray(16); self._rlen = 0
    def init(self):
        fp = _FPIOA()
        fp.set_function(3, _FPIOA.UART1_TXD)
        fp.set_function(4, _FPIOA.UART1_RXD)
        self._uart = UART(UART.UART1, baudrate=921600, bits=UART.EIGHTBITS, parity=UART.PARITY_NONE, stop=UART.STOPBITS_ONE)
        self._ok = True
        return self
    def send_pest_data(self, pest_type, confidence, count, crop_type=0):
        if not self._ok: return
        data = bytearray(DATA_LEN)
        data[0] = pest_type & 0xFF; data[1] = confidence & 0xFF
        data[2] = count & 0xFF; data[3] = (count >> 8) & 0xFF
        data[4] = crop_type & 0xFF
        chk = HEADER1 ^ HEADER2 ^ TYPE_PEST ^ DATA_LEN
        for i in range(DATA_LEN): chk ^= data[i]
        frame = bytearray(TX_FRAME_LEN)
        frame[0] = HEADER1; frame[1] = HEADER2; frame[2] = TYPE_PEST; frame[3] = DATA_LEN
        frame[4:12] = data; frame[12] = chk; frame[13] = FOOTER1; frame[14] = FOOTER2
        try: self._uart.write(bytes(frame))
        except: pass
    def send_track_data(self, x, y, w, h, dx, dy, confidence, track_id, status):
        if not self._ok: return
        data = bytearray(16)
        data[0] = x & 0xFF; data[1] = (x >> 8) & 0xFF
        data[2] = y & 0xFF; data[3] = (y >> 8) & 0xFF
        data[4] = w & 0xFF; data[5] = (w >> 8) & 0xFF
        data[6] = h & 0xFF; data[7] = (h >> 8) & 0xFF
        data[8] = dx & 0xFF; data[9] = (dx >> 8) & 0xFF
        data[10] = dy & 0xFF; data[11] = (dy >> 8) & 0xFF
        data[12] = confidence & 0xFF; data[13] = track_id & 0xFF; data[14] = status & 0xFF; data[15] = 0
        chk = HEADER1 ^ HEADER2 ^ TYPE_TRACK ^ 16
        for i in range(16): chk ^= data[i]
        frame = bytearray(23)
        frame[0] = HEADER1; frame[1] = HEADER2; frame[2] = TYPE_TRACK; frame[3] = 16
        frame[4:20] = data; frame[20] = chk; frame[21] = FOOTER1; frame[22] = FOOTER2
        try: self._uart.write(bytes(frame))
        except: pass
    def check_command(self):
        if not self._ok: return 0
        try:
            n = self._uart.any()
            if n == 0: return 0
            if n > 8: n = 8
            chunk = self._uart.read(n)
            if chunk is None: return 0
        except: return 0
        rb = self._rbuf; rl = self._rlen
        for b in chunk:
            if rl < len(rb): rb[rl] = b
            rl += 1
        self._rlen = rl
        result = 0; consumed = 0; i = 0
        while i + 4 < self._rlen:
            if rb[i] == HEADER1 and rb[i+1] == HEADER2 and rb[i+3] == FOOTER1 and rb[i+4] == FOOTER2:
                _cmd = rb[i+2]
                if _cmd in (CMD_START, CMD_STOP, CMD_MUTE_TX, CMD_SWITCH_CROP, CMD_MODE_PEST, CMD_MODE_TRACK):
                    result = _cmd
                consumed = i + 5; i += 5; continue
            i += 1
        if consumed > 0:
            remaining = self._rlen - consumed
            if remaining > 0:
                for j in range(remaining): rb[j] = rb[consumed + j]
            self._rlen = remaining
        elif self._rlen > 4:
            self._rlen = 0
        return result
    def deinit(self):
        if self._uart:
            try: self._uart.deinit()
            except: pass
            self._uart = None
        self._ok = False


# Button + LED
def button_init():
    global btn_pin, led_pin
    fpioa = FPIOA()
    fpioa.set_function(BTN_PHYS_PIN, getattr(FPIOA, "GPIO{}".format(BTN_GPIO_NUM)))
    fpioa.set_function(LED_GPIO_NUM, getattr(FPIOA, "GPIO{}".format(LED_GPIO_NUM)))
    btn_pin = Pin(BTN_GPIO_NUM, Pin.IN, pull=Pin.PULL_UP, drive=7)
    led_pin = Pin(LED_GPIO_NUM, Pin.OUT, pull=Pin.PULL_NONE, drive=7)
    led_pin.value(0)

def button_is_pressed():
    if btn_pin is None: return False
    v1 = btn_pin.value(); time.sleep_ms(5); v2 = btn_pin.value()
    return v1 == 0 and v2 == 0

def led_set(state):
    if led_pin: led_pin.value(1 if state else 0)

def led_blink_once(ms=100):
    led_set(True); time.sleep_ms(ms); led_set(False)

_btn_last_blink = 0; _btn_blink_on = False

# LVGL
def disp_drv_flush_cb(disp_drv, area, color):
    global disp_img1, disp_img2
    try:
        if disp_drv.flush_is_last() == True:
            if disp_img1.virtaddr() == uctypes.addressof(color.__dereference__()):
                disp_img2.bytearray()[:] = bytearray(0)
                Display.show_image(disp_img1, layer=Display.LAYER_OSD2)
            else:
                disp_img1.bytearray()[:] = bytearray(0)
                Display.show_image(disp_img2, layer=Display.LAYER_OSD2)
    except:
        pass
    disp_drv.flush_ready()

def lvgl_init():
    global disp_img1, disp_img2
    try: lv.deinit()
    except: pass
    time.sleep_ms(100)
    gc.collect()
    lv.init()
    disp_drv = lv.disp_create(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    disp_drv.set_flush_cb(disp_drv_flush_cb)
    disp_drv.set_color_format(lv.COLOR_FORMAT.ARGB8888)
    disp_img1 = image.Image(DISPLAY_SIZE[0], DISPLAY_SIZE[1], image.BGRA8888)
    disp_img2 = image.Image(DISPLAY_SIZE[0], DISPLAY_SIZE[1], image.BGRA8888)
    disp_img1.clear(); disp_img2.clear()
    disp_drv.set_draw_buffers(disp_img1.bytearray(), disp_img2.bytearray(), disp_img1.size(), lv.DISP_RENDER_MODE.FULL)

def lvgl_deinit():
    global disp_img1, disp_img2
    try:
        disp_img1.clear(); disp_img2.clear(); lv.deinit()
        del disp_img1; del disp_img2
    except: pass

def gui_init():
    global fps_label, count_label, result_label, status_label
    font_cn = None
    try: font_cn = lv.font_load("A:" + RES_PATH + "font/lv_font_simsun_16_cjk.fnt")
    except: pass
    scr = lv.scr_act(); scr.set_style_bg_opa(lv.OPA.TRANSP, lv.PART.MAIN)
    panel = lv.obj(lv.layer_sys())
    panel.set_size(180, 480); panel.set_pos(620, 0)
    panel.set_style_bg_color(lv.color_hex(0x1a1a2e), lv.PART.MAIN)
    panel.set_style_bg_opa(200, lv.PART.MAIN)
    panel.set_style_border_width(0, lv.PART.MAIN)
    panel.set_style_radius(0, lv.PART.MAIN)
    panel.clear_flag(lv.obj.FLAG.SCROLLABLE)
    t = lv.label(panel); t.set_text("Corn Disease")
    t.set_style_text_color(lv.color_hex(0xe94560), 0); t.align(lv.ALIGN.TOP_MID, 0, 5)
    lb = lv.label(panel); lb.set_text("FPS")
    lb.set_style_text_color(lv.color_hex(0x53d8fb), 0); lb.align(lv.ALIGN.TOP_MID, 0, 30)
    fps_label = lv.label(panel); fps_label.set_text("--")
    fps_label.set_style_text_color(lv.color_hex(0xffffff), 0); fps_label.align(lv.ALIGN.TOP_MID, 0, 50)
    lb2 = lv.label(panel); lb2.set_text("Count")
    lb2.set_style_text_color(lv.color_hex(0x53d8fb), 0); lb2.align(lv.ALIGN.TOP_MID, 0, 75)
    count_label = lv.label(panel); count_label.set_text("0")
    count_label.set_style_text_color(lv.color_hex(0xffd700), 0); count_label.align(lv.ALIGN.TOP_MID, 0, 95)
    line = lv.obj(panel); line.set_size(150, 2); line.align(lv.ALIGN.TOP_MID, 0, 120)
    line.set_style_bg_color(lv.color_hex(0x0f3460), 0); line.set_style_border_width(0, 0)
    lb3 = lv.label(panel); lb3.set_text("Results")
    lb3.set_style_text_color(lv.color_hex(0x53d8fb), 0); lb3.align(lv.ALIGN.TOP_MID, 0, 130)
    result_label = lv.label(panel); result_label.set_text("---")
    result_label.set_style_text_color(lv.color_hex(0xffffff), 0); result_label.set_width(160)
    if font_cn: result_label.set_style_text_font(font_cn, 0)
    result_label.align(lv.ALIGN.TOP_LEFT, 10, 155)
    status_label = lv.label(panel); status_label.set_text("Starting...")
    status_label.set_style_text_color(lv.color_hex(0xaaaaaa), 0)
    status_label.align(lv.ALIGN.BOTTOM_MID, 0, -10)
    lv.scr_load(scr)

def update_gui():
    global gui_fps, gui_dirty
    try:
        fps_label.set_text("{:.1f}".format(gui_fps))
        dets = yolo_ref.all_dets if yolo_ref else []
        if dets:
            summary = {}
            visible_count = 0
            for det in dets:
                try:
                    cls_id = det[5]
                    if cls_id < len(LABELS_LIST) and LABELS_LIST[cls_id] in LABELS_EXCLUDE:
                        continue
                    visible_count += 1
                    name = LABELS_LIST[cls_id] if cls_id < len(LABELS_LIST) else str(cls_id)
                    summary[name] = summary.get(name, 0) + 1
                except: continue
            count_label.set_text(str(visible_count))
            if summary:
                text = ""
                for name, cnt in summary.items():
                    text += name + " x" + str(cnt) + "\n"
                result_label.set_text(text)
            else:
                result_label.set_text("No pest")
        else:
            count_label.set_text("0")
            result_label.set_text("No pest")
    except: pass

class CornYOLO11(YOLO11):
    """Override run() to parse all detections for GUI"""
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.all_dets = []

    def run(self, input_img):
        result = super().run(input_img)
        try:
            raw = self.results[0]
            out = raw.reshape((raw.shape[0] * raw.shape[1], raw.shape[2]))
            out = out.transpose()
            boxes = out[:, 0:4]
            scores = out[:, 4:]
            confs = np.max(scores, axis=-1)
            cls_ids = np.argmax(scores, axis=-1)
            xf = float(self.rgb888p_size[0]) / self.model_input_size[0]
            yf = float(self.rgb888p_size[1]) / self.model_input_size[1]
            dets = []
            for i in range(len(boxes)):
                if confs[i] > self.conf_thresh:
                    cx = float(boxes[i, 0]); cy = float(boxes[i, 1])
                    w = float(boxes[i, 2]); h = float(boxes[i, 3])
                    x1 = int((cx - 0.5 * w) * xf)
                    y1 = int((cy - 0.5 * h) * yf)
                    x2 = int((cx + 0.5 * w) * xf)
                    y2 = int((cy + 0.5 * h) * yf)
                    x1 = max(0, x1); y1 = max(0, y1)
                    x2 = min(self.rgb888p_size[0], x2)
                    y2 = min(self.rgb888p_size[1], y2)
                    dets.append((x1, y1, x2, y2, float(confs[i]), int(cls_ids[i])))
            # NMS
            nms_dets = []
            groups = {}
            for d in dets:
                cid = d[5]
                if cid not in groups: groups[cid] = []
                groups[cid].append(d)
            for cid, g in groups.items():
                g.sort(key=lambda x: x[4], reverse=True)
                keep = []; suppressed = [False] * len(g)
                for i in range(len(g)):
                    if suppressed[i]: continue
                    keep.append(g[i])
                    for j in range(i + 1, len(g)):
                        if suppressed[j]: continue
                        b1 = g[i]; b2 = g[j]
                        xx1 = max(b1[0], b2[0]); yy1 = max(b1[1], b2[1])
                        xx2 = min(b1[2], b2[2]); yy2 = min(b1[3], b2[3])
                        inter = max(0, xx2 - xx1) * max(0, yy2 - yy1)
                        a1 = (b1[2]-b1[0]) * (b1[3]-b1[1])
                        a2 = (b2[2]-b2[0]) * (b2[3]-b2[1])
                        if inter / (a1 + a2 - inter + 1e-6) > self.nms_thresh:
                            suppressed[j] = True
                nms_dets.extend(keep)
            self.all_dets = nms_dets
        except:
            self.all_dets = []
        return result

def _draw_screenshot_rect(buf, x1, y1, x2, y2, color):
    """Draw rectangle outline on RGB565 160x120 buffer."""
    w = 160
    lo = color & 0xFF
    hi = (color >> 8) & 0xFF
    for x in range(x1, min(x2 + 1, w)):
        for t in [y1, y2]:
            if 0 <= t < 120:
                p = (t * w + x) * 2
                buf[p] = lo; buf[p + 1] = hi
    for y in range(y1, min(y2 + 1, 120)):
        for t in [x1, x2]:
            if 0 <= t < w:
                p = (y * w + t) * 2
                buf[p] = lo; buf[p + 1] = hi


def detect_thread():
    global pl, cur_state, camera_paused, ai_running, stream_frame
    global gui_fps, gui_dirty, yolo_ref, shared_frame_565
    global shared_frame_raw, det_frame_565, det_frame_time
    global prev_det_classes, det_result_changed
    try:
        yolo = CornYOLO11(
            task_type="detect", mode="video",
            kmodel_path=KMODEL_PATH, labels=LABELS,
            rgb888p_size=RGB888P_SIZE, model_input_size=MODEL_INPUT_SIZE,
            display_size=DISPLAY_SIZE,
            conf_thresh=0.6, nms_thresh=0.45, max_boxes_num=50, debug_mode=0,
        )
        yolo.config_preprocess()
        yolo_ref = yolo
        clock = time.clock()

        while ai_running:
            if cur_state == 0 or camera_paused:
                try:
                    pl.osd_img.clear()
                    pl.show_image()
                    yolo.all_dets = []
                    gui_dirty = True
                except: pass
                while ai_running and (cur_state == 0 or camera_paused):
                    time.sleep_ms(100)
                if not ai_running: break
                clock = time.clock()

            clock.tick()
            clock.tick()

            # Crop switch
            if crop_switch_to >= 0 and crop_switch_to in CROP_CONFIGS:
                _cfg = CROP_CONFIGS[crop_switch_to]
                yolo_ref = None
                try: yolo.deinit()
                except: pass
                LABELS = _cfg["labels"]; LABELS_LIST = _cfg["labels_list"]; LABELS_EXCLUDE = _cfg["labels_exclude"]; KMODEL_PATH = _cfg["kmodel_path"]
                gc.collect(); time.sleep_ms(200)
                yolo = CornYOLO11(task_type="detect", mode="video", kmodel_path=KMODEL_PATH, labels=LABELS_LIST, rgb888p_size=RGB888P_SIZE, model_input_size=MODEL_INPUT_SIZE, display_size=DISPLAY_SIZE, conf_thresh=0.6, nms_thresh=0.45, max_boxes_num=50, debug_mode=0)
                yolo.config_preprocess(); yolo_ref = yolo; crop_mode = crop_switch_to; crop_switch_to = -1
                prev_det_classes = set(); gui_dirty = True
                print("[CROP] Switched to: %s" % ("CORN" if crop_mode == CROP_CORN else "POTATO"))
            try:
                img = pl.get_frame()

                if current_mode == MODE_PEST:
                    # PEST mode: YOLO pest detection
                    res = yolo.run(img)
                    yolo.draw_result(res, pl.osd_img)
                    for det in yolo.all_dets:
                        try:
                            if int(det[5]) < len(LABELS_LIST) and LABELS_LIST[int(det[5])] in LABELS_EXCLUDE:
                                _x1, _y1 = int(det[0]), int(det[1])
                                _w, _h = max(1, int(det[2]) - _x1), max(1, int(det[3]) - _y1)
                                pl.osd_img.draw_rectangle(_x1, _y1, _w, _h, color=(0,0,0,0), fill=True)
                        except: pass
                    gui_dets = [d for d in yolo.all_dets if int(d[5]) < len(LABELS_LIST) and LABELS_LIST[int(d[5])] not in LABELS_EXCLUDE]

                    # TRACK mode: person detection + tracking
                    gui_dets = None
                    if person_det is not None and person_tracker is not None:
                        try:
                            raw_dets = person_det.run(img)
                            person_det.draw_result(raw_dets, pl.osd_img)
                            trk_dets = []
                            if raw_dets:
                                bboxes, cls_ids, scores = raw_dets[0], raw_dets[1], raw_dets[2]
                                for i in range(len(bboxes)):
                                    bbox = bboxes[i]
                                    trk_dets.append(((int(bbox[0]), int(bbox[1]), int(bbox[2]), int(bbox[3])), float(scores[i]), int(cls_ids[i])))
                            tr = person_tracker.update(trk_dets)
                            track_result = tr
                            if not tr["found"]:
                                pl.osd_img.clear()
                                pl.osd_img.draw_string_advanced(10, 10, 32, "TARGET LOST", color=(255, 255, 0, 0))
                        except Exception as e:
                            print("[TRACK] Error:", e)

                # RGB565 capture for video stream
                try:
                    _arr = img
                    _ih = _arr.shape[1]
                    _iw = _arr.shape[2]
                    _buf = bytearray(160 * 120 * 2)
                    _idx = 0
                    for _y in range(0, _ih, 3):
                        _rr = _arr[0][_y]
                        _rg = _arr[1][_y]
                        _rb = _arr[2][_y]
                        for _x in range(0, _iw, 4):
                            _v = ((int(_rr[_x]) & 0xF8) << 8) | ((int(_rg[_x]) & 0xFC) << 3) | (int(_rb[_x]) >> 3)
                            _buf[_idx] = _v & 0xFF
                            _buf[_idx + 1] = (_v >> 8) & 0xFF
                            _idx += 2
                    shared_frame_565 = _buf
                except: pass

                # Track detection result changes for screenshot
                _cur = set()
                for det in yolo.all_dets:
                    try:
                        _c = int(det[5])
                        if _c < len(LABELS_LIST) and LABELS_LIST[_c] not in LABELS_EXCLUDE:
                            _cur.add(_c)
                    except: pass
                if _cur and _cur != prev_det_classes:
                    prev_det_classes = _cur
                    det_result_changed = True
                    print("[DET] New: %s" % str([LABELS_LIST[c] for c in _cur]))
                    # Screenshot: copy live RGB565 + draw detection boxes
                    try:
                        _src = shared_frame_565
                        if _src and len(_src) == 160 * 120 * 2:
                            _out = bytearray(_src)
                            _sx = 160.0 / 640.0
                            _sy = 120.0 / 360.0
                            for det in yolo.all_dets:
                                try:
                                    _c = int(det[5])
                                    if _c < len(LABELS_LIST) and LABELS_LIST[_c] in LABELS_EXCLUDE:
                                        continue
                                    _x1 = max(0, min(159, int(det[0] * _sx)))
                                    _y1 = max(0, min(119, int(det[1] * _sy)))
                                    _x2 = max(0, min(159, int(det[2] * _sx)))
                                    _y2 = max(0, min(119, int(det[3] * _sy)))
                                    _draw_screenshot_rect(_out, _x1, _y1, _x2, _y2, 0x07E0)
                                except: pass
                            det_frame_565 = _out
                            print("[DET] Screenshot captured")
                    except Exception as _fe:
                        print("[DET] Screenshot err:", _fe)
                elif not _cur and prev_det_classes:
                    prev_det_classes = set()
            except Exception as e:
                print("[AI] Error: {}".format(e))

        yolo.deinit()
        yolo_ref = None
    except Exception as e:
        sys.print_exception(e)
    cur_state = 0
    ai_running = False

# Button scan
def button_scan_thread():
    global btn_action
    while True:
        if button_is_pressed():
            time.sleep_ms(50)
            if button_is_pressed():
                start = time.ticks_ms()
                while button_is_pressed(): time.sleep_ms(20)
                hold = time.ticks_diff(time.ticks_ms(), start)
                btn_action = 2 if hold >= BTN_LONG_PRESS_MS else 1
        time.sleep_ms(50)



# ==================== Video Streaming ====================
VIDEO_STREAM_W = 160
VIDEO_STREAM_H = 120
VIDEO_DEST_IP = "192.168.1.10"
VIDEO_DEST_PORT = 5001
video_sock = None
video_connected = False

def video_thread():
    import network, socket
    global video_sock, video_connected, shared_frame_565
    print("[VIDEO] Thread started, initializing LAN...")
    try:
        lan = network.LAN()
        time.sleep(2)
        if not lan.isconnected():
            for _ in range(5):
                time.sleep(1)
                if lan.isconnected(): break
        if lan.isconnected():
            print("[VIDEO] LAN connected:", lan.ifconfig())
        else:
            lan.ifconfig(("192.168.1.100", "255.255.255.0", "192.168.1.1", "192.168.1.1"))
            print("[VIDEO] LAN static IP:", lan.ifconfig())
    except Exception as e:
        print("[VIDEO] LAN init error:", e)
    while True:
        try:
            if pl is not None: break
        except: pass
        time.sleep(0.5)
    time.sleep(2)
    print("[VIDEO] Connecting to %s:%d" % (VIDEO_DEST_IP, VIDEO_DEST_PORT))
    CHUNK_SIZE = 1400
    frame_id = 0; frame_count = 0; fps = 0; fps_tick = time.ticks_ms()
    while True:
        try:
            if video_sock is None:
                try:
                    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    s.settimeout(5)
                    s.connect((VIDEO_DEST_IP, VIDEO_DEST_PORT))
                    s.settimeout(None)
                    video_sock = s; video_connected = True
                    print("[VIDEO] Connected to CH32!")
                except Exception as e:
                    print("[VIDEO] Connect failed:", e)
                    if video_sock:
                        try: video_sock.close()
                        except: pass
                    video_sock = None; video_connected = False
                    time.sleep(2); continue
            buf = shared_frame_565
            if buf is None or len(buf) != VIDEO_STREAM_W * VIDEO_STREAM_H * 2:
                time.sleep_ms(50); continue
            total = len(buf)
            num_chunks = (total + CHUNK_SIZE - 1) // CHUNK_SIZE
            frame_id = (frame_id + 1) & 0xFFFF
            send_ok = True
            for i in range(num_chunks):
                offset = i * CHUNK_SIZE; end = min(offset + CHUNK_SIZE, total)
                chunk = buf[offset:end]; chunk_len = len(chunk)
                payload = bytearray(10 + chunk_len)
                payload[0] = frame_id & 0xFF; payload[1] = (frame_id >> 8) & 0xFF
                payload[2] = i & 0xFF; payload[3] = (i >> 8) & 0xFF
                payload[4] = num_chunks & 0xFF; payload[5] = (num_chunks >> 8) & 0xFF
                payload[6] = chunk_len & 0xFF; payload[7] = (chunk_len >> 8) & 0xFF
                payload[8] = (chunk_len >> 16) & 0xFF; payload[9] = (chunk_len >> 24) & 0xFF
                payload[10:] = chunk
                data_len = len(payload)
                frame = bytearray(8 + data_len)
                frame[0] = 0xAA; frame[1] = 0x55; frame[2] = 0x10
                frame[3] = data_len & 0xFF; frame[4] = (data_len >> 8) & 0xFF
                frame[5:5+data_len] = payload
                chk = 0xAA ^ 0x55 ^ 0x10 ^ (data_len & 0xFF) ^ ((data_len >> 8) & 0xFF)
                for b in payload: chk ^= b
                frame[5+data_len] = chk; frame[6+data_len] = 0x55; frame[7+data_len] = 0xAA
                try: video_sock.send(bytes(frame))
                except Exception as e:
                    print("[VIDEO] Send error:", e); send_ok = False; break
            if not send_ok:
                try: video_sock.close()
                except: pass
                video_sock = None; video_connected = False
                print("[VIDEO] Connection lost, reconnecting...")
                time.sleep(1); continue
            frame_count += 1
            now = time.ticks_ms()
            if time.ticks_diff(now, fps_tick) >= 1000:
                fps = frame_count; frame_count = 0; fps_tick = now
                print("[VIDEO] %d fps, %d chunks, %dB" % (fps, num_chunks, total))
            time.sleep_ms(20)
        except Exception as e:
            print("[VIDEO] Err:", type(e).__name__, e); time.sleep(0.5)
def main():
    global pl, cur_state, ai_running, status_label, btn_action
    global _btn_last_blink, _btn_blink_on, camera_paused, uart_tx_enabled
    global gui_fps, gui_dirty, crop_mode, crop_switch_to, current_mode
    os.exitpoint(os.EXITPOINT_ENABLE)

    print("=" * 50)
    print("  Corn Disease Detection - LVGL v3")
    print("=" * 50)

    try:
        gc.collect(); time.sleep_ms(100)

        print("[INIT] PipeLine...")
        pl = PipeLine(rgb888p_size=RGB888P_SIZE, display_size=DISPLAY_SIZE, display_mode="st7701")
        pl.create(sensor=Sensor(width=1920, height=1080))

        print("[INIT] LVGL...")
        lvgl_init()
        gui_init()
        button_init()

        try:
            uart_comm = UartComm()
            uart_comm.init()
            print("[UART] Init OK")
        except: uart_comm = None

        _thread.start_new_thread(button_scan_thread, ())
        for _ in range(3): led_blink_once(100); time.sleep_ms(100)

        # Start AI thread (runs once, stays alive)
        ai_running = True
        cur_state = 1
        _thread.start_new_thread(detect_thread, ())
        try:
            _thread.start_new_thread(video_thread, ())
            print("[INIT] Video stream thread started")
        except Exception as e:
            print("[INIT] Video thread error:", e)
        try:
            _thread.start_new_thread(video_thread, ())
            print("[INIT] Video stream thread started")
        except Exception as e:
            print("[INIT] Video thread error:", e)
        if status_label:
            status_label.set_text("Running...")
            status_label.set_style_text_color(lv.color_hex(0x00ff00), 0)
        print("[INIT] Detection started")
        try:
            _thread.start_new_thread(video_thread, ())
            print("[INIT] Video stream thread started")
        except Exception as e:
            print("[INIT] Video thread error:", e)

        # Main loop: LVGL + button + UART
        while True:
            os.exitpoint()
            # Button
            if btn_action != 0:
                action = btn_action; btn_action = 0
                if action == 1:  # short: toggle AI
                    if camera_paused:
                        print("[BTN] Resume camera first")
                    elif cur_state == 0:
                        cur_state = 1
                        if status_label:
                            status_label.set_text("Running...")
                            status_label.set_style_text_color(lv.color_hex(0x00ff00), 0)
                    elif cur_state == 1:
                        cur_state = 0
                        if status_label:
                            status_label.set_text("AI Paused")
                            status_label.set_style_text_color(lv.color_hex(0xffaa00), 0)
                elif action == 2:  # long: toggle camera
                    camera_paused = not camera_paused
                    if camera_paused:
                        cur_state = 0
                        if status_label:
                            status_label.set_text("Camera Paused")
                            status_label.set_style_text_color(lv.color_hex(0xff5500), 0)
                    else:
                        if status_label:
                            status_label.set_text("Camera Resumed")
                            status_label.set_style_text_color(lv.color_hex(0x00aaff), 0)

            # UART
            if uart_comm:
                cmd = uart_comm.check_command()
                if cmd == CMD_START and cur_state == 0 and not camera_paused:
                    cur_state = 1
                    if status_label:
                        status_label.set_text("Running...")
                        status_label.set_style_text_color(lv.color_hex(0x00ff00), 0)
                elif cmd == CMD_STOP and cur_state == 1:
                    cur_state = 0
                    if status_label:
                        status_label.set_text("AI Paused")
                        status_label.set_style_text_color(lv.color_hex(0xffaa00), 0)
                elif cmd == CMD_MUTE_TX:
                    uart_tx_enabled = not uart_tx_enabled
                    print("[UART] TX %s" % ("OFF" if not uart_tx_enabled else "ON"))
                elif cmd == CMD_MODE_PEST:
                    current_mode = MODE_PEST
                    set_status("PEST Mode", 0x00AAFF)
                    print("[MODE] PEST")
                elif cmd == CMD_MODE_TRACK:
                    if _ensure_person_detector():
                        current_mode = MODE_TRACK
                        set_status("Track Mode", 0x00AAFF)
                        print("[MODE] TRACK")
                    else:
                        set_status("Track FAIL", 0xFF0000)
                elif cmd == CMD_SWITCH_CROP:
                    if crop_switch_to < 0:
                        crop_switch_to = CROP_POTATO if crop_mode == CROP_CORN else CROP_CORN
                        print("[CROP] Switching")
            # Mode-aware UART TX
            if uart_comm and uart_tx_enabled and cur_state == 1:
                if current_mode == MODE_TRACK and track_result is not None:
                    tr = track_result
                    if tr.get('found', False):
                        uart_comm.send_track_data(
                            int(tr['x']), int(tr['y']), int(tr['w']), int(tr['h']),
                            int(tr['dx']), int(tr['dy']),
                            int(tr['confidence'] * 100), int(tr['track_id']), int(tr['status'])
                        )
                elif current_mode == MODE_PEST:
                    _dets = yolo_ref.all_dets if yolo_ref else []
                    if _dets:
                        best = _dets[0]
                        uart_comm.send_pest_data(int(best[5]), int(best[4]*100), len(_dets), crop_mode)
                    else:
                        uart_comm.send_pest_data(0, 0, 0, crop_mode)



            # LED
            if camera_paused:
                now = time.ticks_ms()
                if time.ticks_diff(now, _btn_last_blink) >= 1000:
                    _btn_blink_on = not _btn_blink_on; led_set(_btn_blink_on)
                    _btn_last_blink = now
            elif cur_state == 1:
                led_set(True)
            else:
                now = time.ticks_ms()
                if time.ticks_diff(now, _btn_last_blink) >= 500:
                    _btn_blink_on = not _btn_blink_on; led_set(_btn_blink_on)
                    _btn_last_blink = now

            # LVGL
            update_gui()
            lv.task_handler()
            time.sleep_ms(5)

    except KeyboardInterrupt:
        print("Interrupted")
    except Exception as e:
        print("[MAIN] Error:", e)
    finally:
        cur_state = 0; camera_paused = False; uart_tx_enabled = True
        stream_frame = None; shared_frame_565 = None; ai_running = False
        det_frame_565 = None; det_frame_time = 0; prev_det_classes = set()
        crop_mode = CROP_CORN; crop_switch_to = -1
        global video_sock
        if video_sock:
            try: video_sock.close()
            except: pass
        video_sock = None
        time.sleep_ms(500); led_set(False)
        if uart_comm: uart_comm.deinit()
        lvgl_deinit()
        if pl: pl.destroy()
        gc.collect()
        print("[INFO] Done")

if __name__ == "__main__":
    main()
