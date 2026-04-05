# Spotify Display for AmpliFi AFi-R

Shows the currently playing Spotify track on the AFi-R's 240x240 circular LCD — album art, song title, and artist name. Updates instantly on track changes via D-Bus MPRIS (no polling, no API keys). Tap the screen to control playback.

https://github.com/user-attachments/assets/e399d196-e890-4b34-b4c4-2bdae00b6da4

## How it works

```
Spotify Desktop → D-Bus MPRIS signal → spotify_display.py
    → fetch album art → render 240x240 frame → push to LCD via SSH
```

Track changes are push-based (D-Bus signal), so the display updates instantly when you skip, pause, or start a new track.

## Requirements

- [Custom display firmware](../custom_display_fw/) flashed to the display module
- Spotify desktop client running on the same machine
- SSH key auth to the router (display connected internally)
- Python 3 with `python-dbus`, `Pillow`, `requests`

### Install (Arch Linux)

```bash
sudo pacman -S python-dbus python-pillow python-pyserial python-requests
```

## Usage

```bash
# Display connected to the router (default, pushes via SSH)
python3 spotify_display.py

# Specify SSH host
python3 spotify_display.py --ssh amplifi

# Display connected directly to this PC via USB
sudo python3 spotify_display.py --port /dev/ttyACM0
```

Start playing music in Spotify — the display updates automatically. Press Ctrl+C to stop.

## Touch controls

Tap anywhere on the screen to bring up playback controls:

| Button | Action |
|--------|--------|
| **<<** | Previous track |
| **\|\|** / **>** | Pause / Resume |
| **>>** | Next track |

Tap a button to execute the action, or tap outside the buttons to dismiss. Controls auto-dismiss after 4 seconds.

## Notes

- Works with any MPRIS-compatible player (Spotify, VLC, Firefox, mpv, etc.) — just change `MPRIS_BUS` in the script
- Album art is cached in memory to avoid re-downloading on pause/resume
- No Spotify API key or OAuth needed — uses local D-Bus, not the web API
- Requires the Spotify desktop client (not the web player)
- Uses a single persistent SSH connection for both frame pushes and touch event reading
