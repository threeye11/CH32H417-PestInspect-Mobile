"""
Corn Disease Detection - LVGL v3
Detection: PipeLine + CornYOLO11 (based on demo-camera.py)
"""

from libs.PipeLine import PipeLine
from libs.YOLO import YOLO11
from libs.Utils import *
from media.display import *
from media.media import *
from media.vencoder import StreamData
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
DET_HOLD_MS = 5000

# Crop configurations
CROP_CORN = 0
CROP_SUGARCANE = 1
CROP_TOMATO = 2
CROP_CONFIGS = {
    CROP_CORN: {
        "labels": {0: "玉米非生物胁迫病", 1: "玉米蚜虫害", 2: "玉米弯孢霉叶斑病",
                   3: "泥土", 4: "玉米大斑病", 5: "玉米健康叶",
                   6: "玉米锈病", 7: "草地贪夜蛾幼虫", 8: "草地贪夜蛾叶片",
                   9: "玉米条纹病", 10: "杂草"},
        "labels_list": ["玉米非生物胁迫病", "玉米蚜虫害", "玉米弯孢霉叶斑病",
                        "泥土", "玉米大斑病", "玉米健康叶",
                        "玉米锈病", "草地贪夜蛾幼虫", "草地贪夜蛾叶片",
                        "玉米条纹病", "杂草"],
        "labels_exclude": ("杂草", "泥土"),
        "kmodel_path": "/sdcard/yolo11s_det_320.kmodel",
        "conf_thresh": 0.6,
    },
    CROP_SUGARCANE: {
        "labels": {0: "甘蔗健康叶片", 1: "甘蔗白叶病", 2: "甘蔗叶灼病", 3: "甘蔗红腐病"},
        "labels_list": ["甘蔗健康叶片", "甘蔗白叶病", "甘蔗叶灼病", "甘蔗红腐病"],
        "labels_exclude": (),
        "kmodel_path": "/sdcard/Sugarcane_det_320.kmodel",
        "conf_thresh": 0.6,
    },
    CROP_TOMATO: {
        "labels": {0: "番茄脐腐病", 1: "番茄镰刀菌枯萎病", 2: "番茄健康果实",
                   3: "番茄健康叶片", 4: "番茄棉铃虫虫害", 5: "番茄叶斑病",
                   6: "番茄叶螨虫害", 7: "番茄氮素过剩病", 8: "番茄日灼病"},
        "labels_list": ["番茄脐腐病", "番茄镰刀菌枯萎病", "番茄健康果实",
                        "番茄健康叶片", "番茄棉铃虫虫害", "番茄叶斑病",
                        "番茄叶螨虫害", "番茄氮素过剩病", "番茄日灼病"],
        "labels_exclude": (),
        "kmodel_path": "/sdcard/tomato_det_320.kmodel",
        "conf_thresh": 0.6,
    },
}
CROP_NAMES = {CROP_CORN: 'corn', CROP_SUGARCANE: 'sugarcane', CROP_TOMATO: 'tomato'}

LABELS = CROP_CONFIGS[CROP_CORN]["labels"]
LABELS_LIST = CROP_CONFIGS[CROP_CORN]["labels_list"]
LABELS_EXCLUDE = CROP_CONFIGS[CROP_CORN]["labels_exclude"]

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
title_label = None
font_cn = None
btn_pin = None
led_pin = None
camera_paused = False
stream_frame = None; shared_frame_565 = None
uart_tx_enabled = True
crop_mode = CROP_CORN
crop_switch_to = -1
shared_frame_raw = None
det_frame_565 = None; det_frame_time = 0
prev_det_classes = set(); det_result_changed = False
prev_pest_result = None; prev_track_sig = None
_image_sending = False

# ==================== Person Tracking ====================
MODE_PEST = 0
MODE_TRACK = 1
current_mode = MODE_PEST
track_result = None
person_det = None
person_tracker = None
CMD_MODE_PEST = 0x05
CMD_MODE_TRACK = 0x06
TYPE_TRACK = 0x03
MOTION_CMD = 0x04
CHASSIS_STOP_CMD = 0x11
K_CM_PER_PIXEL = 0.1
CHASSIS_DEAD_ZONE = 15  # pixels
CHASSIS_L_DEAD = 5  # cm
CHASSIS_ALPHA = 0.2  # low-pass filter coefficient
CHASSIS_MIN_COUNT = 2  # consecutive valid frames required
CHASSIS_INTERVAL_MS = 100  # 10Hz control rate
PERSON_KMODEL_PATH = "/sdcard/person_detect_yolo11n.kmodel"

# Servo state machine
from src.hardware.state_machine import get_state_machine, STATE_TRACKING, STATE_SEARCHING, STATE_IDLE

# ==================== Search State ====================
SEARCH_SCAN_CFG = {
    'pan_range':        (-60, 60),
    'tilt_range':       (-40, 10),
    'pan_step':         30,
    'tilt_step':        20,
    'move_estimate_ms': 300,
    'settle_ms':        500,
}

HFOV_DEG = 60.0
VFOV_DEG = 45.0
BLACKLIST_THRESHOLD_DEG = 5.0

# Search shared state (module globals)
system_state = STATE_TRACKING
blacklist_angle = None          # (pan_abs, tilt_abs) or None
blacklist_version = 0           # incremented on blacklist change
blacklist_version_processed = 0 # set by detect thread
search_result = None            # {'found': bool, 'bbox': tuple, 'p_abs': float, 't_abs': float} or None
search_waypoints = []           # [(pan, tilt), ...]
search_waypoint_idx = 0
search_phase = 0                # 0=zero_delay, 1=move, 2=settle
search_phase_start = 0          # ticks_ms for current phase
_SEARCH_PHASE_ZERO_DELAY = 0
_SEARCH_PHASE_MOVE = 1
_SEARCH_PHASE_SETTLE = 2

# Chassis filter state
_chs_filt_dx = 0.0; _chs_filt_dy = 0.0; _chs_filt_l = 0.0
_chs_valid_cnt = 0; _chs_last_tx_ms = 0

# UART
from machine import UART
from machine import FPIOA as _FPIOA
HEADER1 = 0xAA; HEADER2 = 0x55; FOOTER1 = 0x55; FOOTER2 = 0xAA
DATA_LEN = 8; TX_FRAME_LEN = 15; TYPE_PEST = 0x01
CMD_START = 0x01; CMD_STOP = 0x02; CMD_MUTE_TX = 0x03; CMD_SWITCH_CROP = 0x04
CMD_CROP_CORN = 0x0D; CMD_CROP_SUGARCANE = 0x0E; CMD_CROP_TOMATO = 0x0F
CMD_SERVO_HOME = 0x07; CMD_SERVO_LEFT = 0x08; CMD_SERVO_RIGHT = 0x09
CMD_SERVO_DOWN = 0x0A; CMD_SERVO_UP = 0x0B; CMD_SERVO_PATROL = 0x0C
TYPE_IMAGE = 0x10; CHUNK_SIZE = 200

