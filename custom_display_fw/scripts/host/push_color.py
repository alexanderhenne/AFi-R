#!/usr/bin/env python3
"""Push a solid color to the LCD display.

Usage:
    sudo python3 push_color.py [color]

Colors: red, green, blue, white, black, cyan, magenta, yellow
Or pass a hex RGB565 value like: F800
"""

import serial
import time
import sys

COLORS = {
    'red':     b'\xF8\x00',
    'green':   b'\x07\xE0',
    'blue':    b'\x00\x1F',
    'white':   b'\xFF\xFF',
    'black':   b'\x00\x00',
    'cyan':    b'\x07\xFF',
    'magenta': b'\xF8\x1F',
    'yellow':  b'\xFF\xE0',
    'orange':  b'\xFD\x20',
}

def main():
    color_name = sys.argv[1] if len(sys.argv) > 1 else 'red'

    if color_name in COLORS:
        pixel = COLORS[color_name]
    elif len(color_name) == 4:
        pixel = bytes.fromhex(color_name)
    else:
        print(f"Unknown color: {color_name}")
        print(f"Available: {', '.join(COLORS.keys())}")
        sys.exit(1)

    print(f"Pushing {color_name} (RGB565: {pixel.hex().upper()})...")

    s = serial.Serial('/dev/ttyACM0', 115200, timeout=2)
    s.write_timeout = 10
    time.sleep(1)
    s.reset_input_buffer()

    # Send color frame using 0xFF binary protocol
    data = pixel * (240 * 240)  # 57600 pixels = 115200 bytes
    s.write(b'\xff' + data)
    s.flush()
    time.sleep(1)  # Wait for MCU to finish receiving

    print(f"Done")
    s.close()

if __name__ == '__main__':
    main()
