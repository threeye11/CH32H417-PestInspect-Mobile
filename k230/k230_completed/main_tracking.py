# K230 Main Tracking Script (Threaded)
# Main thread: PipeLine + Sensor + UART + servo + PID
# Thread 1: AI detection + OSD display

import sys, os
os.chdir("/sdcard")
sys.path.insert(0, "/sdcard")

import time, gc, _thread

from machine import UART, FPIOA

# UART1
fpioa = FPIOA()
fpioa.set_function(3, FPIOA.UART1_TXD)
fpioa.set_function(4, FPIOA.UART1_RXD)
uart = UART(UART.UART1, 921600)

from src.hardware.servo import servo_write, servo_home, YAW_MIN, YAW_MAX, PITCH_MIN, PITCH_MAX
from src.hardware.state_machine import get_state_machine, MODE_AUTO
from src.hardware.pid import create_gimbal_pids
from src.hardware.tracking import TrackingEngine, LOST_THRESHOLD, HOMING_SPEED

from libs.PipeLine import PipeLine, ScopedTiming
from libs.AIBase import AIBase
from libs.AI2D import Ai2d
from libs.Utils import *
from media.sensor import Sensor
import nncase_runtime as nn
import ulab.numpy as np
import aicube

IMG_W = 640; IMG_H = 360
HEADER1=0xAA; HEADER2=0x55; FOOTER1=0x55; FOOTER2=0xAA
TYPE_TRACK=0x03; TRACK_DATA_LEN=16; TRACK_FRAME_LEN=23
BIN_CMD = {0x01:"S",0x02:"T",0x03:"M",0x04:"C",0x05:"P",0x06:"R",
           0x07:"H",0x08:"A",0x09:"D",0x0A:"Z",0x0B:"W",0x0C:"O"}

# ==================== Shared State ====================
ai_running = False
det_lock = _thread.allocate_lock()
det_result = {"found": False, "x": 0, "y": 0, "score": 0.0}


# ==================== Person Detection ====================
PERSON_KMODEL_PATH = "/sdcard/person_detect_yolo11n.kmodel"

# ==================== Chassis Tracking Parameters ====================
K_CM_PER_PIXEL = 0.1          # 标定系数：厘米/像素（需根据实际标定调整）
CHASSIS_DEAD_ZONE = 10        # 像素死区，小于此距离不发送转向


# ==================== Pest YOLO11 Parse ====================
PEST_EXCLUDE = (3, 10)

def parse_yolo11(raw, rgb_w, rgb_h):
    out = raw.reshape((raw.shape[0]*raw.shape[1], raw.shape[2])).transpose()
    boxes=out[:,0:4]; scores=out[:,4:]
    confs=np.max(scores,axis=-1); cls_ids=np.argmax(scores,axis=-1)
    xf=float(rgb_w)/320.0; yf=float(rgb_h)/320.0
    dets=[]
    for i in range(len(boxes)):
        if confs[i]<=0.6 or int(cls_ids[i]) in PEST_EXCLUDE: continue
        cx,cy,w,h=float(boxes[i,0]),float(boxes[i,1]),float(boxes[i,2]),float(boxes[i,3])
        x1=max(0,int((cx-0.5*w)*xf)); y1=max(0,int((cy-0.5*h)*yf))
        x2=min(rgb_w,int((cx+0.5*w)*xf)); y2=min(rgb_h,int((cy+0.5*h)*yf))
        dets.append((x1,y1,x2,y2,float(confs[i]),int(cls_ids[i])))
    nms=[]; groups={}
    for d in dets: groups.setdefault(d[5],[]).append(d)
    for g in groups.values():
        g.sort(key=lambda x:x[4],reverse=True); keep=[]; sup=[False]*len(g)
        for i in range(len(g)):
            if sup[i]: continue
            keep.append(g[i])
            for j in range(i+1,len(g)):
                if sup[j]: continue
                xx1=max(g[i][0],g[j][0]); yy1=max(g[i][1],g[j][1])
                xx2=min(g[i][2],g[j][2]); yy2=min(g[i][3],g[j][3])
                inter=max(0,xx2-xx1)*max(0,yy2-yy1)
                a1=(g[i][2]-g[i][0])*(g[i][3]-g[i][1])
                a2=(g[j][2]-g[j][0])*(g[j][3]-g[j][1])
                if inter/(a1+a2-inter+1e-6)>0.45: sup[j]=True
        nms.extend(keep)
    return nms