def _compute_abs_angles(det_bbox, servo_pan, servo_tilt, img_w, img_h):
    """Convert detection bbox center + current servo angles to absolute world angles."""
    x1, y1, x2, y2 = int(det_bbox[0]), int(det_bbox[1]), int(det_bbox[2]), int(det_bbox[3])
    cx = (x1 + x2) / 2.0
    cy = (y1 + y2) / 2.0
    center_x = img_w / 2.0
    center_y = img_h / 2.0
    dx_px = cx - center_x
    dy_px = cy - center_y
    offset_pan = dx_px / float(img_w) * HFOV_DEG
    offset_tilt = dy_px / float(img_h) * VFOV_DEG
    p_abs = servo_pan + offset_pan
    t_abs = servo_tilt + offset_tilt
    return (p_abs, t_abs)

def _blacklist_match(p_new, t_new):
    """Return True if (p_new, t_new) matches the blacklisted target within threshold."""
    if blacklist_angle is None:
        return False
    dp = p_new - blacklist_angle[0]
    dt = t_new - blacklist_angle[1]
    dist = (dp * dp + dt * dt) ** 0.5
    return dist < BLACKLIST_THRESHOLD_DEG

def _compute_search_waypoints():
    """Pre-compute comb-scan waypoints from SEARCH_SCAN_CFG. Returns list of (pan, tilt)."""
    cfg = SEARCH_SCAN_CFG
    pan_min, pan_max = cfg['pan_range']
    tilt_min, tilt_max = cfg['tilt_range']
    pan_step = cfg['pan_step']
    tilt_step = cfg['tilt_step']

    waypoints = []
    tilt = tilt_max - tilt_step / 2.0
    direction = 1  # 1 = left-to-right, -1 = right-to-left
    while tilt >= tilt_min + tilt_step / 2.0 - 0.01:
        if direction == 1:
            pan = pan_min + pan_step / 2.0
            while pan <= pan_max - pan_step / 2.0 + 0.01:
                waypoints.append((float(pan), float(tilt)))
                pan += pan_step
        else:
            pan = pan_max - pan_step / 2.0
            while pan >= pan_min + pan_step / 2.0 - 0.01:
                waypoints.append((float(pan), float(tilt)))
                pan -= pan_step
        tilt -= tilt_step
        direction = -direction
    return waypoints

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
        self.last_time=time.ticks_ms() if hasattr(time,"ticks_ms") else int(time.time()*1000)

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
        t=self.select(dets); ts=int(time.ticks_ms()) if hasattr(time,"ticks_ms") else int(time.time()*1000)
        if t:
            x,y,w,h,c=t; cx=x+w//2; cy=y+h//2
            if self._was_lost: self._tid=(self._tid+1)&0xFF or 1; self._was_lost=False
            if not self._kf_pos.initialized: self._kf_pos.initialize(cx,cy); self._kf_size.initialize(w,h)
            else:
                ct=time.ticks_ms() if hasattr(time,"ticks_ms") else int(time.time()*1000)
                dt=(ct-self._kf_pos.last_time)/1000.0; dt=max(dt,0.033)
                self._kf_pos.predict(dt); self._kf_size.predict(dt)
                self._kf_pos.update(cx,cy); self._kf_size.update(w,h)
                self._kf_pos.last_time=ct; self._kf_size.last_time=ct
            sx,sy=self._kf_pos.get_smoothed_position(); sw,sh=self._kf_size.get_smoothed_position()
            sx=int(sx-sw/2); sy=int(sy-sh/2); sw=max(10,int(sw)); sh=max(10,int(sh))
            sx=max(0,min(sx,640-sw)); sy=max(0,min(sy,360-sh))
            dx=int(sx+sw/2-self._cx); dy=int(sy+sh/2-self._cy)
            self._lost=0
            return {"found":True,"x":sx,"y":sy,"w":sw,"h":sh,"dx":dx,"dy":dy,"confidence":c,"track_id":self._tid,"status":1,"timestamp":ts&0xFFFF}
        self._lost+=1
        if self._lost>self._max_lost:
            self._kf_pos.reset(); self._kf_size.reset(); self._was_lost=True
            return {"found":False,"x":0,"y":0,"w":0,"h":0,"dx":0,"dy":0,"confidence":0,"track_id":self._tid,"status":0,"timestamp":ts&0xFFFF}
        ct=time.ticks_ms() if hasattr(time,"ticks_ms") else int(time.time()*1000)
        dt=(ct-self._kf_pos.last_time)/1000.0; dt=max(dt,0.033)
        self._kf_pos.predict(dt); self._kf_size.predict(dt)
        self._kf_pos.last_time=ct; self._kf_size.last_time=ct
        sx,sy=self._kf_pos.get_smoothed_position(); sw,sh=self._kf_size.get_smoothed_position()
        sx=int(sx-sw/2); sy=int(sy-sh/2); sw=max(10,int(sw)); sh=max(10,int(sh))
        dx=int(sx+sw/2-self._cx); dy=int(sy+sh/2-self._cy)
        return {"found":True,"x":sx,"y":sy,"w":sw,"h":sh,"dx":dx,"dy":dy,"confidence":0,"track_id":self._tid,"status":1,"timestamp":ts&0xFFFF}

def _ensure_person_detector():
    global person_det, person_tracker
    if person_det is not None: return True
    try:
        from libs.YOLO import YOLO11
        gc.collect(); time.sleep_ms(100)
        print("[TRACK] Loading person model...")
        pd = YOLO11(task_type="detect", mode="video", kmodel_path=PERSON_KMODEL_PATH, labels={0: "person"}, rgb888p_size=RGB888P_SIZE, model_input_size=[320, 320], display_size=DISPLAY_SIZE, conf_thresh=0.6, nms_thresh=0.45, max_boxes_num=50, debug_mode=0)
        pd.config_preprocess()
        person_det = pd; person_tracker = PersonTracker()
        print("[TRACK] Person detector ready")
        return True
    except Exception as e:
        print("[TRACK] Person init error:", e)
        person_det = None; person_tracker = None
        return False

