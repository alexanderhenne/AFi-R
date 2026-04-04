#!/usr/bin/env python3
"""Push an image to the LCD display.

Usage:
    sudo python3 push_image.py <image_file>
    sudo python3 push_image.py <image_file> [/dev/ttyACM0]

Supports any format PIL can read (PNG, JPG, BMP, GIF, etc.)
Image is resized to 240x240 and converted to RGB565.
"""

import serial
import struct
import time
import sys

def image_to_rgb565(path):
    """Convert image to 240x240 RGB565 bytes."""
    from PIL import Image
    img = Image.open(path).convert('RGB').resize((240, 240), Image.LANCZOS)
    pixels = img.load()
    data = bytearray(240 * 240 * 2)
    idx = 0
    for y in range(240):
        for x in range(240):
            r, g, b = pixels[x, y]
            # RGB565: RRRRRGGG GGGBBBBB
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            data[idx] = (rgb565 >> 8) & 0xFF  # high byte
            data[idx + 1] = rgb565 & 0xFF       # low byte
            idx += 2
    return bytes(data)

def push_raw(port, data):
    """Send raw RGB565 data using the fast binary protocol.

    Protocol: send 0xFF byte followed by exactly 115200 bytes of RGB565.
    No handshake, no response — just raw speed.
    """
    s = serial.Serial(port, 115200, timeout=2)
    s.write_timeout = 10
    time.sleep(0.1)
    s.reset_input_buffer()

    # Flush any stuck transfer by sending a complete black frame
    s.write(b'\xff' + b'\x00' * 115200)
    s.flush()
    time.sleep(0.7)
    s.reset_input_buffer()

    t0 = time.time()

    # 0xFF triggers immediate binary frame mode on the MCU
    s.write(b'\xff' + data)
    s.flush()

    elapsed = time.time() - t0
    print(f"Sent {len(data)} bytes in {elapsed:.2f}s ({len(data)/elapsed/1024:.0f} KB/s)")
    s.close()
    return True

def main():
    if len(sys.argv) < 2:
        print(f"Usage: sudo {sys.argv[0]} <image_file> [/dev/ttyACM0]")
        sys.exit(1)

    image_path = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 else '/dev/ttyACM0'

    print(f"Converting {image_path} to 240x240 RGB565...")
    t0 = time.time()
    data = image_to_rgb565(image_path)
    print(f"Converted in {time.time()-t0:.2f}s")

    print(f"Pushing to display...")
    push_raw(port, data)

if __name__ == '__main__':
    main()
