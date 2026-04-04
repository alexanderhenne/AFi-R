#!/usr/bin/env python3
"""Send a text command to the LCD MCU and print the response.

Usage:
    sudo python3 lcd_cmd.py <command> [port]

Examples:
    sudo python3 lcd_cmd.py fwversion
    sudo python3 lcd_cmd.py mode
    sudo python3 lcd_cmd.py status
    sudo python3 lcd_cmd.py listcmd
    sudo python3 lcd_cmd.py blreboot
"""

import serial
import time
import sys

def main():
    if len(sys.argv) < 2:
        print(f"Usage: sudo {sys.argv[0]} <command> [/dev/ttyACM0]")
        sys.exit(1)

    cmd = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2].startswith('/dev/') else '/dev/ttyACM0'

    s = serial.Serial(port, 115200, timeout=2)
    time.sleep(0.3)
    s.reset_input_buffer()

    s.write(f'{cmd}\r\n'.encode())
    time.sleep(0.5)

    # Read all available response lines
    while True:
        data = s.read(s.in_waiting or 1)
        if not data:
            break
        print(data.decode('ascii', errors='replace'), end='')
        time.sleep(0.1)

    s.close()

if __name__ == '__main__':
    main()