# ==================== UART Send ====================
def send_track(x,y,w,h,dx,dy,conf,tid,status):
    data=bytearray(TRACK_DATA_LEN)
    x=max(0,min(65535,x)); y=max(0,min(65535,y))
    dx=max(-32768,min(32767,dx)); dy=max(-32768,min(32767,dy))
    data[0]=x&0xFF; data[1]=(x>>8)&0xFF; data[2]=y&0xFF; data[3]=(y>>8)&0xFF
    data[4]=w&0xFF; data[5]=(w>>8)&0xFF; data[6]=h&0xFF; data[7]=(h>>8)&0xFF
    data[8]=dx&0xFF; data[9]=(dx>>8)&0xFF; data[10]=dy&0xFF; data[11]=(dy>>8)&0xFF
    data[12]=int(conf*100)&0xFF; data[13]=tid&0xFF; data[14]=status&0xFF
    chk=HEADER1^HEADER2^TYPE_TRACK^TRACK_DATA_LEN
    for i in range(TRACK_DATA_LEN): chk^=data[i]
    frm=bytearray(TRACK_FRAME_LEN)
    frm[0]=HEADER1; frm[1]=HEADER2; frm[2]=TYPE_TRACK; frm[3]=TRACK_DATA_LEN
    frm[4:20]=data; frm[20]=chk; frm[21]=FOOTER1; frm[22]=FOOTER2
    try: uart.write(bytes(frm))
    except: pass


def send_chassis_track(steering_x10, dist_cm, dx, dy, status):
    data=bytearray(TRACK_DATA_LEN)
    steering_x10=max(-32768,min(32767,steering_x10))
    dist_cm=max(0,min(65535,dist_cm))
    dx=max(-32768,min(32767,dx)); dy=max(-32768,min(32767,dy))
    data[0]=steering_x10&0xFF; data[1]=(steering_x10>>8)&0xFF
    data[2]=dist_cm&0xFF; data[3]=(dist_cm>>8)&0xFF
    data[4]=dx&0xFF; data[5]=(dx>>8)&0xFF
    data[6]=dy&0xFF; data[7]=(dy>>8)&0xFF
    data[8]=status&0xFF
    chk=HEADER1^HEADER2^TYPE_TRACK^TRACK_DATA_LEN
    for i in range(TRACK_DATA_LEN): chk^=data[i]
    frm=bytearray(TRACK_FRAME_LEN)
    frm[0]=HEADER1; frm[1]=HEADER2; frm[2]=TYPE_TRACK; frm[3]=TRACK_DATA_LEN
    frm[4:20]=data; frm[20]=chk; frm[21]=FOOTER1; frm[22]=FOOTER2
    try: uart.write(bytes(frm))
    except: pass


