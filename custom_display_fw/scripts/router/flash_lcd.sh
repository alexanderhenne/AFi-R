#!/bin/sh
# Flash display firmware from the router via the bootloader serial protocol.
# Usage: flash_lcd.sh <firmware.bin>
#
# Note: uses the same background-reader approach as lcd_cmd.sh due to
# Linux cdc_acm driver limitations with read buffering between open/close.

DEV=/dev/ttyACM0
FW="$1"

if [ -z "$FW" ]; then
    echo "Usage: $0 <firmware.bin>"
    exit 1
fi

if [ ! -f "$FW" ]; then
    echo "Error: $FW not found"
    exit 1
fi

SIZE=$(wc -c < "$FW")
if [ "$SIZE" -ne 916480 ]; then
    echo "Error: firmware is $SIZE bytes, expected 916480"
    exit 1
fi

# Send command and capture response (with retry)
send_cmd() {
    _CMD="$1"
    _WAIT="$2"
    _TRIES=0
    while [ $_TRIES -lt 3 ]; do
        rm -f /tmp/lcd_resp
        dd if="$DEV" bs=1 count=512 of=/tmp/lcd_resp 2>/dev/null &
        _RPID=$!
        usleep 500000
        printf '%s\r\n' "$_CMD" | dd of="$DEV" 2>/dev/null
        sleep "$_WAIT"
        kill $_RPID 2>/dev/null
        wait $_RPID 2>/dev/null
        if [ -s /tmp/lcd_resp ]; then
            cat /tmp/lcd_resp
            rm -f /tmp/lcd_resp
            return 0
        fi
        _TRIES=$((_TRIES+1))
    done
    rm -f /tmp/lcd_resp
    return 1
}

# Stop uictld
echo "Stopping uictld..."
/etc/init.d/uictld stop 2>/dev/null
sleep 1

if [ ! -e "$DEV" ]; then
    echo "Error: $DEV not found"
    exit 1
fi

# Check current mode
echo "Checking mode..."
MODE=$(send_cmd "mode" 2)

if echo "$MODE" | grep -q "APP"; then
    echo "In APP mode, rebooting to bootloader..."
    printf 'blreboot\r\n' | dd of="$DEV" 2>/dev/null
    sleep 4

    # Wait for ttyACM to reappear
    TRIES=0
    while [ ! -e "$DEV" ] && [ $TRIES -lt 10 ]; do
        sleep 1
        TRIES=$((TRIES+1))
    done

    if [ ! -e "$DEV" ]; then
        echo "Error: MCU did not reconnect after reboot"
        exit 1
    fi
    sleep 1
fi

# Verify bootloader mode
echo "Verifying bootloader..."
MODE=$(send_cmd "mode" 2)

if echo "$MODE" | grep -q "BLD"; then
    echo "Bootloader confirmed."
else
    echo "Warning: could not confirm bootloader mode, trying anyway..."
fi

# Initiate firmware transfer
echo "Sending forcefwupgrade=$SIZE..."
RESP=$(send_cmd "forcefwupgrade=$SIZE" 2)

if ! echo "$RESP" | grep -q "wait"; then
    echo "Error: unexpected response: $RESP"
    exit 1
fi

# Send firmware data
echo "Sending firmware ($SIZE bytes)..."
dd if="$FW" of="$DEV" bs=4096 2>/dev/null

# Wait for verification
echo "Waiting for verification..."
sleep 5
rm -f /tmp/lcd_resp
dd if="$DEV" bs=1 count=512 of=/tmp/lcd_resp 2>/dev/null &
RPID=$!
sleep 3
kill $RPID 2>/dev/null
wait $RPID 2>/dev/null
if [ -s /tmp/lcd_resp ]; then
    echo "MCU: $(cat /tmp/lcd_resp)"
fi
rm -f /tmp/lcd_resp

# Boot into new firmware
echo "Sending startapp..."
printf 'startapp\r\n' | dd of="$DEV" 2>/dev/null
sleep 2

echo "Done."
