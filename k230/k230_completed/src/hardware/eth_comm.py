# K230 Ethernet Communication Module
# Point-to-point TCP communication between K230 and CH32H417
# K230 acts as TCP Server, CH32 acts as TCP Client
# Direct Ethernet cable connection (no router needed)
# Protocol matches existing UART frame format
#
# TX frame (K230 -> CH32):
#   [0xAA][0x55][type=0x01][len=0x08][data[8]][checksum][0x55][0xAA]
# RX frame (CH32 -> K230):
#   [0xAA][0x55][cmd][0x55][0xAA]  cmd=0x01 START, cmd=0x02 STOP

import network
import socket
import time
from machine import Pin

# Frame constants (match uart_comm.py)
HEADER1 = 0xAA
HEADER2 = 0x55
FOOTER1 = 0x55
FOOTER2 = 0xAA
DATA_LEN = 8
TX_FRAME_LEN = 15  # 2(hdr) + 1(type) + 1(len) + 8(data) + 1(chk) + 2(ftr)

# Frame types
TYPE_PEST = 0x01
TYPE_HEARTBEAT = 0x02
TYPE_ACK = 0x03

# Commands from CH32
CMD_START = 0x01
CMD_STOP = 0x02

# RX state machine
_S_HDR1 = 0
_S_HDR2 = 1
_S_CMD = 2
_S_FTR1 = 3
_S_FTR2 = 4


