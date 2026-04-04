#!/bin/sh
# Push a solid color to the LCD display (runs on the router via SSH)
# Usage: push_color.sh [color]
# Colors: red, green, blue, white, black, cyan, magenta, yellow

DEV=/dev/ttyACM0
COLOR=${1:-red}

case "$COLOR" in
    red)     HI="\xf8" LO="\x00" ;;
    green)   HI="\x07" LO="\xe0" ;;
    blue)    HI="\x00" LO="\x1f" ;;
    white)   HI="\xff" LO="\xff" ;;
    black)   HI="\x00" LO="\x00" ;;
    cyan)    HI="\x07" LO="\xff" ;;
    magenta) HI="\xf8" LO="\x1f" ;;
    yellow)  HI="\xff" LO="\xe0" ;;
    *)       echo "Unknown color: $COLOR"; exit 1 ;;
esac

# Build one row (240 pixels = 480 bytes)
ROW=""
i=0
while [ $i -lt 240 ]; do
    ROW="${ROW}${HI}${LO}"
    i=$((i+1))
done

# Reset any partial command state, then flush any stuck 0xFF transfer
printf '\r\n' | dd of="$DEV" 2>/dev/null
sleep 1
{
    printf "\xff"
    dd if=/dev/zero bs=480 count=240 2>/dev/null
} | dd of="$DEV" bs=4096 2>/dev/null
sleep 1

# Send 0xFF prefix + 240 rows of pixel data
{
    printf "\xff"
    r=0
    while [ $r -lt 240 ]; do
        printf "$ROW"
        r=$((r+1))
    done
} | dd of="$DEV" bs=4096 2>/dev/null

echo "Pushed $COLOR"
