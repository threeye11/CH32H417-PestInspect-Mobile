"""
Ethernet-free entry point.
All communication now via UART (see main_lvgl.py).
Ethernet and video streaming removed - image frames sent via UART on result change.
"""
import sys, os
os.chdir("/sdcard")
sys.path.insert(0, "/sdcard")

import main_lvgl as ml

def main():
    ml.main()

if __name__ == "__main__":
    main()