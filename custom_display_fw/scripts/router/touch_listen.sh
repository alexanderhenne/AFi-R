#!/bin/sh
# Listen for touch events from the LCD MCU (runs on the router via SSH)
# Usage: touch_listen.sh [/dev/ttyACM0]
# Press Ctrl+C to stop.
#
# Prints lines like:
#   tp 60,120,down
#   tp 62,118,move
#   tp 62,118,up

DEV="${1:-/dev/ttyACM0}"

trap 'kill $RPID 2>/dev/null; exit 0' INT TERM

# Continuous reader — filter for touch event lines
cat "$DEV" | while IFS= read -r line; do
    case "$line" in
        tp\ *) echo "$line" ;;
    esac
done &
RPID=$!

# Flush any stale buffer (non-fatal if write fails)
(printf '\r\n' > "$DEV") 2>/dev/null

echo "Listening for touch events on $DEV... (Ctrl+C to stop)"
wait $RPID
