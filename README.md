# AFi-R

Research, tools, and custom firmware for the Ubiquiti AmpliFi AFi-R router.

## Hardware overview

| Component | Detail |
|---|---|
| **Main SoC** | Qualcomm Atheros QCA956X (MIPS 74Kc, 775 MHz) |
| **RAM** | 128 MB DDR2 |
| **Flash** | 32 MB |
| **WiFi** | 5GHz AR9561 + 2.4GHz QCA988X |
| **Display module** | STM32F205VGT6 board with 240x240 LCD, touchscreen, speaker |
| **OS** | OpenWrt-based (Linux 4.1.16, BusyBox ash, musl libc) |

## Contents

### [Jailbreak guide](docs/jailbreak.md)
Serial console access, enabling SSH, patching `uh-fw-tool`, and flashing custom router firmware.

### [Firmware tools](tools/)
Scripts for extracting, modifying, and repacking the router's SquashFS firmware images.

### [Display system reverse engineering](docs/lcd-display-system.md)
Full hardware and software analysis of the stock display module — MCU firmware, USB CDC protocol, LCD controller, bootloader, and more.

### [Custom display firmware](custom_display_fw/)
Replacement STM32 firmware that lets you push arbitrary images, animated GIFs, and solid colors to the 240x240 display. Includes host and router scripts.

## Quick start

### Enable SSH access
1. Connect a UART reader to the [J11 header](docs/jailbreak.md#j11) inside the router
2. Run `echo 'SSH' | prst_tool -w misc && reboot`
3. [Set up SSH config and key auth](docs/jailbreak.md#enabling-developer-mode-ssh-access), then: `ssh amplifi`

### Push an image to the display
1. [Flash the custom display firmware](custom_display_fw/#flashing)
2. Connect the display board to your PC via micro-USB
3. Run:
   ```
   sudo python3 custom_display_fw/scripts/host/push_image.py photo.jpg
   ```

## Resources

* [FCC teardown photos](https://fccid.io/SWX-AFR/Internal-Photos/Internal-Photos-3028132)
* [Boot log](bootlog.txt)
