#!/usr/bin/env python3
"""Display the currently playing Spotify track on the AFi-R LCD.

Listens for track changes via D-Bus MPRIS (push, no polling) and renders
the album art with song/artist text to the 240x240 display.

Supports two modes:
  - Direct USB: display connected to this PC via micro-USB
  - SSH: display connected to the router, frames pushed over SSH

Usage:
    # Direct USB
    python3 spotify_display.py --port /dev/ttyACM0

    # Via router SSH (default)
    python3 spotify_display.py --ssh amplifi

Requires:
    - Spotify desktop client running (exposes MPRIS on D-Bus)
    - python-dbus, Pillow, requests
    - pyserial (direct mode only)
    - SSH key auth to the router (SSH mode)
"""

import dbus
import dbus.mainloop.glib
from gi.repository import GLib
from PIL import Image, ImageDraw, ImageFont
import requests
import subprocess
import threading
import sys
import os
import io
import time

DEFAULT_PORT = '/dev/ttyACM0'
DISPLAY_SIZE = 240
ART_SIZE = 140  # album art area (smaller to fit circular display)
TEXT_AREA = DISPLAY_SIZE - ART_SIZE  # text area below art

MPRIS_BUS = 'org.mpris.MediaPlayer2.spotify'
MPRIS_PATH = '/org/mpris/MediaPlayer2'
MPRIS_IFACE = 'org.mpris.MediaPlayer2.Player'
PROPERTIES_IFACE = 'org.freedesktop.DBus.Properties'

# Try to find a decent font
FONT_PATHS = [
    '/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc',     # CJK support
    '/usr/share/fonts/TTF/DejaVuSans-Bold.ttf',
    '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
    '/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf',
    '/usr/share/fonts/TTF/Roboto-Bold.ttf',
]
FONT_PATHS_REGULAR = [
    '/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc',   # CJK support
    '/usr/share/fonts/TTF/DejaVuSans.ttf',
    '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
    '/usr/share/fonts/dejavu/DejaVuSans.ttf',
    '/usr/share/fonts/TTF/Roboto-Regular.ttf',
]