class UartComm:
    def __init__(self):
        self._uart = None; self._ok = False; self._rbuf = bytearray(16); self._rlen = 0; self._frame_id = 0
    def init(self):
        fp = _FPIOA()
        fp.set_function(11, _FPIOA.UART2_TXD)
        fp.set_function(12, _FPIOA.UART2_RXD)
        self._uart = UART(UART.UART2, baudrate=921600, bits=UART.EIGHTBITS, parity=UART.PARITY_NONE, stop=UART.STOPBITS_ONE)
        self._ok = True
        return self
    def send_pest_data(self, pest_type, confidence, count, crop_type=0):
        if not self._ok: return
        data = bytearray(DATA_LEN)
        data[0] = pest_type & 0xFF; data[1] = confidence & 0xFF
        data[2] = count & 0xFF; data[3] = (count >> 8) & 0xFF
        chk = HEADER1 ^ HEADER2 ^ TYPE_PEST ^ DATA_LEN
        data[4] = crop_type & 0xFF
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

    def _stuff_payload(self, raw):
        """Byte-stuff: 0xAA -> 0xAA 0x00, prevents false header in payload."""
        out = bytearray()
        for b in raw:
            out.append(b)
            if b == 0xAA:
                out.append(0x00)
        return out

    def send_image_frame(self, frame_buf):
        if not self._ok or frame_buf is None: return
        total = len(frame_buf)
        if total == 0: return
        fid = self._frame_id; self._frame_id = (self._frame_id + 1) & 0xFFFF
        nc = (total + CHUNK_SIZE - 1) // CHUNK_SIZE
        for ci in range(nc):
            off = ci * CHUNK_SIZE; end = min(off + CHUNK_SIZE, total)
            pixel = frame_buf[off:end]; cs = len(pixel)
            # Build raw payload: metadata(10) + pixel data
            raw = bytearray(10 + cs)
            raw[0] = fid & 0xFF; raw[1] = (fid >> 8) & 0xFF
            raw[2] = ci & 0xFF; raw[3] = (ci >> 8) & 0xFF
            raw[4] = nc & 0xFF; raw[5] = (nc >> 8) & 0xFF
            raw[6] = cs & 0xFF; raw[7] = (cs >> 8) & 0xFF
            raw[8] = (cs >> 16) & 0xFF; raw[9] = (cs >> 24) & 0xFF
            raw[10:] = pixel
            # Byte-stuff entire payload (0xAA -> 0xAA 0xAA)
            stuffed = self._stuff_payload(raw)
            dl = len(stuffed)
            frm = bytearray(8 + dl)
            frm[0] = HEADER1; frm[1] = HEADER2; frm[2] = TYPE_IMAGE
            frm[3] = dl & 0xFF; frm[4] = (dl >> 8) & 0xFF
            frm[5:5+dl] = stuffed
            chk = HEADER1 ^ HEADER2 ^ TYPE_IMAGE ^ (dl & 0xFF) ^ ((dl >> 8) & 0xFF)
            for b in stuffed: chk ^= b
            frm[5+dl] = chk; frm[6+dl] = FOOTER1; frm[7+dl] = FOOTER2
            try:
                self._uart.write(bytes(frm))
            except Exception as _ce:
                print("[TX] chunk err:", _ce)
                break
            time.sleep_ms(10)

    def send_feedback(self, event, data=0):
        """Send async BB 55 feedback frame: BB 55 <EVENT> <DATA> 55 BB"""
        if not self._ok:
            return
        try:
            frame = bytearray(6)
            frame[0] = 0xBB
            frame[1] = 0x55
            frame[2] = event & 0xFF
            frame[3] = data & 0xFF
            frame[4] = 0x55
            frame[5] = 0xBB
            self._uart.write(bytes(frame))
        except Exception as e:
            print('[FB TX] Error:', e)

    def check_command(self):
        if not self._ok: return 0
        try:
            n = self._uart.any()
            if n == 0: return 0
            if n > 8: n = 8
            chunk = self._uart.read(n)
            if chunk is None: return 0
        except: return 0
        # Append to ring buffer
        rb = self._rbuf
        rl = self._rlen
        for b in chunk:
            if rl >= len(rb):
                rl = 0  # overflow, reset
            rb[rl] = b
            rl += 1
        self._rlen = rl
        # Scan for complete frame: AA 55 cmd 55 AA (5 bytes)
        result = 0
        consumed = 0
        i = 0
        while i + 4 < self._rlen:
            if rb[i] == HEADER1 and rb[i+1] == HEADER2 and rb[i+3] == FOOTER1 and rb[i+4] == FOOTER2:
                _cmd = rb[i+2]
                if _cmd in (CMD_START, CMD_STOP, CMD_MUTE_TX, CMD_SWITCH_CROP, CMD_CROP_CORN, CMD_CROP_SUGARCANE, CMD_CROP_TOMATO, CMD_MODE_PEST, CMD_MODE_TRACK, CMD_SERVO_HOME, CMD_SERVO_LEFT, CMD_SERVO_RIGHT, CMD_SERVO_DOWN, CMD_SERVO_UP, CMD_SERVO_PATROL, 0x10, 0x11):
                    result = _cmd
                consumed = i + 5
                i += 5
                continue
            i += 1
        if consumed > 0:
            remaining = self._rlen - consumed
            if remaining > 0:
                for j in range(remaining):
                    rb[j] = rb[consumed + j]
            self._rlen = remaining
        elif self._rlen > 4:
            self._rlen = 0
        return result
    def send_chassis_data(self, dx, dy, theta, l_real):
        if not self._ok: return
        dx16 = max(-32768, min(32767, int(dx)))
        dy16 = max(-32768, min(32767, int(dy)))
        th16 = max(-32768, min(32767, int(theta * 100)))
        lr16 = max(0, min(65535, int(l_real)))
        data = bytearray([dx16 & 0xFF, (dx16 >> 8) & 0xFF, dy16 & 0xFF, (dy16 >> 8) & 0xFF, th16 & 0xFF, (th16 >> 8) & 0xFF, lr16 & 0xFF, (lr16 >> 8) & 0xFF])
        chk = HEADER1 ^ HEADER2 ^ MOTION_CMD ^ 8
        for b in data: chk ^= b
        frame = bytearray(TX_FRAME_LEN)
        frame[0] = HEADER1; frame[1] = HEADER2; frame[2] = MOTION_CMD; frame[3] = 8
        frame[4:12] = data; frame[12] = chk; frame[13] = FOOTER1; frame[14] = FOOTER2
        try: self._uart.write(bytes(frame))
        except: pass
    def send_chassis_stop(self):
        if not self._ok: return
        data = bytearray([CHASSIS_STOP_CMD, 0x00])
        chk = 0
        for b in data: chk ^= b
        data.append(chk)
        try: self._uart.write(bytes(data))
        except: pass
    def deinit(self):
        if self._uart:
            try: self._uart.deinit()
            except: pass
            self._uart = None
        self._ok = False

