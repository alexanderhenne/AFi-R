#!/usr/bin/env python3
"""Send a text command to the LCD MCU and print the response.

Usage:
    sudo python3 lcd_cmd.py <command> [args...] [--port /dev/ttyACM0]

Commands:
    fwversion, mode, status, listcmd, blreboot, reboot, bench
    rawfb, rawrow=N,HEX

Audio commands:
    tone <freq> <duration_ms>     Play a sine wave (1-8000 Hz, 1-10000 ms)
    audiostop                     Stop audio playback
    audiotest <name>              Play preset: tap, tone, bell, ring, sweep
    volume <0-100>                Set master volume (default 50)
    pcmstream [rate]              Enter raw PCM streaming mode (use stream_audio.py instead)

Examples:
    sudo python3 lcd_cmd.py fwversion
    sudo python3 lcd_cmd.py tone 1000 200
    sudo python3 lcd_cmd.py audiotest bell
    sudo python3 lcd_cmd.py volume 75
"""

import serial
import time
import sys

# Commands that take arguments in "cmd=arg" or "cmd=arg,arg" format
AUDIO_COMMANDS = {
    'tone':      lambda args: f"tone={args[0]},{args[1]}",
    'audiotest': lambda args: f"audiotest={args[0]}",
    'volume':    lambda args: f"volume={args[0]}",
    'pcmstream': lambda args: f"pcmstream={args[0]}" if args else "pcmstream",
}

def main():
    if len(sys.argv) < 2:
        print(__doc__.strip())
        sys.exit(1)

    # Parse --port flag from anywhere in argv
    port = '/dev/ttyACM0'
    argv = []
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == '--port' and i + 1 < len(sys.argv):
            port = sys.argv[i + 1]
            i += 2
        elif sys.argv[i].startswith('/dev/'):
            port = sys.argv[i]
            i += 1
        else:
            argv.append(sys.argv[i])
            i += 1

    if not argv:
        print(__doc__.strip())
        sys.exit(1)

    cmd_name = argv[0]
    cmd_args = argv[1:]

    # Build the wire command
    if cmd_name in AUDIO_COMMANDS:
        try:
            cmd = AUDIO_COMMANDS[cmd_name](cmd_args)
        except (IndexError, KeyError):
            print(f"Usage: {cmd_name} requires arguments. See --help.")
            sys.exit(1)
    else:
        # Pass through as-is (e.g. "fwversion", "rawrow=0,AABB...")
        cmd = ' '.join(argv)

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