# ==================== Detection Thread ====================
def detect_thread(pl_ref, sm_ref):
    """AI detection + OSD. PipeLine created in main thread."""
    global ai_running, det_result

    print("[THD] Detection thread started")
    _thd_frame = 0
    person_det = None; pest_yolo = None

    try:
        while ai_running:
            os.exitpoint()
            img = pl_ref.get_frame()
            if _thd_frame % 30 == 0: print("[THD] frame=%d found=%s" % (_thd_frame, found))

            found=False; tx=0; ty=0; ts=0.0

            if sm_ref.work_mode == MODE_AUTO and sm_ref.detect_switch:
                if sm_ref.target_mode == "BODY":
                    if person_det is None:
                        try:
                            gc.collect(); time.sleep_ms(100)
                            from libs.YOLO import YOLO11
                            person_det = YOLO11(task_type="detect", mode="video",
                                kmodel_path=PERSON_KMODEL_PATH,
                                labels={0: "person"},
                                rgb888p_size=[IMG_W, IMG_H],
                                model_input_size=[320, 320],
                                display_size=[800, 480],
                                conf_thresh=0.6, nms_thresh=0.45,
                                max_boxes_num=50, debug_mode=0)
                            person_det.config_preprocess()
                            print("[THD] Person model loaded")
                        except Exception as e:
                            print("[THD] Person load err:", e)
                        dets = person_det.run(img)
                        if dets:
                            bboxes, cls_ids, scores = dets[0], dets[1], dets[2]
                            best=0
                            for i in range(len(bboxes)):
                                bbox = bboxes[i]
                                a = int(bbox[2]) * int(bbox[3])
                                if a > best:
                                    best = a
                                    tx = int(bbox[0]) + int(bbox[2]) // 2
                                    ty = int(bbox[1]) + int(bbox[3]) // 2
                                    ts = float(scores[i]); found = True

                elif sm_ref.target_mode == "PEST":
                    if pest_yolo is None:
                        try:
                            gc.collect(); time.sleep_ms(100)
                            from libs.YOLO import YOLO11
                            pest_yolo = YOLO11(
                                task_type="detect", mode="video",
                                kmodel_path="/sdcard/yolo11s_det_320.kmodel",
                                labels={0:"Abiotics",1:"Aphids",2:"Curvu",3:"dirt",
                                        4:"Helminth",5:"Healthy",6:"Rust",
                                        7:"Spodoptera_a",8:"Spodoptera_P",
                                        9:"Stripe",10:"Weeds"},
                                rgb888p_size=[IMG_W,IMG_H], model_input_size=[320,320],
                                display_size=[800,480], conf_thresh=0.6,
                                nms_thresh=0.45, max_boxes_num=50, debug_mode=0)
                            pest_yolo.config_preprocess()
                            print("[THD] Pest model loaded")
                        except Exception as e:
                            print("[THD] Pest load err:", e)
                    if pest_yolo:
                        pest_yolo.run(img)
                        raw_dets = parse_yolo11(pest_yolo.results[0], IMG_W, IMG_H)
                        if raw_dets:
                            best=0
                            for d in raw_dets:
                                a=(d[2]-d[0])*(d[3]-d[1])
                                if a>best:
                                    best=a; tx=(d[0]+d[2])//2; ty=(d[1]+d[3])//2
                                    ts=d[4]; found=True

            _thd_frame += 1
            # Shared result
            det_lock.acquire()
            det_result["found"]=found; det_result["x"]=tx
            det_result["y"]=ty; det_result["score"]=ts
            det_lock.release()

            # OSD
            pl_ref.osd_img.clear()
            if found:
                pl_ref.osd_img.draw_cross(tx, ty, color=(0,255,0,255), thickness=2)

            pl_ref.show_image()
            gc.collect()

    except Exception as e:
        sys.print_exception(e)
        print("[THD] Thread crashed!")
    finally:
        print("[THD] Detection thread exiting")


