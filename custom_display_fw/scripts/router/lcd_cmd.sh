#!/bin/sh
# Send a text command to the LCD MCU (runs on the router via SSH)
# Usage: lcd_cmd.sh <command>
#
# Note: due to a Linux cdc_acm driver limitation, the serial read buffer
# is not active between open/close cycles. A background reader must be
# started before sending the command. This occasionally requires a retry.

DEV=/dev/ttyACM0
CMD="$1"

if [ -z "$CMD" ]; then
    echo "Usage: $0 <command>"
    echo "Commands: fwversion, mode, status, listcmd, blreboot, reboot"
    exit 1
fi

TRIES=0
while [ $TRIES -lt 3 ]; do
    rm -f /tmp/lcd_resp
    dd if="$DEV" bs=1 count=256 of=/tmp/lcd_resp 2>/dev/null &
    RPID=$!
    usleep 500000
    printf '%s\r\n' "$CMD" | dd of="$DEV" 2>/dev/null
    sleep 2
    kill $RPID 2>/dev/null
    wait $RPID 2>/dev/null

    if [ -s /tmp/lcd_resp ]; then
        cat /tmp/lcd_resp
        rm -f /tmp/lcd_resp
        # Ensure no lingering readers steal bytes from the next command
        sleep 1
        exit 0
    fi
    TRIES=$((TRIES+1))
done
rm -f /tmp/lcd_resp
