#!/usr/bin/env python3
"""Stream an animated GIF to the LCD display.

Usage:
    sudo python3 push_gif.py <gif_file> [speed] [port]

speed: playback speed multiplier (default: 1.0)
Ctrl+C to stop.
"""

import serial
import time
import sys
from PIL import Image

def main():
    if len(sys.argv) < 2:
        print(f"Usage: sudo {sys.argv[0]} <gif_file> [speed] [port]")
        sys.exit(1)

    gif_path = sys.argv[1]
    speed = 1.0
    port = '/dev/ttyACM0'
    for arg in sys.argv[2:]:
        if arg.startswith('/dev/'):
            port = arg
        else:
            try:
                speed = float(arg)
            except ValueError:
                pass

    # Extract and pre-convert all frames
    print(f"Loading {gif_path}...")
    t0 = time.time()
    img = Image.open(gif_path)
    frames = []
    try:
        while True:
            duration = img.info.get('duration', 100) or 100
            frame = img.convert('RGB').resize((240, 240), Image.LANCZOS)
            pixels = frame.load()
            # Pre-build the full packet: 0xFF + 115200 bytes RGB565
            pkt = bytearray(1 + 240 * 240 * 2)
            pkt[0] = 0xFF
            idx = 1
            for y in range(240):
                for x in range(240):
                    r, g, b = pixels[x, y]
                    rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                    pkt[idx] = (rgb565 >> 8) & 0xFF
                    pkt[idx + 1] = rgb565 & 0xFF
                    idx += 2
            frames.append((bytes(pkt), duration))
            img.seek(img.tell() + 1)
    except EOFError:
        pass

    nframes = len(frames)
    total_kb = nframes * 115 // 1
    print(f"{nframes} frames ({total_kb}KB), converted in {time.time()-t0:.1f}s")
    print(f"~1.4 fps (USB Full Speed limit), {speed}x speed")
    print(f"Ctrl+C to stop\n")

    s = serial.Serial(port, 115200, timeout=2)
    s.write_timeout = 10
    time.sleep(0.1)
    s.reset_input_buffer()

    # Flush any stuck transfer by sending a complete black frame.
    # This either completes a partial 0xFF transfer or starts a new one.
    s.write(b'\xff' + b'\x00' * 115200)
    s.flush()
    time.sleep(0.7)
    s.reset_input_buffer()

    loop = 0
    try:
        while True:
            loop += 1
            for i, (pkt, duration) in enumerate(frames):
                t_start = time.time()
                s.write(pkt)
                s.flush()
                send_time = time.time() - t_start
                # At 1.4fps, frame send time dominates — no extra delay needed
                # unless GIF frame duration is very long
                wait = (duration / 1000.0 / speed) - send_time
                if wait > 0:
                    time.sleep(wait)
                fps = 1.0 / (time.time() - t_start)
            print(f"  Loop {loop}, {fps:.1f} fps   ", end='\r')
    except KeyboardInterrupt:
        print(f"\nStopped.")

    s.close()

if __name__ == '__main__':
    main()
