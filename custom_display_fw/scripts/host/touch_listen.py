#!/usr/bin/env python3
"""Listen for touch events from the AFi-R display module.

Prints touch coordinates and events (down/move/up) as they arrive.

Usage:
    sudo python3 touch_listen.py [--port /dev/ttyACM0]

Examples:
    sudo python3 touch_listen.py
    sudo python3 touch_listen.py --port /dev/ttyACM1
"""

import serial
import sys
import time


def main():
    port = '/dev/ttyACM0'
    for i, a in enumerate(sys.argv[1:], 1):
        if a == '--port' and i < len(sys.argv) - 1:
            port = sys.argv[i + 1]
        elif a.startswith('/dev/'):
            port = a

    ser = serial.Serial(port, 115200, timeout=0.1)
    time.sleep(0.3)
    ser.reset_input_buffer()

    # Check touch controller status
    ser.write(b'tpstatus\r\n')
    time.sleep(0.3)
    status = ser.read(ser.in_waiting).decode('ascii', errors='replace').strip()
    print(f"Touch controller: {status}")

    if 'none' in status.lower():
        print("No touch controller detected.")
        ser.close()
        sys.exit(1)

    print("Listening for touch events... (Ctrl+C to stop)\n")

    buf = b''
    try:
        while True:
            data = ser.read(ser.in_waiting or 1)
            if not data:
                continue
            buf += data
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                line = line.strip().decode('ascii', errors='replace')
                if line.startswith('tp '):
                    print(line)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        ser.close()


if __name__ == '__main__':
    main()