def find_font(paths, size):
    for p in paths:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def rgb888_to_rgb565be(img):
    """Convert a PIL RGB image to RGB565 big-endian bytes."""
    pixels = img.tobytes()
    out = bytearray(img.width * img.height * 2)
    for i in range(0, len(pixels), 3):
        r, g, b = pixels[i], pixels[i + 1], pixels[i + 2]
        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        j = (i // 3) * 2
        out[j] = (rgb565 >> 8) & 0xFF
        out[j + 1] = rgb565 & 0xFF
    return bytes(out)


def fetch_album_art(url):
    """Download album art and return as PIL Image, or None on failure."""
    try:
        resp = requests.get(url, timeout=5)
        resp.raise_for_status()
        return Image.open(io.BytesIO(resp.content)).convert('RGB')
    except Exception:
        return None


def render_frame(title, artist, art_img=None):
    """Render a 240x240 frame with album art and text.
    Layout is optimized for the AFi-R's circular display window."""
    frame = Image.new('RGB', (DISPLAY_SIZE, DISPLAY_SIZE), (0, 0, 0))
    draw = ImageDraw.Draw(frame)

    font_title = find_font(FONT_PATHS, 15)
    font_artist = find_font(FONT_PATHS_REGULAR, 12)
    text_max_w = 180

    def truncate(text, font, max_w):
        if draw.textlength(text, font=font) <= max_w:
            return text
        while len(text) > 0 and draw.textlength(text + '...', font=font) > max_w:
            text = text[:-1]
        return text + '...'

    title_str = truncate(title, font_title, text_max_w)
    artist_str = truncate(artist, font_artist, text_max_w)

    # Measure actual text height and center everything vertically
    title_bbox = draw.textbbox((0, 0), title_str, font=font_title)
    artist_bbox = draw.textbbox((0, 0), artist_str, font=font_artist)
    title_h = title_bbox[3] - title_bbox[1]
    artist_h = artist_bbox[3] - artist_bbox[1]
    gap = 8
    line_gap = 4
    total_h = ART_SIZE + gap + title_h + line_gap + artist_h
    top_y = (DISPLAY_SIZE - total_h) // 2

    # Album art (centered)
    if art_img:
        art = art_img.resize((ART_SIZE, ART_SIZE), Image.LANCZOS)
        frame.paste(art, ((DISPLAY_SIZE - ART_SIZE) // 2, top_y))

    # Text (centered below art)
    y = top_y + ART_SIZE + gap
    tw = draw.textlength(title_str, font=font_title)
    draw.text(((DISPLAY_SIZE - tw) / 2, y), title_str,
              fill=(255, 255, 255), font=font_title)
    y += title_h + line_gap
    aw = draw.textlength(artist_str, font=font_artist)
    draw.text(((DISPLAY_SIZE - aw) / 2, y), artist_str,
              fill=(180, 180, 180), font=font_artist)

    # Flip horizontally — LCD MADCTL 0xC8 mirrors X axis
    frame = frame.transpose(Image.FLIP_LEFT_RIGHT)

    return frame


def push_frame_serial(ser, frame):
    """Push a 240x240 PIL image to the display via local serial."""
    data = rgb888_to_rgb565be(frame)
    ser.write(b'\xff')
    ser.write(data)


def push_frame_ssh(ssh_pipe, frame):
    """Push a 240x240 PIL image via a persistent SSH pipe."""
    data = b'\xff' + rgb888_to_rgb565be(frame)
    try:
        ssh_pipe.stdin.write(data)
        ssh_pipe.stdin.flush()
    except BrokenPipeError:
        pass


class SpotifyDisplay:
    def __init__(self, port=None, ssh_host=None):
        self.ssh_host = ssh_host
        self.ssh_pipe = None
        self.ser = None

        if ssh_host:
            print(f'Connecting to {ssh_host}...')
            # Unstick cdc_acm driver in case a previous session left it stale
            subprocess.run(
                ['ssh', ssh_host, 'head -c1 < /dev/ttyACM0 > /dev/null'],
                timeout=5, check=False, capture_output=True)
            # Single persistent SSH session: bidirectional pipe to /dev/ttyACM0
            # Use 'read' loop for unbuffered line output (cat would pipe-buffer)
            # Use 'dd' for writing (shell redirect '>' fails with permission error)
            self.ssh_pipe = subprocess.Popen(
                ['ssh', ssh_host,
                 'while IFS= read -r line; do printf "%s\\n" "$line"; done '
                 '</dev/ttyACM0 & dd of=/dev/ttyACM0 bs=4096 2>/dev/null'],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )
            time.sleep(1)
            print(f'Connected to {ssh_host}')
        else:
            import serial as pyserial
            self.ser = pyserial.Serial(port, 115200, timeout=2, write_timeout=5)
            time.sleep(0.3)
            self.ser.reset_input_buffer()
            print(f'Using serial port {port}')

        self.current_track_id = None
        self.art_cache = {}  # url -> PIL Image
        self.last_touch = 0
        self.controls_visible = False
        self.controls_shown_at = 0
        self.last_frame = None  # cache for redisplay after controls dismiss
        self.waiting_for_lift = False  # wait for finger lift before accepting next tap

        # Show startup screen
        frame = render_frame('Waiting for Spotify...', 'Start playing a track')
        self.push(frame)

    def push(self, frame):
        if self.ssh_pipe:
            push_frame_ssh(self.ssh_pipe, frame)
        else:
            push_frame_serial(self.ser, frame)

    def render_controls(self):
        """Render a control overlay with prev / play-pause / next buttons.
        Uses drawn shapes (triangles, bars) instead of text for consistent centering.
        Sized to fit within the circular display window."""
        frame = Image.new('RGB', (DISPLAY_SIZE, DISPLAY_SIZE), (0, 0, 0))
        draw = ImageDraw.Draw(frame)

        btn_h = 50
        gap = 12
        total_h = btn_h * 3 + gap * 2
        top_y = (DISPLAY_SIZE - total_h) // 2
        btn_w = 140
        btn_x = (DISPLAY_SIZE - btn_w) // 2

        try:
            status = self.get_metadata()[0]
            is_paused = (status == 'Paused')
        except Exception:
            is_paused = False

        btn_colors = [(60, 60, 60), (50, 50, 50), (60, 60, 60)]
        icon_color = (255, 255, 255)
        cx = DISPLAY_SIZE // 2
        s = 10  # icon half-size

        for i in range(3):
            y = top_y + i * (btn_h + gap)
            draw.rounded_rectangle(
                [btn_x, y, btn_x + btn_w, y + btn_h],
                radius=btn_h // 2, fill=btn_colors[i])
            cy = y + btn_h // 2

            if i == 0:  # Previous: two left-pointing triangles
                draw.polygon([(cx - 2, cy - s), (cx - 2, cy + s),
                              (cx - s - 2, cy)], fill=icon_color)
                draw.polygon([(cx + s - 2, cy - s), (cx + s - 2, cy + s),
                              (cx - 2, cy)], fill=icon_color)
            elif i == 1:  # Play or Pause
                if is_paused:
                    # Play: right-pointing triangle
                    draw.polygon([(cx - s + 2, cy - s), (cx - s + 2, cy + s),
                                  (cx + s + 2, cy)], fill=icon_color)
                else:
                    # Pause: two vertical bars
                    bar_w, bar_gap = 5, 4
                    draw.rectangle([cx - bar_gap - bar_w, cy - s,
                                    cx - bar_gap, cy + s], fill=icon_color)
                    draw.rectangle([cx + bar_gap, cy - s,
                                    cx + bar_gap + bar_w, cy + s], fill=icon_color)
            elif i == 2:  # Next: two right-pointing triangles
                draw.polygon([(cx + 2, cy - s), (cx + 2, cy + s),
                              (cx + s + 2, cy)], fill=icon_color)
                draw.polygon([(cx - s + 2, cy - s), (cx - s + 2, cy + s),
                              (cx + 2, cy)], fill=icon_color)

        frame = frame.transpose(Image.FLIP_LEFT_RIGHT)
        return frame

    def _button_zones(self):
        btn_h = 50
        gap = 12
        btn_w = 140
        total_h = btn_h * 3 + gap * 2
        top_y = (DISPLAY_SIZE - total_h) // 2
        # Button X in render coords: btn_x to btn_x+btn_w
        # After horizontal flip, in touch coords: (239-btn_x-btn_w) to (239-btn_x)
        btn_x = (DISPLAY_SIZE - btn_w) // 2
        tx_min = DISPLAY_SIZE - 1 - (btn_x + btn_w)
        tx_max = DISPLAY_SIZE - 1 - btn_x
        return [
            (top_y, top_y + btn_h),
            (top_y + btn_h + gap, top_y + 2 * btn_h + gap),
            (top_y + 2 * (btn_h + gap), top_y + 3 * btn_h + 2 * gap),
        ], tx_min, tx_max

    def spotify_command(self, method):
        """Send a command to Spotify via MPRIS D-Bus."""
        try:
            bus = dbus.SessionBus()
            proxy = bus.get_object(MPRIS_BUS, MPRIS_PATH)
            player = dbus.Interface(proxy, MPRIS_IFACE)
            getattr(player, method)()
        except Exception as e:
            print(f'Spotify command {method} failed: {e}')

    def on_touch(self, x, y):
        """First tap: show controls. Second tap: execute button action."""
        if not self.controls_visible:
            # Show control overlay
            self.controls_visible = True
            self.controls_shown_at = time.time()
            self.push(self.render_controls())
            return

        # Controls are visible — check if a button was tapped
        y_zones, tx_min, tx_max = self._button_zones()
        actions = ['Previous', 'PlayPause', 'Next']
        self.controls_visible = False
        if tx_min <= x <= tx_max:
            for i, (zy0, zy1) in enumerate(y_zones):
                if zy0 <= y <= zy1:
                    print(f'Touch: {actions[i]}')
                    self.spotify_command(actions[i])
                    return
        # Tapped outside buttons — just dismiss
        if self.last_frame:
            self.push(self.last_frame)

    def start_touch_listener(self):
        """Background thread: read touch events from the persistent SSH pipe."""
        def listen():
            pipe = self.ssh_pipe
            buf = b''
            print('Touch listener: started')
            try:
                for chunk in iter(lambda: pipe.stdout.read(1), b''):
                    buf += chunk
                    while b'\n' in buf:
                        line, buf = buf.split(b'\n', 1)
                        line = line.strip().decode('ascii', errors='replace')
                        if line.startswith('tp '):
                            if ',up' in line:
                                self.waiting_for_lift = False
                            elif ',down' in line and not self.waiting_for_lift:
                                parts = line[3:].split(',')
                                if len(parts) >= 2:
                                    try:
                                        x, y = int(parts[0]), int(parts[1])
                                        self.waiting_for_lift = True
                                        self.on_touch(x, y)
                                    except ValueError:
                                        pass
            except Exception as e:
                print(f'Touch listener error: {e}')

        t = threading.Thread(target=listen, daemon=True)
        t.start()

    def on_properties_changed(self, interface, changed, invalidated):
        if interface != MPRIS_IFACE:
            return
        if 'Metadata' not in changed and 'PlaybackStatus' not in changed:
            return

        try:
            self.update_display()
        except Exception as e:
            print(f'Error updating display: {e}')

    def get_metadata(self):
        bus = dbus.SessionBus()
        proxy = bus.get_object(MPRIS_BUS, MPRIS_PATH)
        props = dbus.Interface(proxy, PROPERTIES_IFACE)

        status = str(props.Get(MPRIS_IFACE, 'PlaybackStatus'))
        metadata = props.Get(MPRIS_IFACE, 'Metadata')

        title = str(metadata.get('xesam:title', 'Unknown'))
        artists = metadata.get('xesam:artist', ['Unknown'])
        artist = str(artists[0]) if artists else 'Unknown'
        art_url = str(metadata.get('mpris:artUrl', ''))
        track_id = str(metadata.get('mpris:trackid', ''))

        return status, title, artist, art_url, track_id

    def update_display(self):
        status, title, artist, art_url, track_id = self.get_metadata()

        if status == 'Stopped':
            frame = render_frame('Spotify', 'Not playing')
            self.push(frame)
            self.current_track_id = None
            return

        # Skip if same track and same status (avoid re-rendering on seek/volume)
        track_key = track_id + ':' + status
        if track_key == self.current_track_id:
            return
        self.current_track_id = track_key

        # Fetch album art
        art_img = None
        if art_url:
            if art_url in self.art_cache:
                art_img = self.art_cache[art_url]
            else:
                art_img = fetch_album_art(art_url)
                if art_img:
                    self.art_cache[art_url] = art_img
                    # Keep cache small
                    if len(self.art_cache) > 20:
                        oldest = next(iter(self.art_cache))
                        del self.art_cache[oldest]

        display_title = ('|| ' + title) if status == 'Paused' else title
        frame = render_frame(display_title, artist, art_img)
        self.last_frame = frame
        if not self.controls_visible:
            self.push(frame)
        state = 'Paused' if status == 'Paused' else 'Playing'
        print(f'{state}: {artist} - {title}')

    def run(self):
        dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
        bus = dbus.SessionBus()

        # Listen for property changes on the Spotify MPRIS interface
        bus.add_signal_receiver(
            self.on_properties_changed,
            signal_name='PropertiesChanged',
            dbus_interface=PROPERTIES_IFACE,
            bus_name=MPRIS_BUS,
            path=MPRIS_PATH,
        )

        # Start touch listener (SSH mode only)
        if self.ssh_host:
            self.start_touch_listener()

        # Show current track immediately
        try:
            self.update_display()
        except dbus.exceptions.DBusException:
            print('Spotify not running yet. Waiting...')

        # Auto-dismiss controls after timeout
        def check_controls_timeout():
            if self.controls_visible and (time.time() - self.controls_shown_at) > 4:
                self.controls_visible = False
                if self.last_frame:
                    self.push(self.last_frame)
            return True  # keep timer running
        GLib.timeout_add(500, check_controls_timeout)

        print('Listening for Spotify track changes... (Ctrl+C to stop)')
        loop = GLib.MainLoop()
        try:
            loop.run()
        except KeyboardInterrupt:
            print('\nStopped.')
            self.ser.close()


def main():
    port = None
    ssh_host = None

    i = 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == '--port' and i + 1 < len(sys.argv):
            port = sys.argv[i + 1]; i += 2
        elif a == '--ssh' and i + 1 < len(sys.argv):
            ssh_host = sys.argv[i + 1]; i += 2
        elif a.startswith('/dev/'):
            port = a; i += 1
        else:
            i += 1

    if not port and not ssh_host:
        ssh_host = 'amplifi'  # default: SSH to router

    display = SpotifyDisplay(port=port, ssh_host=ssh_host)
    display.run()


if __name__ == '__main__':
    main()
