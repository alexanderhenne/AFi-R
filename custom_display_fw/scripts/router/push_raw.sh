#!/bin/sh
# Push a raw RGB565 file to the LCD display (runs on the router via SSH)
# Usage: push_raw.sh <rgb565_file>
#
# The file must be exactly 115200 bytes of raw RGB565 pixel data.
# Generate on the host with:
#   ffmpeg -i image.png -vf scale=240:240 -pix_fmt rgb565be -f rawvideo image.rgb565
# or with ImageMagick:
#   convert image.png -resize 240x240 -depth 16 rgb:image.rgb565

DEV=/dev/ttyACM0
FILE="$1"

if [ -z "$FILE" ]; then
    echo "Usage: $0 <rgb565_file>"
    echo "File must be exactly 115200 bytes of raw RGB565BE pixel data."
    exit 1
fi

SIZE=$(wc -c < "$FILE")
if [ "$SIZE" -ne 115200 ]; then
    echo "Error: file is $SIZE bytes, expected 115200"
    exit 1
fi

# Reset any partial command state, then flush any stuck 0xFF transfer
printf '\r\n' | dd of="$DEV" 2>/dev/null
sleep 1
{
    printf "\xff"
    dd if=/dev/zero bs=480 count=240 2>/dev/null
} | dd of="$DEV" bs=4096 2>/dev/null
sleep 1

# Send 0xFF prefix + pixel data
{
    printf "\xff"
    cat "$FILE"
} | dd of="$DEV" bs=4096 2>/dev/null

echo "Pushed $FILE"