# ==================== Main ====================
def main():
    global ai_running

    sm = get_state_machine()
    sm.handle_command("H")

    # PipeLine + Sensor MUST be in main thread
    print("[MAIN] PipeLine...")
    pl = PipeLine(rgb888p_size=[IMG_W, IMG_H],
                   display_size=[800, 480], display_mode="st7701")
    print("[MAIN] Sensor...")
    pl.create(sensor=Sensor(width=1920, height=1080))
    print("[MAIN] PipeLine ready")

    engine = TrackingEngine(img_w=IMG_W, img_h=IMG_H,
                            kp=0.05, ki=0.0, kd=0.005,
                            dead_zone=5, max_output=8.0)
    engine.yaw_pid.dead_zone = 8   # yaw轴死区略大，抑制水平舵机抖动

    # Start detection thread
    print("[MAIN] Starting AI thread...")
    ai_running = True
    _thread.start_new_thread(detect_thread, (pl, sm))
    time.sleep_ms(500)

    print("[MAIN] Ready. S=Start T=Stop H=Home O=Auto P=Pest R=Body")
    frame = 0

    try:
        while True:
            os.exitpoint()
            frame += 1

            # UART commands
            if uart.any():
                raw = uart.read(1)
                if raw and raw[0] in BIN_CMD:
                    sm.handle_command(BIN_CMD[raw[0]])

            # Shared detection result
            det_lock.acquire()
            found = det_result["found"]
            tx = det_result["x"]; ty = det_result["y"]
            det_lock.release()

            if sm.work_mode == MODE_AUTO and sm.detect_switch:
                if sm.target_mode == "BODY":
                    # === Chassis tracking mode ===
                    if found:
                        sm.lost_reset()
                        dx = tx - IMG_W // 2
                        dy = IMG_H // 2 - ty  # Y-up positive
                        pixel_dist = np.sqrt(dx*dx + dy*dy)
                        dist_cm = int(K_CM_PER_PIXEL * pixel_dist)
                        if pixel_dist < CHASSIS_DEAD_ZONE:
                            steering = 0
                            status = 0x02  # dead zone, no turn needed
                        else:
                            heading = np.arctan2(float(dx), float(dy))
                            steering = int(heading * 1800.0 / 3.14159265)  # 0.1 deg
                            status = 0x01  # tracking
                        if sm.uart_tx_on:
                            send_chassis_track(steering, dist_cm, dx, dy, status)
                    else:
                        sm.lost_tick()
                        # Lost: send stop command, chassis decelerates
                        if sm.uart_tx_on:
                            send_chassis_track(0, 0, 0, 0, 0x00)
                else:
                    # === Gimbal PID tracking (PEST mode, unchanged) ===
                    if found:
                        sm.lost_reset()
                        dyaw = engine.yaw_pid.update(float(tx))
                        dpitch = engine.pitch_pid.update(float(ty))
                        engine.yaw = max(YAW_MIN, min(YAW_MAX, engine.yaw + dyaw))
                        engine.pitch = max(PITCH_MIN, min(PITCH_MAX, engine.pitch - dpitch))
                        servo_write(engine.pitch, engine.yaw)
                        sm.yaw_angle=engine.yaw; sm.pitch_angle=engine.pitch
                        if sm.uart_tx_on:
                            send_track(tx,ty,0,0,tx-IMG_W//2,ty-IMG_H//2,0.9,1,0x01)
                    else:
                        sm.lost_tick()
                        if sm.is_lost(LOST_THRESHOLD):
                            moved=False
                            HOMING_KP = 0.10
                            yaw_err = 90.0 - engine.yaw
                            pitch_err = 90.0 - engine.pitch
                            if abs(yaw_err) > 0.5:
                                engine.yaw += yaw_err * HOMING_KP; moved=True
                            if abs(pitch_err) > 0.5:
                                engine.pitch += pitch_err * HOMING_KP; moved=True
                            if moved:
                                servo_write(engine.pitch,engine.yaw)
                                sm.yaw_angle=engine.yaw; sm.pitch_angle=engine.pitch
                        if sm.uart_tx_on and frame%10==0:
                            send_track(0,0,0,0,0,0,0,0,0x00)

            if frame%15==0:
                tag="AUTO" if sm.work_mode==MODE_AUTO else "MANUAL"
                det="ON" if sm.detect_switch else "OFF"
                lock="LK" if found else "LOST"
                print("[%s %s] %s x=%d y=%d yaw=%.1f pitch=%.1f lost=%d" %
                      (tag,det,lock,tx if found else 0,ty if found else 0,
                       engine.yaw,engine.pitch,sm.lost_counter))

            time.sleep_ms(5)

    except KeyboardInterrupt:
        print("\n[MAIN] Interrupted")
    finally:
        ai_running = False
        time.sleep_ms(300)
        servo_home()
        uart.deinit()
        gc.collect()
        print("[MAIN] Done")


if __name__ == "__main__":
    main()
