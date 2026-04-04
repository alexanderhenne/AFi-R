# Custom Display Firmware for AmpliFi AFi-R

Custom bare-metal STM32F205 firmware that replaces the stock display module firmware. The display module contains an LCD screen, capacitive touchscreen, and speaker — all controlled by the STM32 MCU.

Currently implemented:
- **Push any image** (PNG, JPG, BMP, etc.) to the 240x240 LCD
- **Stream animated GIFs** at ~1.4 fps
- **Solid color fills** for testing
- **USB CDC serial** compatible with the stock bootloader

Not yet implemented (hardware is present and accessible — see [reverse engineering notes](../docs/lcd-display-system.md)):
- **Speaker/audio output** — STM32 DAC channel 2 (PA5), 12-bit, 16 kHz via TIM6. Stock firmware has built-in tones (bell, ring, tap, etc.)
- **Touchscreen input** — Cypress TrueTouch Gen5 (cyttsp5) via I2C
- **Bootloader-safe**: can always revert to the stock firmware

## Requirements

- `arm-none-eabi-gcc` (ARM bare-metal toolchain)
- [TinyUSB](https://github.com/hathach/tinyusb) cloned to `/tmp/tinyusb`
- Python 3 with `pyserial` and `Pillow`
- The display board connected via micro-USB

### Install dependencies

```bash
# Arch Linux
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-newlib

# TinyUSB
git clone --depth 1 https://github.com/hathach/tinyusb.git /tmp/tinyusb

# Python packages
pip install pyserial Pillow
```

## Building

```bash
make clean && make
```

Produces `lcd_firmware.bin` (916,480 bytes — must be this exact size for the bootloader to accept it).

## Flashing

### Via bootloader serial protocol (normal method)

```bash
sudo python3 scripts/host/flash_lcd.py lcd_firmware.bin
```

This automatically handles APP → bootloader reboot if needed.

### Via DFU (recovery method)

If the firmware is corrupted and the MCU doesn't respond:

1. Short the **J7** pads on the display board (BOOT0 jumper)
2. Connect display board to PC via micro-USB
3. Flash:
   ```bash
   sudo dfu-util -a 0 -S <serial> -s 0x08020000:leave -D lcd_firmware.bin
   ```
4. Remove J7 short, reconnect

### Restoring stock firmware

```bash
sudo python3 scripts/host/flash_lcd.py /path/to/AFi-R_LCD_original.bin
```

## Usage

### Push an image

```bash
sudo python3 scripts/host/push_image.py photo.jpg
```

Any image format supported by PIL. Automatically resized to 240x240.

### Push a solid color

```bash
sudo python3 scripts/host/push_color.py red
sudo python3 scripts/host/push_color.py green
sudo python3 scripts/host/push_color.py blue
sudo python3 scripts/host/push_color.py white
sudo python3 scripts/host/push_color.py FF00    # Raw RGB565 hex
```

### Stream an animated GIF

```bash
sudo python3 scripts/host/push_gif.py animation.gif        # Normal speed
sudo python3 scripts/host/push_gif.py animation.gif 2.0     # 2x speed
```

Press Ctrl+C to stop. The display recovers automatically on the next push.

### Serial commands (for scripting)

The firmware accepts text commands over `/dev/ttyACM0` with `\r\n` termination:

| Command | Response | Description |
|---|---|---|
| `fwversion` | `[info:] aFiDsp.AP.99.99` | Firmware version |
| `mode` | `[info:] APP` | Current mode |
| `status` | `[info:] 0x00000000` | Status |
| `rawfb` | `ok 115200` then reads 115200 bytes | Push raw RGB565 frame |
| `rawrow=N,HEXDATA` | — | Push single row (960 hex chars) |
| `blreboot` | Reboots into bootloader | For firmware updates |
| `reboot` | Reboots | Simple restart |
| `hostmsg={...}` | `{"status":"ok"}` | Stock uictld compatibility |

Example with pyserial:
```python
import serial, time
s = serial.Serial('/dev/ttyACM0', 115200, timeout=2)
time.sleep(0.3)
s.reset_input_buffer()
s.write(b'fwversion\r\n')
time.sleep(0.5)
print(s.read(s.in_waiting))  # b'[info:] aFiDsp.AP.99.99\r\n'
s.close()
```

### Binary protocol (fastest)

Send byte `0xFF` followed by exactly 115,200 bytes of RGB565 pixel data. No handshake, no response — pixels go straight to the LCD. This is what `push_image.py` and `push_gif.py` use.

## Technical Details

### Hardware

| | |
|---|---|
| **MCU** | STM32F205VGT6 (Cortex-M3, 120MHz, 1MB flash, 128KB SRAM) |
| **Crystal** | 8.00 MHz HSE |
| **LCD** | 240x240 ILI9341V/ST7789V, FSMC 8080 parallel 16-bit bus |
| **USB** | Full Speed (12 Mbps) CDC ACM, VID 0x0483 / PID 0x5740 |

### PLL Configuration

```
HSE 8MHz → PLLM=8, PLLN=240, PLLP=2, PLLQ=5
→ SYSCLK=120MHz, USB=48MHz, APB1=30MHz, APB2=60MHz
```

### LCD Configuration

- **FSMC**: 16-bit bus (MWID=01), byte writes via `uint8_t*` pointers
- **Addresses**: CMD at `0x60000000`, DATA at `0x60020000`
- **MADCTL**: `0xC8` (MY=1, MX=1, BGR=1)
- **Pixel format**: RGB565 (16-bit, `0x3A` = `0x55`)
- **Y offset**: 50 (ST7789V 320-row controller with 240-row panel)
- **FSMC timing**: ADDSET=2, DATAST=5, BUSTURN=1

### Performance

- **USB throughput**: ~150 KB/s (USB Full Speed CDC hardware limit)
- **Frame transfer**: ~0.7s per 240x240 frame (115,200 bytes)
- **Max FPS**: ~1.4 fps for full-frame updates
- **LCD write speed**: ~8ms per frame (FSMC is not the bottleneck)

### Project Structure

```
custom_display_fw/
├── Makefile                    Build system
├── src/                        Firmware source
│   ├── main.c                  USB CDC, LCD driver, command parser, pixel streaming
│   ├── stm32f2xx.h             STM32F2xx register definitions
│   ├── tusb_config.h           TinyUSB configuration
│   ├── usb_descriptors.c       USB device/configuration/string descriptors
│   └── linker.ld               Linker script
├── scripts/
│   ├── host/                   Scripts for running on a PC (requires Python + Pillow)
│   │   ├── flash_lcd.py        Flash firmware to MCU via bootloader serial protocol
│   │   ├── push_image.py       Push any image (PNG/JPG/etc.) to the display
│   │   ├── push_color.py       Fill display with solid color
│   │   ├── push_gif.py         Stream animated GIF
│   │   └── lcd_cmd.py          Send text command to MCU
│   └── router/                 Scripts for running on the router via SSH (busybox/ash)
│       ├── flash_lcd.sh        Flash firmware to MCU via bootloader serial protocol
│       ├── push_color.sh       Fill display with solid color
│       ├── push_raw.sh         Push raw RGB565 file (115200 bytes)
│       └── lcd_cmd.sh          Send text command to MCU
└── README.md
```

### Router usage (via SSH)

When the display is connected to the router instead of a PC:

```bash
# Copy scripts to router
scp scripts/router/*.sh amplifi:/tmp/

# SSH in, stop uictld, and use the display
ssh amplifi
/etc/init.d/uictld stop       # Must stop or it reflashes stock firmware
sh /tmp/push_color.sh red     # Push solid color
sh /tmp/lcd_cmd.sh fwversion  # Send command

# Flash firmware from the router
scp lcd_firmware.bin amplifi:/tmp/
ssh amplifi 'sh /tmp/flash_lcd.sh /tmp/lcd_firmware.bin'

# Push an image (convert on PC, push raw file via SSH)
# On PC:
ffmpeg -i photo.jpg -vf scale=240:240 -pix_fmt rgb565be -f rawvideo /tmp/image.rgb565
scp /tmp/image.rgb565 amplifi:/tmp/
# On router:
sh /tmp/push_raw.sh /tmp/image.rgb565
```

Note: `uictld` must be stopped first or it will reflash the stock firmware over the custom one:
```bash
ssh amplifi '/etc/init.d/uictld stop'
```
The display board connects to the router internally via header J28 and can be hot-plugged while the router is powered on. For a permanent setup, build a custom router firmware with `lcd_firmware.bin` replacing `/lib/firmware/AFi-R_LCD.bin`.

### Known limitations on the router

The Linux `cdc_acm` driver on the router (kernel 4.1.16) cancels USB read URBs when `/dev/ttyACM0` is closed. This means:

- **Text commands (`lcd_cmd.sh`)** require a background reader started before sending, since the response is lost if no process has the device open when it arrives. The script retries automatically.
- **After running `lcd_cmd.sh`**, the killed background reader can leave the serial port in a state that affects subsequent commands. The pixel push scripts (`push_color.sh`, `push_raw.sh`) send a `\r\n` reset and a black frame flush before each push to recover.
- **Host Python scripts** don't have this issue since pyserial keeps the port open for both read and write on a single file descriptor.

For the most reliable experience, use the **host Python scripts** connected directly via micro-USB.