uart_comm = None

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
    global fps_label, count_label, result_label, status_label, title_label, font_cn
    # Load Chinese font (.fnt)
    _fnt = "/sdcard/res/font/lv_font_simsun_16_cjk.fnt"
    try:
        font_cn = lv.font_load("A:" + _fnt)
        print("[GUI] Chinese font loaded: " + _fnt)
    except Exception as e:
        print("[GUI] WARN: font load failed:", e)
        font_cn = None
    _f = font_cn
    scr = lv.scr_act(); scr.set_style_bg_opa(lv.OPA.TRANSP, lv.PART.MAIN)
    panel = lv.obj(lv.layer_sys())
    panel.set_size(180, 480); panel.set_pos(620, 0)
    panel.set_style_bg_color(lv.color_hex(0x1a1a2e), lv.PART.MAIN)
    panel.set_style_bg_opa(200, lv.PART.MAIN)
    panel.set_style_border_width(0, lv.PART.MAIN)
    panel.set_style_radius(0, lv.PART.MAIN)
    panel.clear_flag(lv.obj.FLAG.SCROLLABLE)
    title_label = lv.label(panel); title_label.set_text("玉米病害检测")
    title_label.set_style_text_color(lv.color_hex(0xe94560), 0)
    if _f: title_label.set_style_text_font(_f, 0)
    title_label.align(lv.ALIGN.TOP_MID, 0, 5)
    lb = lv.label(panel); lb.set_text("帧率")
    lb.set_style_text_color(lv.color_hex(0x53d8fb), 0)
    if _f: lb.set_style_text_font(_f, 0)
    lb.align(lv.ALIGN.TOP_MID, 0, 30)
    fps_label = lv.label(panel); fps_label.set_text("--")
    fps_label.set_style_text_color(lv.color_hex(0xffffff), 0); fps_label.align(lv.ALIGN.TOP_MID, 0, 50)
    lb2 = lv.label(panel); lb2.set_text("数量")
    lb2.set_style_text_color(lv.color_hex(0x53d8fb), 0)
    if _f: lb2.set_style_text_font(_f, 0)
    lb2.align(lv.ALIGN.TOP_MID, 0, 75)
    count_label = lv.label(panel); count_label.set_text("0")
    count_label.set_style_text_color(lv.color_hex(0xffd700), 0); count_label.align(lv.ALIGN.TOP_MID, 0, 95)
    line = lv.obj(panel); line.set_size(150, 2); line.align(lv.ALIGN.TOP_MID, 0, 120)
    line.set_style_bg_color(lv.color_hex(0x0f3460), 0); line.set_style_border_width(0, 0)
    lb3 = lv.label(panel); lb3.set_text("检测结果")
    lb3.set_style_text_color(lv.color_hex(0x53d8fb), 0)
    if _f: lb3.set_style_text_font(_f, 0)
    lb3.align(lv.ALIGN.TOP_MID, 0, 130)
    result_label = lv.label(panel); result_label.set_text("---")
    result_label.set_style_text_color(lv.color_hex(0xffffff), 0); result_label.set_width(160)
    if _f: result_label.set_style_text_font(_f, 0)
    result_label.align(lv.ALIGN.TOP_LEFT, 10, 155)
    status_label = lv.label(panel); status_label.set_text("启动中...")
    status_label.set_style_text_color(lv.color_hex(0xaaaaaa), 0)
    if _f: status_label.set_style_text_font(_f, 0)
    status_label.align(lv.ALIGN.BOTTOM_MID, 0, -10)
    lv.scr_load(scr)

def set_status(text, color_hex):
    try:
        if status_label:
            status_label.set_text(text)
            status_label.set_style_text_color(lv.color_hex(color_hex), 0)
            if font_cn: status_label.set_style_text_font(font_cn, 0)
    except: pass

def update_gui():
    global gui_fps, gui_dirty, current_mode, track_result, crop_mode
    try:
        fps_label.set_text("{:.1f}".format(gui_fps))
        if current_mode == MODE_TRACK:
            title_label.set_text("追踪目标")
        else:
            _crop_t = {CROP_CORN: "玉米病害检测", CROP_SUGARCANE: "甘蔗病虫检测", CROP_TOMATO: "番茄病害检测"}.get(crop_mode, "病害检测")
            title_label.set_text(_crop_t)
        if current_mode == MODE_TRACK:
            count_label.set_text("-")
            result_label.set_text("---")
            return
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
                result_label.set_text("无虫害")
        else:
            count_label.set_text("0")
            result_label.set_text("无虫害")
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
            # Reverse letterbox: top=0, padding all at bottom (Utils.py letterbox_pad_param)
            _scale = min(float(self.model_input_size[0]) / float(self.rgb888p_size[0]),
                         float(self.model_input_size[1]) / float(self.rgb888p_size[1]))
            _xoff = 0
            _yoff = 0
            dets = []
            for i in range(len(boxes)):
                if confs[i] > self.conf_thresh:
                    cx = float(boxes[i, 0]); cy = float(boxes[i, 1])
                    w = float(boxes[i, 2]); h = float(boxes[i, 3])
                    x1 = int((cx - 0.5 * w) / _scale)
                    y1 = int((cy - 0.5 * h) / _scale)
                    x2 = int((cx + 0.5 * w) / _scale)
                    y2 = int((cy + 0.5 * h) / _scale)
                    x1 = max(0, x1); y1 = max(0, y1)
                    x2 = min(self.rgb888p_size[0], x2)
                    y2 = min(self.rgb888p_size[1], y2)
                    if x2 > x1 and y2 > y1:  # filter padding-region detections
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
    """Draw rectangle outline on RGB565 80x60 buffer."""
    w = 80
    lo = color & 0xFF
    hi = (color >> 8) & 0xFF
    for x in range(x1, min(x2 + 1, w)):
        for t in [y1, y2]:
            if 0 <= t < 60:
                p = (t * w + x) * 2
                buf[p] = lo; buf[p + 1] = hi
    for y in range(y1, min(y2 + 1, 60)):
        for t in [x1, x2]:
            if 0 <= t < w:
                p = (y * w + t) * 2
                buf[p] = lo; buf[p + 1] = hi

