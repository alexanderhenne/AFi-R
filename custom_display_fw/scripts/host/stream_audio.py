#!/usr/bin/env python3
"""Stream audio to the AFi-R LCD speaker via USB CDC.

Decodes any audio file using ffmpeg and streams signed 16-bit LE mono PCM
to the display module's DAC. Supports MP3, WAV, FLAC, OGG, etc.

Usage:
    sudo python3 stream_audio.py <audio_file> [options]

Options:
    --rate RATE     Sample rate in Hz (default: 22050)
    --volume VOL    Speaker volume 0-100 (default: current)
    --port PORT     Serial port (default: /dev/ttyACM0)

Examples:
    sudo python3 stream_audio.py song.mp3
    sudo python3 stream_audio.py song.flac --rate 32000 --volume 80
    cat audio.raw | sudo python3 stream_audio.py - --rate 16000

Requires: ffmpeg (for file decoding), pyserial
"""

import serial
import subprocess
import sys
import time
import signal
import os

DEFAULT_RATE = 22050
DEFAULT_PORT = '/dev/ttyACM0'
CHUNK_BYTES = 1024  # ~23ms at 22050 Hz


def parse_args():
    args = {'file': None, 'rate': DEFAULT_RATE, 'volume': None, 'port': DEFAULT_PORT}
    i = 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == '--rate' and i + 1 < len(sys.argv):
            args['rate'] = int(sys.argv[i + 1]); i += 2
        elif a == '--volume' and i + 1 < len(sys.argv):
            args['volume'] = int(sys.argv[i + 1]); i += 2
        elif a == '--port' and i + 1 < len(sys.argv):
            args['port'] = sys.argv[i + 1]; i += 2
        elif a.startswith('/dev/'):
            args['port'] = a; i += 1
        elif args['file'] is None:
            args['file'] = a; i += 1
        else:
            i += 1
    return args


def send_cmd(ser, cmd, timeout=1.0):
    """Send a text command and return the response."""
    ser.reset_input_buffer()
    ser.write(f'{cmd}\r\n'.encode())
    time.sleep(0.1)
    deadline = time.time() + timeout
    resp = b''
    while time.time() < deadline:
        n = ser.in_waiting
        if n:
            resp += ser.read(n)
            if b'\n' in resp:
                break
        else:
            time.sleep(0.01)
    return resp.decode('ascii', errors='replace').strip()


def main():
    args = parse_args()
    if not args['file']:
        print(__doc__.strip())
        sys.exit(1)

    rate = args['rate']
    use_stdin = (args['file'] == '-')

    # Open serial port
    ser = serial.Serial(args['port'], 115200, timeout=2,
                        write_timeout=5)
    time.sleep(0.3)
    ser.reset_input_buffer()

    # Set volume if requested
    if args['volume'] is not None:
        resp = send_cmd(ser, f"volume={args['volume']}")
        if 'ok' not in resp:
            print(f"Warning: volume command failed: {resp}")

    # Start PCM streaming mode on the device
    resp = send_cmd(ser, f'pcmstream={rate}')
    if 'ok' not in resp:
        print(f"Failed to start streaming: {resp}")
        ser.close()
        sys.exit(1)

    # Small delay to let the firmware switch modes
    time.sleep(0.05)

    # Open audio source
    if use_stdin:
        # Raw s16le from stdin — user must provide correct format
        audio_src = sys.stdin.buffer
        ffmpeg_proc = None
        print(f"Streaming raw PCM from stdin at {rate} Hz... (Ctrl+C to stop)")
    else:
        # Decode with ffmpeg
        ffmpeg_cmd = [
            'ffmpeg', '-v', 'error',
            '-i', args['file'],
            '-f', 's16le', '-acodec', 'pcm_s16le',
            '-ar', str(rate), '-ac', '1',
            '-'
        ]
        try:
            ffmpeg_proc = subprocess.Popen(
                ffmpeg_cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        except FileNotFoundError:
            print("Error: ffmpeg not found. Install it with your package manager.")
            ser.close()
            sys.exit(1)
        audio_src = ffmpeg_proc.stdout
        print(f"Streaming '{args['file']}' at {rate} Hz... (Ctrl+C to stop)")

    # Stream audio data
    bytes_sent = 0
    t0 = time.time()
    stopped = False

    def cleanup(signum=None, frame=None):
        nonlocal stopped
        if stopped:
            return
        stopped = True
        if ffmpeg_proc:
            ffmpeg_proc.kill()
            ffmpeg_proc.wait()
        # Let the firmware drain its buffer and auto-stop via timeout
        time.sleep(0.6)
        # Explicitly stop (in case timeout hasn't fired yet)
        try:
            send_cmd(ser, 'audiostop')
        except Exception:
            pass
        elapsed = time.time() - t0
        print(f"\nDone. Sent {bytes_sent:,} bytes ({elapsed:.1f}s)")
        ser.close()

    signal.signal(signal.SIGINT, cleanup)

    try:
        while not stopped:
            data = audio_src.read(CHUNK_BYTES)
            if not data:
                break
            # Ensure even byte count (16-bit samples)
            if len(data) % 2:
                data = data[:-1]
            if not data:
                break
            try:
                ser.write(data)
                bytes_sent += len(data)
            except serial.SerialTimeoutException:
                # Write blocked too long — device buffer full, retry
                time.sleep(0.01)
                continue
    except (BrokenPipeError, OSError):
        pass

    cleanup()


if __name__ == '__main__':
    main()
