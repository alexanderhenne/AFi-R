#!/usr/bin/env python3
"""
Flash LCD MCU firmware via the bootloader's serial protocol.

Usage:
    sudo python3 flash_lcd.py <firmware.bin> [/dev/ttyACM0]
"""

import serial
import hashlib
import base64
import sys
import time

def read_response(ser, timeout=5):
    """Read until we get a complete response line."""
    ser.timeout = timeout
    data = b''
    deadline = time.time() + timeout
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            data += chunk
            if b'\n\r' in data or b'\r\n' in data:
                break
        time.sleep(0.01)
    return data.decode('ascii', errors='replace').strip()

def send_cmd(ser, cmd):
    """Send a command and return the response."""
    ser.reset_input_buffer()
    ser.write(f'{cmd}\r\n'.encode())
    time.sleep(0.3)
    resp = read_response(ser)
    return resp

def main():
    if len(sys.argv) < 2:
        print(f"Usage: sudo {sys.argv[0]} <firmware.bin> [/dev/ttyACM0]")
        sys.exit(1)

    fw_path = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 else '/dev/ttyACM0'

    # Read firmware
    with open(fw_path, 'rb') as f:
        fw = f.read()

    fw_size = len(fw)
    fw_md5 = hashlib.md5(fw).hexdigest()
    print(f"Firmware: {fw_path}")
    print(f"Size: {fw_size} bytes")
    print(f"MD5: {fw_md5}")

    # Open serial
    ser = serial.Serial(port, 115200, timeout=2)
    time.sleep(0.5)
    ser.reset_input_buffer()

    # Check mode
    resp = send_cmd(ser, 'mode')
    print(f"Mode: {resp}")

    resp = send_cmd(ser, 'fwversion')
    print(f"FW Version: {resp}")

    # If in APP mode, reboot into bootloader first
    if 'APP' in resp or 'APP' in send_cmd(ser, 'mode'):
        print(f"\nIn APP mode — rebooting into bootloader...")
        ser.write(b'blreboot\r\n')
        ser.close()
        # Wait for MCU to reboot and USB to re-enumerate
        print(f"Waiting for bootloader to come up...")
        time.sleep(4)
        # Reopen serial
        for attempt in range(10):
            try:
                ser = serial.Serial(port, 115200, timeout=2)
                time.sleep(0.5)
                ser.reset_input_buffer()
                resp = send_cmd(ser, 'mode')
                print(f"Mode: {resp}")
                if 'BLD' in resp:
                    break
            except Exception:
                time.sleep(1)
        else:
            print("ERROR: Could not reconnect to bootloader.")
            sys.exit(1)

    # Now in bootloader mode — initiate firmware transfer
    print(f"\nSending: forcefwupgrade={fw_size}")
    ser.reset_input_buffer()
    ser.write(f'forcefwupgrade={fw_size}\r\n'.encode())
    time.sleep(1)
    resp = read_response(ser, timeout=5)
    print(f"Response: {resp}")

    if 'wait' not in resp.lower() and 'ok' not in resp.lower():
        print(f"ERROR: Unexpected response. Aborting.")
        ser.close()
        sys.exit(1)

    print(f"\nMCU ready for transfer. Sending {fw_size} raw bytes...")

    # Send raw binary data in chunks
    chunk_size = 64  # Small chunks to avoid USB CDC overflow
    sent = 0
    start_time = time.time()

    for offset in range(0, fw_size, chunk_size):
        chunk = fw[offset:offset + chunk_size]
        ser.write(chunk)

        sent += len(chunk)

        if offset % (chunk_size * 200) == 0:
            elapsed = time.time() - start_time
            pct = sent * 100 / fw_size
            rate = sent / elapsed if elapsed > 0 else 0
            print(f"  {pct:5.1f}% ({sent}/{fw_size}) {rate/1024:.1f} KB/s")

        # Tiny delay every few chunks to let MCU process
        if offset % (chunk_size * 16) == 0:
            time.sleep(0.001)

    elapsed = time.time() - start_time
    print(f"  100.0% ({sent}/{fw_size}) in {elapsed:.1f}s")

    # Wait for MCU to verify hash
    print(f"\nWaiting for MCU to verify hash...")
    time.sleep(3)
    resp = read_response(ser, timeout=10)
    print(f"Response: {resp}")

    # Read any additional responses
    time.sleep(2)
    remaining = ser.read(ser.in_waiting or 0)
    if remaining:
        print(f"Additional: {remaining.decode('ascii', errors='replace').strip()}")

    # Reboot MCU into the new firmware
    print(f"Sending startapp to reboot into new firmware...")
    try:
        ser.write(b'startapp\r\n')
        time.sleep(0.5)
        ser.close()
    except Exception:
        pass  # MCU reboots immediately, USB disconnects

    print(f"\nDone. MCU rebooting into new firmware.")

if __name__ == '__main__':
    main()