def detect_thread():
    global pl, cur_state, camera_paused, ai_running, stream_frame
    global gui_fps, gui_dirty, yolo_ref, shared_frame_565
    global shared_frame_raw, det_frame_565, det_frame_time
    global prev_det_classes, det_result_changed
    global prev_pest_result, prev_track_sig, _image_sending
    global current_mode, person_det, person_tracker, track_result
    global crop_mode, crop_switch_to, LABELS, LABELS_LIST, LABELS_EXCLUDE, KMODEL_PATH
    global _chs_filt_dx, _chs_filt_dy, _chs_filt_l, _chs_valid_cnt, _chs_last_tx_ms
    global system_state, blacklist_angle, blacklist_version, blacklist_version_processed, search_result
    os.exitpoint(os.EXITPOINT_ENABLE)
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
                yolo.all_dets = []
                _idle_clock = time.clock()
                while ai_running and (cur_state == 0 or camera_paused):
                    try:
                        _idle_img = pl.get_frame()
                        pl.osd_img.clear()
                        pl.show_image()
                        gui_fps = _idle_clock.fps()
                        gui_dirty = True
                    except: pass
                    time.sleep_ms(30)
                if not ai_running: break
                clock = time.clock()

            # Crop switch
            if crop_switch_to >= 0 and crop_switch_to in CROP_CONFIGS:
                _cfg = CROP_CONFIGS[crop_switch_to]
                yolo_ref = None
                try:
                    yolo.deinit()
                except: pass
                LABELS = _cfg["labels"]
                LABELS_LIST = _cfg["labels_list"]
                LABELS_EXCLUDE = _cfg["labels_exclude"]
                KMODEL_PATH = _cfg["kmodel_path"]
                yolo = CornYOLO11(
                    task_type="detect", mode="video",
                    kmodel_path=KMODEL_PATH, labels=LABELS,
                    rgb888p_size=RGB888P_SIZE, model_input_size=MODEL_INPUT_SIZE,
                    display_size=DISPLAY_SIZE,
                    conf_thresh=_cfg.get("conf_thresh", 0.6), nms_thresh=0.45, max_boxes_num=50, debug_mode=0,
                )
                yolo.config_preprocess()
                yolo_ref = yolo
                crop_mode = crop_switch_to
                crop_switch_to = -1
                prev_det_classes = set()
                gui_dirty = True
                print("[CROP] Switched to: %s" % CROP_NAMES.get(crop_mode, "?"))

            clock.tick()
            try:
                img = pl.get_frame()
                shared_frame_raw = img

                # --- Search mode: override normal processing ---
                if system_state == STATE_SEARCHING:
                    # Sync blacklist version at start of frame
                    if blacklist_version > blacklist_version_processed:
                        blacklist_version_processed = blacklist_version

                    # Run detection based on current mode
                    _search_dets = []
                    if current_mode == MODE_TRACK and person_det is not None:
                        try:
                            _raw = person_det.run(img)
                            person_det.draw_result(_raw, pl.osd_img)
                            if _raw:
                                _bboxes, _cls_ids, _scores = _raw[0], _raw[1], _raw[2]
                                for _i in range(len(_bboxes)):
                                    _b = _bboxes[_i]
                                    _search_dets.append((int(_b[0]), int(_b[1]), int(_b[2]), int(_b[3]), float(_scores[_i]), int(_cls_ids[_i])))
                        except:
                            pass
                    elif current_mode == MODE_PEST:
                        try:
                            _search_res = yolo.run(img)
                            yolo.draw_result(_search_res, pl.osd_img)
                            for _d in yolo.all_dets:
                                _cid = int(_d[5])
                                if _cid < len(LABELS_LIST) and LABELS_LIST[_cid] in LABELS_EXCLUDE:
                                    continue
                                _search_dets.append((int(_d[0]), int(_d[1]), int(_d[2]), int(_d[3]), float(_d[4]), int(_d[5])))
                        except Exception as _e:
                            print("[SRCH] EXCEPTION: %s" % str(_e))

                    # Filter by blacklist, find first new target
                    _found_new = False
                    _sm = get_state_machine()
                    _servo_pan = _sm.auto_yaw
                    _servo_tilt = _sm.auto_pitch
                    for _d in _search_dets:
                        _p_abs, _t_abs = _compute_abs_angles(_d, _servo_pan, _servo_tilt, float(RGB888P_SIZE[0]), float(RGB888P_SIZE[1]))
                        _bl_hit = _blacklist_match(_p_abs, _t_abs)
                        if _bl_hit:
                            continue
                        search_result = {
                            'found': True,
                            'bbox': _d,
                            'p_abs': _p_abs,
                            't_abs': _t_abs
                        }
                        _found_new = True
                        break
                    if not _found_new:
                        if search_result is None:
                            search_result = {'found': False, 'bbox': None, 'p_abs': 0.0, 't_abs': 0.0}

                    pl.show_image()
                    stream_frame = pl.osd_img
                    gui_dirty = True
                    gui_fps = clock.fps()
                    continue  # skip normal PEST/TRACK processing below
                # --- End search mode ---

                _mode = current_mode
                if _mode == MODE_PEST:
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
                    # Auto servo tracking (PEST mode) - track largest target
                    if yolo.all_dets and system_state == STATE_TRACKING:
                        _at_sm = get_state_machine()
                        if _at_sm.work_mode == "AUTO":
                            _at_best = None; _at_max_area = 0
                            for _at_d in yolo.all_dets:
                                try:
                                    if int(_at_d[5]) < len(LABELS_LIST) and LABELS_LIST[int(_at_d[5])] in LABELS_EXCLUDE:
                                        continue
                                    _at_area = (_at_d[2] - _at_d[0]) * (_at_d[3] - _at_d[1])
                                    if _at_area > _at_max_area:
                                        _at_max_area = _at_area; _at_best = _at_d
                                except: pass
                            if _at_best:
                                _at_cx = (_at_best[0] + _at_best[2]) / 2.0
                                _at_cy = (_at_best[1] + _at_best[3]) / 2.0
                                _at_sm.auto_track_update(_at_cx, _at_cy, RGB888P_SIZE[0], RGB888P_SIZE[1])


                elif _mode == MODE_TRACK:
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
                            if tr["found"]:
                                _cx = tr["x"] + tr["w"] // 2
                                _cy = tr["y"] + tr["h"] // 2
                                _dx = _cx - RGB888P_SIZE[0] // 2
                                _dy = RGB888P_SIZE[1] // 2 - _cy
                                _pixel_dist = (_dx * _dx + _dy * _dy) ** 0.5
                                _theta = np.arctan2(float(_dy), float(_dx))
                                _l_real = K_CM_PER_PIXEL * _pixel_dist
                                # Low-pass filter (alpha=0.2)
                                _chs_filt_dx = _chs_filt_dx * (1.0 - CHASSIS_ALPHA) + float(_dx) * CHASSIS_ALPHA
                                _chs_filt_dy = _chs_filt_dy * (1.0 - CHASSIS_ALPHA) + float(_dy) * CHASSIS_ALPHA
                                _chs_filt_l  = _chs_filt_l  * (1.0 - CHASSIS_ALPHA) + float(_l_real) * CHASSIS_ALPHA
                                _chs_valid_cnt += 1
                                # Send at 10Hz + dead zone + min count
                                _now = time.ticks_ms()
                                if _chs_valid_cnt >= CHASSIS_MIN_COUNT and time.ticks_diff(_now, _chs_last_tx_ms) >= CHASSIS_INTERVAL_MS:
                                    _fdx = int(_chs_filt_dx) if abs(_chs_filt_dx) >= CHASSIS_DEAD_ZONE else 0
                                    _fdy = int(_chs_filt_dy) if abs(_chs_filt_dy) >= CHASSIS_DEAD_ZONE else 0
                                    _fl  = int(_chs_filt_l)  if abs(_chs_filt_l)  >= CHASSIS_L_DEAD   else 0
                                    _fth = np.arctan2(float(_fdy), float(_fdx)) if (_fdx != 0 or _fdy != 0) else 0.0
                                    if uart_comm:
                                        uart_comm.send_chassis_data(_fdx, _fdy, _fth, _fl)
                                    _chs_last_tx_ms = _now
                            else:
                                _chs_valid_cnt = 0
                                _trk_sm = get_state_machine()
                                _trk_sm.lost_tick()
                                pl.osd_img.draw_string_advanced(DISPLAY_WIDTH // 2 - 60, DISPLAY_HEIGHT // 2 - 12, 24, "TRACK LOST", color=(255, 255, 0, 0))
                                _now = time.ticks_ms()
                                if time.ticks_diff(_now, _chs_last_tx_ms) >= CHASSIS_INTERVAL_MS:
                                    if uart_comm:
                                        uart_comm.send_chassis_data(0, 0, 0.0, 0)
                                    _chs_last_tx_ms = _now
                            # Auto servo tracking in TRACK mode (only in TRACKING state)
                            if tr["found"] and system_state == STATE_TRACKING:
                                _trk_sm = get_state_machine()
                                _trk_sm.auto_track_update(float(_cx), float(_cy), float(RGB888P_SIZE[0]), float(RGB888P_SIZE[1]))
                        except Exception as e:
                            print("[TRACK] Error:", e)

                pl.show_image()
                stream_frame = pl.osd_img
                gui_dirty = True
                gui_fps = clock.fps()

                if _mode != MODE_TRACK:
                    # RGB565 capture: numpy subsample (640x360 -> 80x60)
                    try:
                        _arr = img
                        _ih = _arr.shape[1]; _iw = _arr.shape[2]
                        _SH = 60; _SW = 80
                        _R = _arr[0][::6, ::8]
                        _G = _arr[1][::6, ::8]
                        _B = _arr[2][::6, ::8]
                        _buf = bytearray(_SH * _SW * 2)
                        _idx = 0
                        for _i in range(_SH):
                            for _j in range(_SW):
                                _v = ((_R[_i][_j] & 0xF8) << 8) | ((_G[_i][_j] & 0xFC) << 3) | (_B[_i][_j] >> 3)
                                _buf[_idx] = _v & 0xFF
                                _buf[_idx + 1] = (_v >> 8) & 0xFF
                                _idx += 2
                        shared_frame_565 = _buf
                    except Exception as _e:
                        print("[RGB565] numpy err:", type(_e).__name__, _e)
                        try:
                            _buf2 = bytearray(_SW * _SH * 2); _idx2 = 0
                            for _y in range(0, _ih, 6):
                                _rr = _arr[0][_y]; _rg = _arr[1][_y]; _rb = _arr[2][_y]
                                for _x in range(0, _iw, 8):
                                    _v = ((int(_rr[_x]) & 0xF8) << 8) | ((int(_rg[_x]) & 0xFC) << 3) | (int(_rb[_x]) >> 3)
                                    _buf2[_idx2] = _v & 0xFF; _buf2[_idx2 + 1] = (_v >> 8) & 0xFF; _idx2 += 2
                            shared_frame_565 = _buf2
                        except Exception as _e2:
                            print("[RGB565] fallback err:", type(_e2).__name__, _e2)
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
                            if _src and len(_src) == 80 * 60 * 2:
                                _out = bytearray(_src)
                                _sx = 80.0 / 640.0
                                _sy = 60.0 / 360.0
                                for det in yolo.all_dets:
                                    try:
                                        _c = int(det[5])
                                        if _c < len(LABELS_LIST) and LABELS_LIST[_c] in LABELS_EXCLUDE:
                                            continue
                                        _x1 = max(0, min(79, int(det[0] * _sx)))
                                        _y1 = max(0, min(59, int(det[1] * _sy)))
                                        _x2 = max(0, min(79, int(det[2] * _sx)))
                                        _y2 = max(0, min(59, int(det[3] * _sy)))
                                        _draw_screenshot_rect(_out, _x1, _y1, _x2, _y2, 0x07E0)
                                    except: pass
                                det_frame_565 = _out
                                print("[DET] Screenshot captured")
                                if uart_tx_enabled and cur_state == 1:
                                    _cr = tuple(sorted(_cur))
                                    if _cr != prev_pest_result:
                                        prev_pest_result = _cr
                                        try:
                                            _image_sending = True
                                            uart_comm.send_image_frame(_out)
                                            _image_sending = False
                                            print("[TX] Image sent, %d bytes" % len(_out))
                                        except Exception as _ie:
                                            _image_sending = False
                                            print("[TX] Image err:", _ie)
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
    os.exitpoint(os.EXITPOINT_ENABLE)
    while True:
        if button_is_pressed():
            time.sleep_ms(50)
            if button_is_pressed():
                start = time.ticks_ms()
                while button_is_pressed(): time.sleep_ms(20)
                hold = time.ticks_diff(time.ticks_ms(), start)
                btn_action = 2 if hold >= BTN_LONG_PRESS_MS else 1
        os.exitpoint()
        time.sleep_ms(50)

# ==================== Search Orchestration ====================
def _search_start():
    """Enter search mode: record blacklist, set state, send feedback."""
    global system_state, blacklist_angle, blacklist_version, search_result
    global search_waypoints, search_waypoint_idx, search_phase, search_phase_start
    global _chs_filt_dx, _chs_filt_dy, _chs_filt_l, _chs_valid_cnt, _chs_last_tx_ms

    _sm = get_state_machine()

    # Record blacklist from current tracking target
    if current_mode == MODE_TRACK and track_result and track_result.get('found'):
        _tr = track_result
        _dummy_bbox = (_tr['x'], _tr['y'], _tr['x'] + _tr['w'], _tr['y'] + _tr['h'])
        _p, _t = _compute_abs_angles(_dummy_bbox, _sm.auto_yaw, _sm.auto_pitch,
                                     float(RGB888P_SIZE[0]), float(RGB888P_SIZE[1]))
        blacklist_angle = (_p, _t)
    elif current_mode == MODE_PEST and yolo_ref and yolo_ref.all_dets:
        # Blacklist largest non-excluded pest detection
        _best = None
        _best_area = 0
        for _d in yolo_ref.all_dets:
            _cid = int(_d[5])
            if _cid < len(LABELS_LIST) and LABELS_LIST[_cid] in LABELS_EXCLUDE:
                continue
            _a = (_d[2] - _d[0]) * (_d[3] - _d[1])
            if _a > _best_area:
                _best_area = _a
                _best = _d
        if _best:
            _p, _t = _compute_abs_angles(_best, _sm.auto_yaw, _sm.auto_pitch,
                                         float(RGB888P_SIZE[0]), float(RGB888P_SIZE[1]))
            blacklist_angle = (_p, _t)
        else:
            blacklist_angle = None
    else:
        blacklist_angle = None

    blacklist_version += 1
    search_result = None
    search_waypoints = _compute_search_waypoints()
    search_waypoint_idx = 0
    search_phase = _SEARCH_PHASE_ZERO_DELAY
    search_phase_start = time.ticks_ms()

    _sm.system_state = STATE_SEARCHING
    system_state = STATE_SEARCHING

    # Reset chassis filter state
    _chs_filt_dx = 0.0
    _chs_filt_dy = 0.0
    _chs_filt_l = 0.0
    _chs_valid_cnt = 0
    _chs_last_tx_ms = 0

    if uart_comm:
        uart_comm.send_feedback(0xF0, 0x01)  # search started
        set_status("搜索中...", 0x00AAFF)
    print("[SEARCH] Started | blacklist=%.1f,%.1f | waypoints=%s" %
          (blacklist_angle[0] if blacklist_angle else 0.0,
           blacklist_angle[1] if blacklist_angle else 0.0,
           str(search_waypoints)))


def _search_success():
    """Exit search mode: lock new target, resume tracking."""
    global system_state, blacklist_angle, blacklist_version, search_result
    global search_waypoints, search_waypoint_idx, search_phase

    _sr = search_result
    print("[SEARCH] SUCCESS | new target at pan=%.1f tilt=%.1f bbox=%s" %
          (_sr['p_abs'], _sr['t_abs'], str(_sr['bbox']) if _sr else '?'))

    _sm = get_state_machine()

    # Immediately center servo on found target so PID starts near zero error
    from src.hardware.servo import servo_write, YAW_MIN, YAW_MAX, PITCH_MIN, PITCH_MAX
    _pan = max(YAW_MIN, min(YAW_MAX, _sr['p_abs']))
    _tilt = max(PITCH_MIN, min(PITCH_MAX, _sr['t_abs']))
    servo_write(_tilt, _pan)
    _sm.auto_yaw = _pan
    _sm.auto_pitch = _tilt

    _sm.system_state = STATE_TRACKING
    system_state = STATE_TRACKING
    _sm.lost_reset()

    blacklist_angle = None
    blacklist_version += 1
    search_result = None
    search_waypoints = []
    search_waypoint_idx = 0
    search_phase = _SEARCH_PHASE_ZERO_DELAY

    if uart_comm:
        uart_comm.send_feedback(0xF1, 0x02)  # search success
        set_status("搜索成功", 0x00FF00)


def _search_failed():
    """Exit search mode: fast home, enter IDLE."""
    global system_state, blacklist_angle, blacklist_version, search_result
    global search_waypoints, search_waypoint_idx, search_phase

    _sm = get_state_machine()
    _sm.fast_home()
    _sm.system_state = STATE_IDLE
    system_state = STATE_IDLE
    _sm.lost_reset()

    print("[SEARCH] FAILED | No new target found, entering IDLE")

    blacklist_angle = None
    blacklist_version += 1
    search_result = None
    search_waypoints = []
    search_waypoint_idx = 0
    search_phase = _SEARCH_PHASE_ZERO_DELAY

    if uart_comm:
        uart_comm.send_feedback(0xF2, 0x03)  # search failed
        set_status("搜索失败, 已空闲", 0xFF5500)


def _search_tick():
    """Non-blocking search state machine. Call from main loop each iteration."""
    global search_phase, search_phase_start, search_waypoint_idx, search_result

    _now = time.ticks_ms()

    if search_phase == _SEARCH_PHASE_ZERO_DELAY:
        # Wait for detect thread to process blacklisted frame
        if blacklist_version_processed >= blacklist_version:
            if search_result and search_result.get('found'):
                _search_success()
                return
            # Move to first waypoint
            if search_waypoints:
                _wp = search_waypoints[0]
                _sm = get_state_machine()
                from src.hardware.servo import servo_write, YAW_MIN, YAW_MAX, PITCH_MIN, PITCH_MAX, YAW_HOME, PITCH_HOME
                _pan = max(YAW_MIN, min(YAW_MAX, YAW_HOME + _wp[0]))
                _tilt = max(PITCH_MIN, min(PITCH_MAX, PITCH_HOME + _wp[1]))
                servo_write(_tilt, _pan)
                _sm.auto_yaw = _pan
                _sm.auto_pitch = _tilt
                print("[SEARCH] WP[0] -> pan=%.0f tilt=%.0f" % (_pan, _tilt))
            search_phase = _SEARCH_PHASE_MOVE
            search_phase_start = _now

    elif search_phase == _SEARCH_PHASE_MOVE:
        if time.ticks_diff(_now, search_phase_start) >= SEARCH_SCAN_CFG['move_estimate_ms']:
            search_phase = _SEARCH_PHASE_SETTLE
            search_phase_start = _now

    elif search_phase == _SEARCH_PHASE_SETTLE:
        if time.ticks_diff(_now, search_phase_start) >= SEARCH_SCAN_CFG['settle_ms']:
            if search_result and search_result.get('found'):
                _search_success()
                return
            # Next waypoint
            search_waypoint_idx += 1
            if search_waypoint_idx >= len(search_waypoints):
                _search_failed()
                return
            _wp = search_waypoints[search_waypoint_idx]
            _sm = get_state_machine()
            from src.hardware.servo import servo_write, YAW_MIN, YAW_MAX, PITCH_MIN, PITCH_MAX, YAW_HOME, PITCH_HOME
            _pan = max(YAW_MIN, min(YAW_MAX, YAW_HOME + _wp[0]))
            _tilt = max(PITCH_MIN, min(PITCH_MAX, PITCH_HOME + _wp[1]))
            servo_write(_tilt, _pan)
            _sm.auto_yaw = _pan
            _sm.auto_pitch = _tilt
            print("[SEARCH] WP[%d] -> pan=%.0f tilt=%.0f" % (search_waypoint_idx, _pan, _tilt))
            search_phase = _SEARCH_PHASE_MOVE
            search_phase_start = _now


def main():
    global pl, cur_state, ai_running, status_label, btn_action
    global _btn_last_blink, _btn_blink_on, camera_paused, uart_tx_enabled
    global gui_fps, gui_dirty, crop_mode, crop_switch_to, current_mode, track_result, uart_comm
    global system_state, blacklist_angle, blacklist_version, search_result
    global search_waypoints, search_waypoint_idx, search_phase, search_phase_start
    global prev_pest_result
    os.exitpoint(os.EXITPOINT_ENABLE)

    print("=" * 50)
    print("  Corn Disease Detection - LVGL v3")
    print("=" * 50)

    try:
        gc.collect(); time.sleep_ms(100)

        print("[INIT] PipeLine...")
        pl = PipeLine(rgb888p_size=RGB888P_SIZE, display_size=DISPLAY_SIZE, display_mode="st7701")
        _sensor_ref = Sensor(width=1920, height=1080)
        pl.create(sensor=_sensor_ref)

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
        cur_state = 0
        get_state_machine().detect_switch = False
        _thread.start_new_thread(detect_thread, ())
        if status_label:
            status_label.set_text("AI暂停")
            status_label.set_style_text_color(lv.color_hex(0xffaa00), 0)
        print("[INIT] Detection started")

        # Main loop: LVGL + button + UART
        while True:
            # Button
            if btn_action != 0:
                action = btn_action; btn_action = 0
                if action == 1:  # short: toggle AI
                    if camera_paused:
                        print("[BTN] Resume camera first")
                    elif cur_state == 0:
                        cur_state = 1
                        get_state_machine().detect_switch = True
                        if status_label:
                            status_label.set_text("运行中")
                            status_label.set_style_text_color(lv.color_hex(0x00ff00), 0)
                    elif cur_state == 1:
                        cur_state = 0
                        get_state_machine().detect_switch = False
                        if status_label:
                            status_label.set_text("AI暂停")
                            status_label.set_style_text_color(lv.color_hex(0xffaa00), 0)
                elif action == 2:  # long: toggle camera
                    camera_paused = not camera_paused
                    if camera_paused:
                        cur_state = 0
                        if status_label:
                            status_label.set_text("相机已暂停")
                            status_label.set_style_text_color(lv.color_hex(0xff5500), 0)
                    else:
                        if status_label:
                            status_label.set_text("相机已恢复")
                            status_label.set_style_text_color(lv.color_hex(0x00aaff), 0)

            # UART 命令轮询（来自CH32的模式/启停命令）
            try:
                if uart_comm:
                    cmd = uart_comm.check_command()
                    if cmd == CMD_START and cur_state == 0 and not camera_paused:
                        cur_state = 1
                        set_status("运行中", 0x00ff00)
                        _sm = get_state_machine()
                        _sm.detect_switch = True
                        _sm.lost_counter = 0
                    elif cmd == CMD_STOP and cur_state == 1:
                        cur_state = 0
                        set_status("AI暂停", 0xffaa00)
                        _sm = get_state_machine()
                        _sm.detect_switch = False
                    elif cmd == CMD_MODE_PEST:
                        current_mode = MODE_PEST
                        _sm = get_state_machine()
                        _sm.target_mode = "PEST"
                        _sm.lost_counter = 0
                        set_status("病虫检测模式", 0x00AAFF)
                        print("[MODE] PEST")
                    elif cmd == CMD_MODE_TRACK:
                        if _ensure_person_detector():
                            _sm = get_state_machine()
                            # a. Disable auto-tracking, enable detection
                            _sm.work_mode = "MANUAL"
                            _sm.detect_switch = True
                            _sm.target_mode = "BODY"
                            _sm.lost_counter = 0
                            # b. Home servos to center (90,90)
                            _sm.handle_command("H")
                            # c. Start chassis tracking
                            current_mode = MODE_TRACK
                            gui_dets = None
                            prev_det_classes = set()
                            if result_label: result_label.set_text("---")
                            if count_label: count_label.set_text("0")
                            set_status("追踪模式", 0x00AAFF)
                            print("[MODE] TRACK -> servo home -> chassis")
                        else:
                            set_status("追踪失败", 0xFF0000)
                            print("[MODE] TRACK failed")
                    elif cmd == CMD_MUTE_TX:
                        uart_tx_enabled = not uart_tx_enabled
                        print("[UART] TX %s" % ("OFF" if not uart_tx_enabled else "ON"))
                    elif cmd == CMD_SWITCH_CROP:
                        if crop_switch_to < 0:
                            _next = {CROP_CORN: CROP_SUGARCANE, CROP_SUGARCANE: CROP_TOMATO, CROP_TOMATO: CROP_CORN}
                            crop_switch_to = _next.get(crop_mode, CROP_CORN)
                            print("[CROP] Switching to: %s" % CROP_NAMES.get(crop_switch_to, "?"))
                        else:
                            print("[CROP] Switch already pending, ignoring")
                    elif cmd == CMD_CROP_CORN:
                        if crop_switch_to < 0 and crop_mode != CROP_CORN:
                            crop_switch_to = CROP_CORN
                            print("[CROP] Switching to: corn")
                    elif cmd == CMD_CROP_SUGARCANE:
                        if crop_switch_to < 0 and crop_mode != CROP_SUGARCANE:
                            crop_switch_to = CROP_SUGARCANE
                            print("[CROP] Switching to: sugarcane")
                    elif cmd == CMD_CROP_TOMATO:
                        if crop_switch_to < 0 and crop_mode != CROP_TOMATO:
                            crop_switch_to = CROP_TOMATO
                            print("[CROP] Switching to: tomato")
                    elif cmd in (CMD_SERVO_HOME, CMD_SERVO_LEFT, CMD_SERVO_RIGHT,
                                 CMD_SERVO_DOWN, CMD_SERVO_UP, CMD_SERVO_PATROL):
                        _sm = get_state_machine()
                        _sm.handle_command(cmd)
                    elif cmd == 0x10:  # CMD_SEARCH
                        _sm = get_state_machine()
                        if _sm.work_mode != "AUTO" or (system_state != STATE_TRACKING and system_state != STATE_IDLE):
                            if uart_comm:
                                uart_comm.send_feedback(0xF3, 0x04)  # rejected
                            print("[SEARCH] REJECTED: not in AUTO+TRACKING/IDLE")
                        else:
                            _search_start()
                    elif cmd == 0x11:  # CMD_ABORT
                        _sm = get_state_machine()
                        if system_state == STATE_SEARCHING:
                            _sm.fast_home()
                            _sm.system_state = STATE_IDLE
                            system_state = STATE_IDLE
                            blacklist_angle = None
                            blacklist_version += 1
                            search_result = None
                            search_waypoints = []
                            search_waypoint_idx = 0
                            search_phase = _SEARCH_PHASE_ZERO_DELAY
                            if uart_comm:
                                uart_comm.send_feedback(0xF4, 0x05)  # search aborted
                                set_status("搜索已中止", 0xFF5500)
                            print("[SEARCH] ABORTED -> IDLE")
                        elif system_state == STATE_TRACKING:
                            _sm.system_state = STATE_IDLE
                            system_state = STATE_IDLE
                            _sm.fast_home()
                            if uart_comm:
                                uart_comm.send_feedback(0xF4, 0x05)  # aborted
                            print("[SYS] ABORT -> IDLE")
                        else:
                            # Already IDLE, just confirm
                            if uart_comm:
                                uart_comm.send_feedback(0xF4, 0x05)

                    # Populate gui_dets for UART TX
                    try:
                        _dets = yolo_ref.all_dets if yolo_ref else []
                        _c = []; _s = []
                        for _d in _dets:
                            _cid = _d[5]
                            if _cid < len(LABELS_LIST) and LABELS_LIST[_cid] in LABELS_EXCLUDE:
                                continue
                            _c.append(_cid)
                            _s.append(_d[4])
                        gui_dets = [list(range(len(_c))), _c, _s] if _c else None
                    except: gui_dets = None

                    # Send detection results to CH32 via UART
                    if uart_tx_enabled and cur_state == 1 and system_state != STATE_IDLE:
                        if _image_sending:
                            pass  # skip pest/track TX during image transfer
                        elif current_mode == MODE_TRACK:
                            pass  # chassis data sent from detection thread
                        elif current_mode == MODE_PEST:
                            if gui_dets and gui_dets[0]:
                                best_cls = int(gui_dets[1][0])
                                best_score = float(gui_dets[2][0])
                                uart_comm.send_pest_data(best_cls, int(best_score * 100), len(gui_dets[0]), crop_mode)
                            else:
                                uart_comm.send_pest_data(0, 0, 0, crop_mode)
            except Exception as _uart_e:
                print("[UART] Err:", _uart_e)

            # Sync module-level system_state from state machine
            # (handles IDLE->TRACKING transitions from _cmd_toggle_mode etc.)
            _sync_sm = get_state_machine()
            if _sync_sm.system_state != system_state:
                system_state = _sync_sm.system_state

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

            # Search state machine (non-blocking)
            if system_state == STATE_SEARCHING:
                _search_tick()

            # LVGL
            os.exitpoint()
            update_gui()
            lv.task_handler()
            time.sleep_ms(5)

    except KeyboardInterrupt:
        print("Interrupted")
    except Exception as e:
        print("[MAIN] Error:", e)
    finally:
        cur_state = 0; camera_paused = False; uart_tx_enabled = True; crop_switch_to = -1
        stream_frame = None; shared_frame_565 = None; ai_running = False
        det_frame_565 = None; det_frame_time = 0; prev_det_classes = set(); det_result_changed = False
        time.sleep_ms(500); led_set(False)
        if uart_comm: uart_comm.deinit()
        lvgl_deinit()
        if pl: pl.destroy()
        gc.collect()
        print("[INFO] Done")

if __name__ == "__main__":
    main()