class EthComm:
    """
    Ethernet point-to-point communication for K230.

    Usage:
        eth = EthComm()
        eth.init()                    # Start LAN + TCP server
        cmd = eth.check_command()     # Poll for CH32 commands
        eth.send_pest_data(t, c, n)   # Send detection results
        eth.deinit()                  # Cleanup
    """

    # Default network config (point-to-point, no router)
    DEFAULT_IP = "192.168.1.100"
    DEFAULT_SUBNET = "255.255.255.0"
    DEFAULT_GATEWAY = "192.168.1.1"
    DEFAULT_PORT = 5000

    # LED pin (board LED2)
    LED_GPIO = 52

    def __init__(self, ip=None, port=None):
        self._ip = ip or self.DEFAULT_IP
        self._port = port or self.DEFAULT_PORT
        self._lan = None
        self._server_sock = None
        self._client_sock = None
        self._client_addr = None
        self._ok = False
        self._connected = False

        # RX state machine
        self._state = _S_HDR1
        self._cmd = 0
        self._rx_n = 0
        self._tx_n = 0

        # LED
        self._led = None

    def init(self):
        """Initialize Ethernet LAN and start TCP server."""
        # LED for status indication
        self._led = Pin(self.LED_GPIO, Pin.OUT)
        self._led.value(0)

        # 1) Initialize LAN
        print("[ETH] Initializing LAN...")
        self._lan = network.LAN()
        time.sleep(2)  # Wait for PHY link up

        if not self._lan.isconnected():
            # Retry a few times
            for retry in range(5):
                time.sleep(1)
                if self._lan.isconnected():
                    break

        if self._lan.isconnected():
            ifconfig = self._lan.ifconfig()
            print("[ETH] LAN connected:", ifconfig)
            self._led.value(1)  # LED on = link up
        else:
            # Try setting static IP anyway
            print("[ETH] LAN not linked, setting static IP...")
            self._lan.ifconfig((self._ip, self.DEFAULT_SUBNET,
                                self.DEFAULT_GATEWAY, self.DEFAULT_GATEWAY))
            ifconfig = self._lan.ifconfig()
            print("[ETH] Static config:", ifconfig)

        # Ensure static IP for point-to-point (handle ifconfig returning None)
        try:
            _ifc = self._lan.ifconfig()
            current_ip = _ifc[0] if _ifc else None
        except:
            current_ip = None
        if current_ip != self._ip:
            try:
                print("[ETH] Setting static IP: %s" % self._ip)
                self._lan.ifconfig((self._ip, self.DEFAULT_SUBNET,
                                    self.DEFAULT_GATEWAY, self.DEFAULT_GATEWAY))
                _ifc = self._lan.ifconfig()
                if _ifc: print("[ETH] New config:", _ifc)
            except Exception as _e:
                print("[ETH] Static IP set failed:", _e)

        # 2) Create TCP server socket
        try:
            self._server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._server_sock.bind(("0.0.0.0", self._port))
            self._server_sock.listen(1)
            self._server_sock.setblocking(False)  # Non-blocking accept
            self._ok = True
            print("[ETH] TCP Server listening on port %d" % self._port)
        except Exception as e:
            print("[ETH] Server socket error:", e)
            self._ok = False

        return self

    def _try_accept(self):
        """Try to accept a client connection (non-blocking)."""
        if self._server_sock is None:
            return
        if self._connected:
            return
        try:
            client, addr = self._server_sock.accept()
            self._client_sock = client
            self._client_addr = addr
            self._client_sock.setblocking(False)
            self._connected = True
            self._led.value(1)
            print("[ETH] Client connected from %s:%d" % (addr[0], addr[1]))
        except OSError:
            pass  # No connection pending

    def _disconnect_client(self):
        """Disconnect current client."""
        if self._client_sock:
            try:
                self._client_sock.close()
            except Exception:
                pass
            self._client_sock = None
            self._client_addr = None
        self._connected = False
        self._state = _S_HDR1
        print("[ETH] Client disconnected")

    def check_command(self):
        """
        Poll for commands from CH32.
        Returns CMD_START (0x01), CMD_STOP (0x02), or 0 (nothing).
        """
        if not self._ok:
            return 0

        # Try to accept new client if not connected
        if not self._connected:
            self._try_accept()
            return 0

        # Read data from client
        try:
            data = self._client_sock.recv(64)
            if data is None or len(data) == 0:
                return 0
        except OSError:
            return 0  # No data available
        except Exception:
            self._disconnect_client()
            return 0

        # Parse frame bytes
        for b in data:
            cmd = self._parse(b)
            if cmd != 0:
                return cmd
        return 0

    def _parse(self, b):
        """Parse one byte of incoming frame."""
        if self._state == _S_HDR1:
            if b == HEADER1:
                self._state = _S_HDR2
        elif self._state == _S_HDR2:
            if b == HEADER2:
                self._state = _S_CMD
            else:
                self._state = _S_HDR1
        elif self._state == _S_CMD:
            self._cmd = b
            self._state = _S_FTR1
        elif self._state == _S_FTR1:
            if b == FOOTER1:
                self._state = _S_FTR2
            else:
                self._state = _S_HDR1
        elif self._state == _S_FTR2:
            self._state = _S_HDR1
            if b == FOOTER2:
                self._rx_n += 1
                name = ("START" if self._cmd == CMD_START
                        else "STOP" if self._cmd == CMD_STOP
                        else "0x%02X" % self._cmd)
                print("[ETH RX] Cmd=%s (#%d)" % (name, self._rx_n))
                return self._cmd
        else:
            self._state = _S_HDR1
        return 0

    def send_pest_data(self, pest_type, confidence, count, crop_type=0):
        """Send pest detection result to CH32."""
        if not self._ok or not self._connected:
            return False
        data = bytearray(DATA_LEN)
        data[0] = pest_type & 0xFF
        data[1] = confidence & 0xFF
        data[2] = count & 0xFF
        data[3] = (count >> 8) & 0xFF
        data[4] = crop_type & 0xFF
        checksum = HEADER1 ^ HEADER2 ^ TYPE_PEST ^ DATA_LEN
        for i in range(DATA_LEN):
            checksum ^= data[i]
        frame = bytearray(TX_FRAME_LEN)
        frame[0] = HEADER1
        frame[1] = HEADER2
        frame[2] = TYPE_PEST
        frame[3] = DATA_LEN
        frame[4:12] = data
        frame[12] = checksum
        frame[13] = FOOTER1
        frame[14] = FOOTER2
        try:
            self._client_sock.send(bytes(frame))
            self._tx_n += 1
            print("[ETH TX] PestType=%d Conf=%d%% Count=%d (#%d)" %
                  (pest_type, confidence, count, self._tx_n))
            return True
        except Exception as e:
            print("[ETH TX] Error:", e)
            self._disconnect_client()
            return False

    def send_heartbeat(self):
        """Send heartbeat to CH32."""
        if not self._ok or not self._connected:
            return
        data = bytearray(DATA_LEN)
        checksum = HEADER1 ^ HEADER2 ^ TYPE_HEARTBEAT ^ DATA_LEN
        for i in range(DATA_LEN):
            checksum ^= data[i]
        frame = bytearray(TX_FRAME_LEN)
        frame[0] = HEADER1
        frame[1] = HEADER2
        frame[2] = TYPE_HEARTBEAT
        frame[3] = DATA_LEN
        frame[4:12] = data
        frame[12] = checksum
        frame[13] = FOOTER1
        frame[14] = FOOTER2
        try:
            self._client_sock.send(bytes(frame))
        except Exception:
            self._disconnect_client()

    def send_ack(self, cmd):
        """Send ACK response for a received command."""
        if not self._ok or not self._connected:
            return
        data = bytearray(DATA_LEN)
        data[0] = cmd & 0xFF
        checksum = HEADER1 ^ HEADER2 ^ TYPE_ACK ^ DATA_LEN
        for i in range(DATA_LEN):
            checksum ^= data[i]
        frame = bytearray(TX_FRAME_LEN)
        frame[0] = HEADER1
        frame[1] = HEADER2
        frame[2] = TYPE_ACK
        frame[3] = DATA_LEN
        frame[4:12] = data
        frame[12] = checksum
        frame[13] = FOOTER1
        frame[14] = FOOTER2
        try:
            self._client_sock.send(bytes(frame))
        except Exception:
            self._disconnect_client()

    @property
    def is_ready(self):
        return self._ok

    @property
    def is_connected(self):
        return self._connected

    @property
    def client_addr(self):
        return self._client_addr

    @property
    def tx_count(self):
        return self._tx_n

    @property
    def rx_count(self):
        return self._rx_n

    def deinit(self):
        """Release all resources."""
        self._disconnect_client()
        if self._server_sock:
            try:
                self._server_sock.close()
            except Exception:
                pass
            self._server_sock = None
        if self._lan:
            try:
                self._lan.active(False)
            except Exception:
                pass
            self._lan = None
        if self._led:
            self._led.value(0)
        self._ok = False
        print("[ETH] Deinitialized")