# UART Communication Module v3
# Supports pest data, tracking data, and image frame transfer via UART

from machine import UART
from machine import FPIOA
import time

HEADER1 = 0xAA
HEADER2 = 0x55
# BB 55 feedback frame (async event push)
FB_HEADER1 = 0xBB
FB_HEADER2 = 0x55
FB_FOOTER1 = 0x55
FB_FOOTER2 = 0xBB
FOOTER1 = 0x55
FOOTER2 = 0xAA
TYPE_PEST = 0x01
TYPE_TRACK = 0x03
TYPE_IMAGE = 0x10
PEST_DATA_LEN = 8
TRACK_DATA_LEN = 16
PEST_FRAME_LEN = 15
TRACK_FRAME_LEN = 23
CMD_START = 0x01
CMD_STOP = 0x02
CMD_MUTE_TX = 0x03
CMD_SWITCH_CROP = 0x04
CMD_MODE_PEST = 0x05
CMD_MODE_TRACK = 0x06
CHUNK_SIZE = 1400
CMD_SEARCH = 0x10
CMD_ABORT = 0x11
_S_HDR1 = 0
_S_HDR2 = 1
_S_CMD = 2
_S_FTR1 = 3
_S_FTR2 = 4

class UartComm:
    PIN_TX = 3
    PIN_RX = 4
    BAUD = 921600
    def __init__(self):
        self._uart = None
        self._ok = False
        self._state = _S_HDR1
        self._cmd = 0
        self._tx_pest_n = 0
        self._tx_track_n = 0
        self._rx_n = 0
        self._frame_id = 0
    def init(self):
        fpioa = FPIOA()
        fpioa.set_function(self.PIN_TX, FPIOA.UART1_TXD)
        fpioa.set_function(self.PIN_RX, FPIOA.UART1_RXD)
        self._uart = UART(UART.UART1, self.BAUD)
        self._ok = True
        self._state = _S_HDR1
        return self
    def send_pest_data(self, pest_type, confidence, count, crop_type=0):
        if not self._ok or self._uart is None: return
        data = bytearray(PEST_DATA_LEN)
        data[0] = pest_type & 0xFF
        data[1] = confidence & 0xFF
        data[2] = count & 0xFF
        data[3] = (count >> 8) & 0xFF
        data[4] = crop_type & 0xFF
        checksum = HEADER1 ^ HEADER2 ^ TYPE_PEST ^ PEST_DATA_LEN
        for i in range(PEST_DATA_LEN): checksum ^= data[i]
        frame = bytearray(PEST_FRAME_LEN)
        frame[0] = HEADER1; frame[1] = HEADER2; frame[2] = TYPE_PEST; frame[3] = PEST_DATA_LEN
        frame[4:4 + PEST_DATA_LEN] = data
        frame[4 + PEST_DATA_LEN] = checksum
        frame[4 + PEST_DATA_LEN + 1] = FOOTER1
        frame[4 + PEST_DATA_LEN + 2] = FOOTER2
        try:
            self._uart.write(bytes(frame))
            self._tx_pest_n += 1
        except Exception as e:
            print('[UART TX] Error:', e)
    def send_track_data(self, result):
        if not self._ok or self._uart is None or result is None: return
        data = bytearray(TRACK_DATA_LEN)
        x = max(0, min(65535, result.get('x', 0)))
        y = max(0, min(65535, result.get('y', 0)))
        w = max(0, min(65535, result.get('w', 0)))
        h = max(0, min(65535, result.get('h', 0)))
        data[0] = x & 0xFF; data[1] = (x >> 8) & 0xFF
        data[2] = y & 0xFF; data[3] = (y >> 8) & 0xFF
        data[4] = w & 0xFF; data[5] = (w >> 8) & 0xFF
        data[6] = h & 0xFF; data[7] = (h >> 8) & 0xFF
        dx = max(-32768, min(32767, result.get('dx', 0)))
        dy = max(-32768, min(32767, result.get('dy', 0)))
        data[8] = dx & 0xFF; data[9] = (dx >> 8) & 0xFF
        data[10] = dy & 0xFF; data[11] = (dy >> 8) & 0xFF
        data[12] = int(result.get('confidence', 0.0) * 100) & 0xFF
        data[13] = result.get('track_id', 0) & 0xFF
        data[14] = result.get('status', 0x00) & 0xFF
        data[15] = 0
        checksum = HEADER1 ^ HEADER2 ^ TYPE_TRACK ^ TRACK_DATA_LEN
        for i in range(TRACK_DATA_LEN): checksum ^= data[i]
        frame = bytearray(TRACK_FRAME_LEN)
        frame[0] = HEADER1; frame[1] = HEADER2; frame[2] = TYPE_TRACK; frame[3] = TRACK_DATA_LEN
        frame[4:4 + TRACK_DATA_LEN] = data
        frame[4 + TRACK_DATA_LEN] = checksum
        frame[4 + TRACK_DATA_LEN + 1] = FOOTER1
        frame[4 + TRACK_DATA_LEN + 2] = FOOTER2
        try:
            self._uart.write(bytes(frame))
            self._tx_track_n += 1
        except Exception as e:
            print('[UART TX] Error:', e)
    def send_image_frame(self, frame_buf):
        """Send 160x120 RGB565 frame via chunked IMAGE frames (type 0x10)."""
        if not self._ok or self._uart is None or frame_buf is None: return
        total = len(frame_buf)
        if total == 0: return
        fid = self._frame_id
        self._frame_id = (self._frame_id + 1) & 0xFFFF
        nc = (total + CHUNK_SIZE - 1) // CHUNK_SIZE
        ok = True
        for ci in range(nc):
            off = ci * CHUNK_SIZE
            end = min(off + CHUNK_SIZE, total)
            chunk = frame_buf[off:end]
            cs = len(chunk)
            data = bytearray(10 + cs)
            data[0] = fid & 0xFF; data[1] = (fid >> 8) & 0xFF
            data[2] = ci & 0xFF; data[3] = (ci >> 8) & 0xFF
            data[4] = nc & 0xFF; data[5] = (nc >> 8) & 0xFF
            data[6] = cs & 0xFF; data[7] = (cs >> 8) & 0xFF
            data[8] = (cs >> 16) & 0xFF; data[9] = (cs >> 24) & 0xFF
            data[10:] = chunk
            dl = len(data)
            frm = bytearray(8 + dl)
            frm[0] = HEADER1; frm[1] = HEADER2; frm[2] = TYPE_IMAGE
            frm[3] = dl & 0xFF; frm[4] = (dl >> 8) & 0xFF
            frm[5:5+dl] = data
            chk = HEADER1 ^ HEADER2 ^ TYPE_IMAGE ^ (dl & 0xFF) ^ ((dl >> 8) & 0xFF)
            for b in data: chk ^= b
            frm[5+dl] = chk; frm[6+dl] = FOOTER1; frm[7+dl] = FOOTER2
            try:
                self._uart.write(bytes(frm))
            except Exception as e:
                print('[UART IMG] Error:', e)
                ok = False
                break
        if ok:
            print('[UART IMG] fid=%d %dB %d chunks' % (fid, total, nc))
    def send_feedback(self, event, data=0):
        """Send async BB 55 feedback frame: BB 55 <EVENT> <DATA> 55 BB"""
        if not self._ok or self._uart is None:
            return
        try:
            frame = bytearray(6)
            frame[0] = FB_HEADER1
            frame[1] = FB_HEADER2
            frame[2] = event & 0xFF
            frame[3] = data & 0xFF
            frame[4] = FB_FOOTER1
            frame[5] = FB_FOOTER2
            self._uart.write(bytes(frame))
        except Exception as e:
            print('[FB TX] Error:', e)
    def check_command(self):
        if not self._ok or self._uart is None: return 0
        try:
            avail = self._uart.any()
            if avail == 0: return 0
            if avail > 8: avail = 8
            buf = self._uart.read(avail)
            if buf is None: return 0
        except: return 0
        cmd = 0
        for b in buf:
            cmd = self._parse(b)
            if cmd != 0: break
        return cmd
    def _parse(self, b):
        if self._state == _S_HDR1:
            if b == HEADER1: self._state = _S_HDR2
        elif self._state == _S_HDR2:
            if b == HEADER2: self._state = _S_CMD
            else: self._state = _S_HDR1
        elif self._state == _S_CMD:
            self._cmd = b; self._state = _S_FTR1
        elif self._state == _S_FTR1:
            if b == FOOTER1: self._state = _S_FTR2
            else: self._state = _S_HDR1
        elif self._state == _S_FTR2:
            self._state = _S_HDR1
            if b == FOOTER2:
                self._rx_n += 1
                return self._cmd
        else: self._state = _S_HDR1
        return 0
    def deinit(self):
        if self._uart:
            try: self._uart.deinit()
            except: pass
        self._ok = False